#include <network/cpp/metadata_client.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace fly {

static bool recv_exact(int fd, char* buf, size_t len, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    size_t received = 0;
    while (received < len) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) return false;

        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(remaining / 1000);
        tv.tv_usec = static_cast<suseconds_t>((remaining % 1000) * 1000);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ssize_t n = ::recv(fd, buf + received, len - received, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

static bool send_all(int fd, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

MetadataClient::DataLocation MetadataClient::query_data_location(
    const CMString& master_host,
    int master_port,
    const CMString& object_name,
    int timeout_ms)
{
    DataLocation result;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        result.error = "Failed to create socket: " + CMString(std::strerror(errno));
        return result;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(master_host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(master_port));

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        result.error = "Failed to connect to Master " + master_host + ":" + std::to_string(master_port);
        ::close(fd);
        return result;
    }

    DataQueryMessage req;
    req.object_name = object_name;
    CMString encoded_req = MessageProtocol::encode(req);

    if (!send_all(fd, encoded_req.data(), encoded_req.size())) {
        result.error = "Failed to send DataQuery for " + object_name;
        ::close(fd);
        return result;
    }

    char header[5] = {};
    if (!recv_exact(fd, header, 5, timeout_ms)) {
        result.error = "Timeout receiving DataLocation header for " + object_name;
        ::close(fd);
        return result;
    }

    uint32_t total_len =
        (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

    if (total_len < 1 || total_len > 16 * 1024 * 1024) {
        result.error = "Invalid DataLocation frame size for " + object_name;
        ::close(fd);
        return result;
    }

    uint32_t payload_len = total_len - 1;
    CMString payload(payload_len, '\0');
    if (payload_len > 0 && !recv_exact(fd, payload.data(), payload_len, timeout_ms)) {
        result.error = "Timeout receiving DataLocation payload for " + object_name;
        ::close(fd);
        return result;
    }

    ::close(fd);

    CMString full_buf;
    full_buf.resize(4 + total_len);
    std::memcpy(&full_buf[0], header, 5);
    if (payload_len > 0) {
        std::memcpy(&full_buf[5], payload.data(), payload_len);
    }

    DataLocationMessage response;
    if (!MessageProtocol::decode(full_buf, response)) {
        result.error = "Failed to decode DataLocation for " + object_name;
        return result;
    }

    result.found = response.success;
    result.worker_id = response.worker_id;
    result.host = response.data_host;
    result.port = response.data_port;
    if (!response.success) {
        result.error = "Master has no location for " + object_name;
    }
    return result;
}

}  // namespace fly
