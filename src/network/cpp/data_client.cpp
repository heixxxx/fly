#include <network/cpp/data_client.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <cstring>

namespace fly {

std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString> DataClient::request_compressed_data(
    const CMString& host,
    int port,
    const CMString& object_name,
    uint64_t requesting_worker_id,
    uint64_t request_id,
    int timeout_ms)
{
    auto transport = create_tcp_transport();

    int fd = transport->create_connection(host, port);
    if (fd < 0) {
        return {false, nullptr, "", "", "Failed to connect to " + host + ":" + std::to_string(port)};
    }

    transport->set_send_timeout(fd, timeout_ms);
    transport->set_recv_timeout(fd, timeout_ms);

    DataRequestMessage req;
    req.object_name_ = object_name;
    req.requesting_worker_id_ = requesting_worker_id;
    req.request_id_ = request_id;
    CMString encoded_req = MessageProtocol::encode(req);

    if (!transport->send_all(fd, encoded_req.data(), encoded_req.size())) {
        transport->close(fd);
        return {false, nullptr, "", "", "Failed to send request for " + object_name};
    }

    char header[5] = {};
    size_t header_received = 0;
    while (header_received < 5) {
        ssize_t n = transport->recv(fd, header + header_received, 5 - header_received);
        if (n <= 0) {
            transport->close(fd);
            return {false, nullptr, "", "", "Timeout receiving response header for " + object_name};
        }
        header_received += static_cast<size_t>(n);
    }

    uint32_t total_len =
        (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

    if (total_len < 1 || total_len > 256 * 1024 * 1024) {
        transport->close(fd);
        return {false, nullptr, "", "", "Invalid response frame size for " + object_name};
    }

    uint32_t payload_len = total_len - 1;
    CMString payload(payload_len, '\0');
    size_t payload_received = 0;
    while (payload_received < payload_len) {
        ssize_t n = transport->recv(fd, payload.data() + payload_received, payload_len - payload_received);
        if (n <= 0) {
            transport->close(fd);
            return {false, nullptr, "", "", "Timeout receiving response payload for " + object_name};
        }
        payload_received += static_cast<size_t>(n);
    }

    transport->close(fd);

    CMString full_buf;
    full_buf.resize(4 + total_len);
    std::memcpy(&full_buf[0], header, 5);
    if (payload_len > 0) {
        std::memcpy(&full_buf[5], payload.data(), payload_len);
    }

    DataResponseMessage response;
    if (!MessageProtocol::decode(full_buf, response)) {
        return {false, nullptr, "", "", "Failed to decode response for " + object_name};
    }

    // Wire ingress: wrap decoded CMString into FlyBufferPtr (zero-copy move).
    auto buf = CMMakeShared<FlyBuffer>();
    buf->take(std::move(response.compressed_data_));
    return {response.success_, buf, response.py_name_,
            response.write_context_hash_, response.error_message_};
}

}  // namespace fly
