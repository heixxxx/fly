#include <network/cpp/metadata_client.h>
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

    if (!net_send_all(fd, encoded_req.data(), encoded_req.size())) {
        result.error = "Failed to send DataQuery for " + object_name;
        ::close(fd);
        return result;
    }

    char header[5] = {};
    if (!net_recv_exact(fd, header, 5, timeout_ms)) {
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
    if (payload_len > 0 && !net_recv_exact(fd, payload.data(), payload_len, timeout_ms)) {
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
