#include <network/cpp/metadata_client.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <cstring>
#include <cerrno>

namespace fly {

MetadataClient::MetadataClient(CMSharedPtr<Transport> transport)
    : transport_(std::move(transport)) {}

MetadataClient::MetadataClient()
    : MetadataClient(create_tcp_transport()) {}

MetadataClient::DataLocation MetadataClient::query_data_location(
    const CMString& master_host,
    int master_port,
    const CMString& object_name,
    int timeout_ms)
{
    DataLocation result;

    int fd = transport_->create_connection(master_host, master_port);
    if (fd < 0) {
        result.error_ = "Failed to connect to Master " + master_host + ":" + std::to_string(master_port);
        return result;
    }

    transport_->set_send_timeout(fd, timeout_ms);
    transport_->set_recv_timeout(fd, timeout_ms);

    DataQueryMessage req;
    req.object_name_ = object_name;
    CMString encoded_req = MessageProtocol::encode(req);

    // send all
    const char* send_ptr = encoded_req.data();
    size_t send_remaining = encoded_req.size();
    while (send_remaining > 0) {
        ssize_t n = transport_->send(fd, send_ptr, send_remaining);
        if (n < 0) {
            result.error_ = "Failed to send DataQuery for " + object_name;
            transport_->close(fd);
            return result;
        }
        send_ptr += n;
        send_remaining -= static_cast<size_t>(n);
    }

    // recv frame prefix (9 bytes: 8B header + 1B type)
    char header[9] = {};
    if (!recv_exact(transport_.get(), fd, header, 9)) {
        result.error_ = "Timeout receiving DataLocation header for " + object_name;
        transport_->close(fd);
        return result;
    }

    uint64_t total_len = 0;
    if (!parse_frame_header(header, total_len)) {
        result.error_ = "Invalid DataLocation frame header for " + object_name;
        transport_->close(fd);
        return result;
    }

    // 16MB 是【元数据域内界】（DataLocation 副本列表的实际量级），与数据面
    // 帧域（256TB）无关——元数据帧不该携带大对象。
    if (total_len < 1 || total_len > 16 * 1024 * 1024) {
        result.error_ = "Invalid DataLocation frame size for " + object_name;
        transport_->close(fd);
        return result;
    }

    uint64_t payload_len = total_len - 1;
    CMString payload(static_cast<size_t>(payload_len), '\0');
    if (payload_len > 0) {
        if (!recv_exact(transport_.get(), fd, payload.data(), static_cast<size_t>(payload_len))) {
            result.error_ = "Timeout receiving DataLocation payload for " + object_name;
            transport_->close(fd);
            return result;
        }
    }

    transport_->close(fd);

    CMString full_buf;
    full_buf.resize(8 + total_len);
    std::memcpy(&full_buf[0], header, 9);
    if (payload_len > 0) {
        std::memcpy(&full_buf[9], payload.data(), static_cast<size_t>(payload_len));
    }

    DataLocationMessage response;
    if (!MessageProtocol::decode(full_buf, response)) {
        result.error_ = "Failed to decode DataLocation for " + object_name;
        return result;
    }

    result.found_ = response.success_;
    result.can_still_produce_ = response.can_still_produce_;
    if (!response.success_) {
        result.error_ = "Master has no location for " + object_name;
    } else {
        // Populate all replicas; mirror the first one into the convenience fields.
        for (const auto& dl : response.locations_) {
            ReplicaLocation rl;
            rl.worker_id_ = dl.worker_id;
            rl.host_ = dl.host;
            rl.port_ = dl.port;
            rl.storage_only_ = dl.storage_only != 0;
            result.all_locations_.push_back(std::move(rl));
        }
        if (!result.all_locations_.empty()) {
            const auto& first = result.all_locations_.front();
            result.worker_id_ = first.worker_id_;
            result.host_ = first.host_;
            result.port_ = first.port_;
        }
    }
    return result;
}

}  // namespace fly
