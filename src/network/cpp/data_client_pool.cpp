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

std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString> DataClientPool::request(
    const CMString& host,
    int port,
    const CMString& object_name,
    uint64_t requesting_worker_id,
    uint64_t request_id,
    int timeout_ms)
{
    if (stopped_.load()) {
        return {false, nullptr, "", "", "Pool stopped"};
    }

    {
        std::unique_lock<std::mutex> lk(mutex_);
        slot_cv_.wait(lk, [&] {
            return stopped_.load() || active_count_.load() < static_cast<int>(pool_size_);
        });
        if (stopped_.load()) {
            return {false, nullptr, "", "", "Pool stopped"};
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
        return {false, nullptr, "", "", "Failed to connect to " + host + ":" + std::to_string(port)};
    }

    transport_->set_recv_timeout(fd, 30000);
    transport_->set_send_timeout(fd, 30000);

    DataRequestMessage req;
    req.object_name_ = object_name;
    req.requesting_worker_id_ = requesting_worker_id;
    req.request_id_ = request_id;
    CMString encoded_req = MessageProtocol::encode(req);

    constexpr int POLL_INTERVAL_MS = 100;

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
                return {false, nullptr, "", "", "Connection lost sending request for " + object_name};
            }
            send_ptr += n;
            send_remaining -= static_cast<size_t>(n);
        }

        // recv header (5 bytes)
        char header[5] = {};
        size_t header_received = 0;
        while (header_received < 5) {
            ssize_t n = transport_->recv(fd, header + header_received, 5 - header_received);
            if (n <= 0) {
                ERR("[DCP] recv header failed: obj={} fd={} errno={}", object_name, fd, errno);
                transport_->close(fd);
                release_slot();
                return {false, nullptr, "", "", "Connection lost for " + object_name};
            }
            header_received += static_cast<size_t>(n);
        }

        uint32_t total_len =
            (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

        if (total_len < 1 || total_len > 256 * 1024 * 1024) {
            ERR("[DCP] invalid total_len={}: obj={} fd={}", total_len, object_name, fd);
            transport_->close(fd);
            release_slot();
            return {false, nullptr, "", "", "Invalid response for " + object_name};
        }

        uint32_t payload_len = total_len - 1;
        CMString payload(payload_len, '\0');
        size_t payload_received = 0;
        while (payload_received < payload_len) {
            ssize_t n = transport_->recv(fd, payload.data() + payload_received, payload_len - payload_received);
            if (n <= 0) {
                ERR("[DCP] recv payload failed: obj={} fd={} payload_len={} errno={}", object_name, fd, payload_len, errno);
                transport_->close(fd);
                release_slot();
                return {false, nullptr, "", "", "Connection lost receiving payload for " + object_name};
            }
            payload_received += static_cast<size_t>(n);
        }

        CMString full_buf;
        full_buf.resize(4 + total_len);
        std::memcpy(&full_buf[0], header, 5);
        if (payload_len > 0) {
            std::memcpy(&full_buf[5], payload.data(), payload_len);
        }

        DataResponseMessage response;
        if (!MessageProtocol::decode(full_buf, response)) {
            ERR("[DCP] decode failed: obj={} fd={} frame_size={}", object_name, fd, full_buf.size());
            transport_->close(fd);
            release_slot();
            return {false, nullptr, "", "", "Failed to decode response for " + object_name};
        }

        if (response.success_) {
            DBG("[DCP] success: obj={} fd={} data_size={}", object_name, fd, response.compressed_data_.size());
            transport_->close(fd);
            release_slot();
            // Wire ingress: wrap decoded CMString into FlyBufferPtr (zero-copy move).
            auto buf = CMMakeShared<FlyBuffer>();
            buf->take(std::move(response.compressed_data_));
            return {true, buf, response.py_name_,
                    response.write_context_hash_, ""};
        }

        if (response.error_message_ == "DATA_NOT_READY") {
            if (stopped_.load()) {
                transport_->close(fd);
                release_slot();
                return {false, nullptr, "", "", "Pool stopped"};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            continue;
        }

        transport_->close(fd);
        release_slot();
        return {false, nullptr, "", "", response.error_message_};
    }
}

void DataClientPool::stop() {
    stopped_.store(true);
    slot_cv_.notify_all();

    std::unique_lock<std::mutex> lk(mutex_);
    slot_cv_.wait(lk, [&] { return active_count_.load() == 0; });
}

}  // namespace fly
