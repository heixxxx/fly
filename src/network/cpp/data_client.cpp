#include <network/cpp/data_client.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace fly {

// Helper: recv exact number of bytes (blocking with timeout)
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

// Helper: send all bytes
static bool send_all(int fd, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::tuple<bool, CMString, CMString> DataClient::request_data(
    const CMString& host,
    int port,
    const CMString& object_name,
    int timeout_ms)
{
    // 1. Create blocking TCP socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return {false, "", "Failed to create socket: " + CMString(std::strerror(errno))};
    }
    
    // Set connect timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // 2. Connect
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return {false, "", "Failed to connect to " + host + ":" + std::to_string(port)};
    }
    
    // 3. Send DataRequestMessage
    DataRequestMessage req;
    req.object_name = object_name;
    CMString encoded_req = MessageProtocol::encode(req);
    
    if (!send_all(fd, encoded_req.data(), encoded_req.size())) {
        ::close(fd);
        return {false, "", "Failed to send request for " + object_name};
    }
    
    // 4. Receive response frame
    // Frame format: [4 bytes total_len] [1 byte msg_type] [payload]
    char header[5] = {};
    if (!recv_exact(fd, header, 5, timeout_ms)) {
        ::close(fd);
        return {false, "", "Timeout receiving response header for " + object_name};
    }
    
    uint32_t total_len =
        (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(header[3]));
    
    if (total_len < 1 || total_len > 256 * 1024 * 1024) {  // Sanity check: max 256MB
        ::close(fd);
        return {false, "", "Invalid response frame size for " + object_name};
    }
    
    uint32_t payload_len = total_len - 1;
    CMString payload(payload_len, '\0');
    if (payload_len > 0 && !recv_exact(fd, payload.data(), payload_len, timeout_ms)) {
        ::close(fd);
        return {false, "", "Timeout receiving response payload for " + object_name};
    }
    
    // 5. Close socket
    ::close(fd);
    
    // 6. Decode DataResponseMessage
    // Reconstruct the full buffer for MessageProtocol::decode
    CMString full_buf;
    full_buf.resize(4 + total_len);
    std::memcpy(&full_buf[0], header, 5);
    if (payload_len > 0) {
        std::memcpy(&full_buf[5], payload.data(), payload_len);
    }
    
    DataResponseMessage response;
    if (!MessageProtocol::decode(full_buf, response)) {
        return {false, "", "Failed to decode response for " + object_name};
    }
    
    return {response.success, response.data, response.error_message};
}

}  // namespace fly