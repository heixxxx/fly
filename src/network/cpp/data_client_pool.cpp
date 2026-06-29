#include <network/cpp/data_client_pool.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <log/cpp/logger.h>
#include <cstring>
#include <chrono>
#include <thread>

namespace fly {

DataClientPool::DataClientPool(CMSharedPtr<Transport> transport, int64_t pool_size)
    : transport_(std::move(transport))
    , pool_size_(pool_size > 0 ? pool_size : 2) {}

DataClientPool::DataClientPool(int64_t pool_size)
    : DataClientPool(create_tcp_transport(), pool_size) {}

DataClientPool::~DataClientPool() {
    stop();
}

std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString, ReadError> DataClientPool::request(
    const CMString& host,
    int port,
    const CMString& object_name,
    uint64_t requesting_worker_id,
    uint64_t request_id,
    int timeout_ms)
{
    if (stopped_.load()) {
        return {false, nullptr, "", "", "Pool stopped", ReadError::SHUTDOWN};
    }

    {
        std::unique_lock<std::mutex> lk(mutex_);
        slot_cv_.wait(lk, [&] {
            return stopped_.load() || active_count_.load() < static_cast<int>(pool_size_);
        });
        if (stopped_.load()) {
            return {false, nullptr, "", "", "Pool stopped", ReadError::SHUTDOWN};
        }
        active_count_.fetch_add(1);
    }

    auto release_slot = [&]() {
        active_count_.fetch_sub(1);
        slot_cv_.notify_one();
    };

    int fd = transport_->create_connection(host, port);
    if (fd < 0) {
        release_slot();
        return {false, nullptr, "", "",
                "Failed to connect to " + host + ":" + std::to_string(port),
                ReadError::NETWORK};
    }

    transport_->set_recv_timeout(fd, 30000);
    transport_->set_send_timeout(fd, 30000);

    DataRequestMessage req;
    req.object_name_ = object_name;
    req.requesting_worker_id_ = requesting_worker_id;
    req.request_id_ = request_id;
    CMString encoded_req = MessageProtocol::encode(req);

    // NOTE: this pool performs exactly ONE request per call. DATA_NOT_READY is
    // returned to the caller (ReadError::DATA_NOT_READY) so the TIER2 layer
    // owns backoff/retry policy. No internal polling loop here.
    while (true) {
        // send all
        const char* send_ptr = encoded_req.data();
        size_t send_remaining = encoded_req.size();
        while (send_remaining > 0) {
            ssize_t n = transport_->send(fd, send_ptr, send_remaining);
            if (n < 0) {
                ERR("[DCP] send failed: obj={} fd={} errno={}", object_name, fd, errno);
                transport_->close(fd);
                release_slot();
                return {false, nullptr, "", "",
                        "Connection lost sending request for " + object_name,
                        ReadError::NETWORK};
            }
            send_ptr += n;
            send_remaining -= static_cast<size_t>(n);
        }

        // ── Two-segment response read (DATA_RESPONSE protocol) ──

        // 1. Read 5B frame header [4B total_len][1B type]
        char frame_header[5];
        size_t header_received = 0;
        while (header_received < 5) {
            ssize_t n = transport_->recv(fd, frame_header + header_received, 5 - header_received);
            if (n <= 0) {
                ERR("[DCP] recv header failed: obj={} fd={} errno={}", object_name, fd, errno);
                transport_->close(fd);
                release_slot();
                return {false, nullptr, "", "",
                        "Connection lost for " + object_name,
                        ReadError::NETWORK};
            }
            header_received += static_cast<size_t>(n);
        }
        uint32_t total_len =
            (static_cast<uint32_t>(static_cast<unsigned char>(frame_header[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(frame_header[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(frame_header[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(frame_header[3]));
        if (total_len < 6 || total_len > 256 * 1024 * 1024) {
            ERR("[DCP] invalid total_len={}: obj={} fd={}", total_len, object_name, fd);
            transport_->close(fd);
            release_slot();
            return {false, nullptr, "", "",
                    "Invalid response for " + object_name,
                    ReadError::NETWORK};
        }

        // 2. Read 5B sub-header [4B small_fields_len][1B has_raw]
        char sub_header[5];
        size_t sub_received = 0;
        while (sub_received < 5) {
            ssize_t n = transport_->recv(fd, sub_header + sub_received, 5 - sub_received);
            if (n <= 0) {
                transport_->close(fd);
                release_slot();
                return {false, nullptr, "", "",
                        "Connection lost for " + object_name,
                        ReadError::NETWORK};
            }
            sub_received += static_cast<size_t>(n);
        }
        uint32_t small_fields_len = 0;
        bool has_raw = false;
        DataResponseProtocol::parse_sub_header(sub_header, small_fields_len, has_raw);

        // 3. Read small_fields_len bytes → FLY_DECODE → msg
        CMString small_payload(small_fields_len, '\0');
        if (small_fields_len > 0) {
            size_t sf_received = 0;
            while (sf_received < small_fields_len) {
                ssize_t n = transport_->recv(fd, small_payload.data() + sf_received,
                                              small_fields_len - sf_received);
                if (n <= 0) {
                    transport_->close(fd);
                    release_slot();
                    return {false, nullptr, "", "",
                            "Connection lost for " + object_name,
                            ReadError::NETWORK};
                }
                sf_received += static_cast<size_t>(n);
            }
        }
        DataResponseMessage response;
        if (!DataResponseProtocol::decode_small_fields(small_payload, response)) {
            ERR("[DCP] decode failed: obj={} fd={}", object_name, fd);
            transport_->close(fd);
            release_slot();
            return {false, nullptr, "", "",
                    "Failed to decode response for " + object_name,
                    ReadError::NETWORK};
        }

        // 4. If has_raw: read raw payload directly into FlyBuffer
        FlyBufferPtr data_buf;
        if (has_raw) {
            uint32_t raw_len = DataResponseProtocol::raw_len_from_total(total_len, small_fields_len);
            data_buf = CMMakeShared<FlyBuffer>();
            data_buf->resize(raw_len);
            size_t raw_received = 0;
            while (raw_received < raw_len) {
                ssize_t n = transport_->recv(fd, data_buf->data() + raw_received,
                                              raw_len - raw_received);
                if (n <= 0) {
                    ERR("[DCP] recv raw failed: obj={} fd={} errno={}", object_name, fd, errno);
                    transport_->close(fd);
                    release_slot();
                    return {false, nullptr, "", "",
                            "Connection lost receiving payload for " + object_name,
                            ReadError::NETWORK};
                }
                raw_received += static_cast<size_t>(n);
            }
        }

        if (response.success_) {
            DBG("[DCP] success: obj={} fd={} data_size={}", object_name, fd,
                data_buf ? data_buf->size() : 0);
            transport_->close(fd);
            release_slot();
            return {true, data_buf, response.py_name_,
                    response.write_context_hash_, "", ReadError::NONE};
        }

        // Failure: classify and return. DATA_NOT_READY and OBJECT_NOT_FOUND are
        // protocol-level; everything else is NETWORK. The caller (TIER2) decides
        // whether/how to retry — the pool does not poll.
        transport_->close(fd);
        release_slot();
        ReadError rerr = (response.error_message_ == "DATA_NOT_READY")
                             ? ReadError::DATA_NOT_READY
                         : (response.error_message_ == "OBJECT_NOT_FOUND")
                             ? ReadError::OBJECT_NOT_FOUND
                             : ReadError::NETWORK;
        return {false, nullptr, "", "", response.error_message_, rerr};
    }
}

void DataClientPool::stop() {
    stopped_.store(true);
    slot_cv_.notify_all();

    std::unique_lock<std::mutex> lk(mutex_);
    slot_cv_.wait(lk, [&] { return active_count_.load() == 0; });
}

}  // namespace fly
