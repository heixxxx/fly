#include <network/cpp/data_client_pool.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_utils.h>
#include <log/cpp/logger.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <thread>

namespace fly {

DataClientPool::DataClientPool(int64_t pool_size)
    : pool_size_(pool_size > 0 ? pool_size : 2) {}

DataClientPool::~DataClientPool() {
    stop();
}

int DataClientPool::create_connection(const CMString& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

std::tuple<bool, CMString, CMString, CMString, CMString> DataClientPool::request(
    const CMString& host,
    int port,
    const CMString& object_name,
    uint64_t requesting_worker_id,
    uint64_t request_id,
    int timeout_ms)
{
    if (stopped_.load()) {
        return {false, "", "", "", "Pool stopped"};
    }

    {
        std::unique_lock<std::mutex> lk(mutex_);
        slot_cv_.wait(lk, [&] {
            return stopped_.load() || active_count_.load() < static_cast<int>(pool_size_);
        });
        if (stopped_.load()) {
            return {false, "", "", "", "Pool stopped"};
        }
        active_count_.fetch_add(1);
    }

    auto release_slot = [&]() {
        active_count_.fetch_sub(1);
        slot_cv_.notify_one();
    };

    int fd = create_connection(host, port);
    if (fd < 0) {
        release_slot();
        return {false, "", "", "", "Failed to connect to " + host + ":" + std::to_string(port)};
    }

    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    DataRequestMessage req;
    req.object_name_ = object_name;
    req.requesting_worker_id_ = requesting_worker_id;
    req.request_id_ = request_id;
    CMString encoded_req = MessageProtocol::encode(req);

    constexpr int POLL_INTERVAL_MS = 100;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        if (!net_send_all(fd, encoded_req.data(), encoded_req.size())) {
            ERR("[DCP] send failed: obj={} fd={} errno={}", object_name, fd, errno);
            ::close(fd);
            release_slot();
            return {false, "", "", "", "Connection lost sending request for " + object_name};
        }

        char header[5] = {};
        if (!net_recv_exact(fd, header, 5, 30000)) {
            ERR("[DCP] recv header failed: obj={} fd={} errno={}", object_name, fd, errno);
            ::close(fd);
            release_slot();
            return {false, "", "", "", "Connection lost for " + object_name};
        }

        uint32_t total_len =
            (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

        if (total_len < 1 || total_len > 256 * 1024 * 1024) {
            ERR("[DCP] invalid total_len={}: obj={} fd={}", total_len, object_name, fd);
            ::close(fd);
            release_slot();
            return {false, "", "", "", "Invalid response for " + object_name};
        }

        uint32_t payload_len = total_len - 1;
        CMString payload(payload_len, '\0');
        if (payload_len > 0 && !net_recv_exact(fd, payload.data(), payload_len, 30000)) {
            ERR("[DCP] recv payload failed: obj={} fd={} payload_len={} errno={}", object_name, fd, payload_len, errno);
            ::close(fd);
            release_slot();
            return {false, "", "", "", "Connection lost receiving payload for " + object_name};
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
            ::close(fd);
            release_slot();
            return {false, "", "", "", "Failed to decode response for " + object_name};
        }

        if (response.success_) {
            DBG("[DCP] success: obj={} fd={} data_size={}", object_name, fd, response.compressed_data_.size());
            ::close(fd);
            release_slot();
            return {true, response.compressed_data_, response.py_name_,
                    response.write_context_hash_, ""};
        }

        if (response.error_message_ == "DATA_NOT_READY") {
            if (std::chrono::steady_clock::now() >= deadline) {
                ::close(fd);
                release_slot();
                return {false, "", "", "", "Timeout waiting for data: " + object_name};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            continue;
        }

        ::close(fd);
        release_slot();
        return {false, "", "", "", response.error_message_};
    }
}

void DataClientPool::stop() {
    stopped_.store(true);
    slot_cv_.notify_all();

    std::unique_lock<std::mutex> lk(mutex_);
    slot_cv_.wait(lk, [&] { return active_count_.load() == 0; });
}

}  // namespace fly
