#include <network/cpp/data_client.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_quality_monitor.h>
#include <cstring>
#include <chrono>

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

    // Passive RTT probe: time the full request/response round-trip. Only a
    // completed exchange (even a DATA_NOT_READY verdict) yields a meaningful
    // sample; the mid-function early returns below skip this.
    auto rtt_start = std::chrono::steady_clock::now();

    DataRequestMessage req;
    req.object_name_ = object_name;
    req.requesting_worker_id_ = requesting_worker_id;
    req.request_id_ = request_id;
    CMString encoded_req = MessageProtocol::encode(req);

    if (!transport->send_all(fd, encoded_req.data(), encoded_req.size())) {
        transport->close(fd);
        return {false, nullptr, "", "", "Failed to send request for " + object_name};
    }

    // ── Two-segment response read (DATA_RESPONSE protocol) ──

    // 1. Read 5B frame header [4B total_len][1B type]
    char frame_header[5];
    if (!recv_exact(transport.get(), fd, frame_header, 5)) {
        transport->close(fd);
        return {false, nullptr, "", "", "Timeout receiving response header for " + object_name};
    }
    uint32_t total_len = read_be32(frame_header);
    if (total_len < 6) {  // 1(type)+4(small_len)+1(has_raw) minimum
        transport->close(fd);
        return {false, nullptr, "", "", "Invalid response frame size for " + object_name};
    }

    // 2. Read 5B sub-header [4B small_fields_len][1B has_raw]
    char sub_header[5];
    if (!recv_exact(transport.get(), fd, sub_header, 5)) {
        transport->close(fd);
        return {false, nullptr, "", "", "Timeout receiving response sub-header for " + object_name};
    }
    uint32_t small_fields_len = 0;
    bool has_raw = false;
    DataResponseProtocol::parse_sub_header(sub_header, small_fields_len, has_raw);

    // 3. Read small_fields_len bytes → FLY_DECODE → msg
    CMString small_payload(small_fields_len, '\0');
    if (small_fields_len > 0) {
        if (!recv_exact(transport.get(), fd, small_payload.data(), small_fields_len)) {
            transport->close(fd);
            return {false, nullptr, "", "", "Timeout receiving response fields for " + object_name};
        }
    }
    DataResponseMessage response;
    if (!DataResponseProtocol::decode_small_fields(small_payload, response)) {
        transport->close(fd);
        return {false, nullptr, "", "", "Failed to decode response for " + object_name};
    }

    // 4. If has_raw: read raw payload directly into FlyBuffer (zero user-space copy)
    FlyBufferPtr buf;
    if (has_raw) {
        uint32_t raw_len = DataResponseProtocol::raw_len_from_total(total_len, small_fields_len);
        if (raw_len > 256 * 1024 * 1024) {
            transport->close(fd);
            return {false, nullptr, "", "", "Invalid raw payload size for " + object_name};
        }
        buf = CMMakeShared<FlyBuffer>();
        buf->resize(raw_len);
        if (!recv_exact(transport.get(), fd, buf->data(), raw_len)) {
            transport->close(fd);
            return {false, nullptr, "", "", "Timeout receiving raw payload for " + object_name};
        }
    }

    transport->close(fd);
    double rtt_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - rtt_start)
                        .count();
    NetQualityMonitor::instance().update_rtt(host, rtt_ms);
    return {response.success_, buf, response.py_name_,
            response.write_context_hash_, response.error_message_};
}

}  // namespace fly
