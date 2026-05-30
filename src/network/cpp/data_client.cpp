#include <network/cpp/data_client.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_utils.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace fly {

std::tuple<bool, CMString, CMString, CMString> DataClient::request_compressed_data(
    const CMString& host,
    int port,
    const CMString& object_name,
    int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return {false, "", "", "Failed to create socket: " + CMString(std::strerror(errno))};
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return {false, "", "", "Failed to connect to " + host + ":" + std::to_string(port)};
    }

    DataRequestMessage req;
    req.object_name = object_name;
    CMString encoded_req = MessageProtocol::encode(req);

    if (!net_send_all(fd, encoded_req.data(), encoded_req.size())) {
        ::close(fd);
        return {false, "", "", "Failed to send compressed request for " + object_name};
    }

    char header[5] = {};
    if (!net_recv_exact(fd, header, 5, timeout_ms)) {
        ::close(fd);
        return {false, "", "", "Timeout receiving response header for " + object_name};
    }

    uint32_t total_len =
        (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

    if (total_len < 1 || total_len > 256 * 1024 * 1024) {
        ::close(fd);
        return {false, "", "", "Invalid response frame size for " + object_name};
    }

    uint32_t payload_len = total_len - 1;

    CMString payload(payload_len, '\0');
    if (payload_len > 0 && !net_recv_exact(fd, payload.data(), payload_len, timeout_ms)) {
        ::close(fd);
        return {false, "", "", "Timeout receiving response payload for " + object_name};
    }

    ::close(fd);

    CMString full_buf;
    full_buf.resize(4 + total_len);
    std::memcpy(&full_buf[0], header, 5);
    if (payload_len > 0) {
        std::memcpy(&full_buf[5], payload.data(), payload_len);
    }

    DataResponseMessage response;
    if (!MessageProtocol::decode(full_buf, response)) {
        return {false, "", "", "Failed to decode response for " + object_name};
    }

    return {response.success, response.compressed_data, response.py_name, response.error_message};
}

}  // namespace fly