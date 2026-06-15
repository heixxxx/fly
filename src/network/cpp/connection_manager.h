#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace fly {

enum class TransportEventType : uint8_t {
    CONNECT = 0,
    DATA = 1,
    DISCONNECT = 2,
    ERROR = 3
};

struct TransportEvent {
    TransportEventType type_;
    uint64_t conn_id_ = 0;
    CMString data_;
    int error_code_ = 0;

    FLY_SERIALIZE(type_, conn_id_, data_, error_code_);
};

class ConnectionManager {
public:
    virtual ~ConnectionManager() = default;

    virtual void listen(const CMString& address, int port) = 0;
    virtual void stop_listening() = 0;

    virtual uint64_t connect(const CMString& address, int port) = 0;

    virtual ssize_t send(uint64_t conn_id, const CMString& data) = 0;
    virtual ssize_t recv(uint64_t conn_id, CMString& buffer, size_t max_size) = 0;

    virtual CMVector<TransportEvent> poll(int timeout_ms) = 0;

    virtual void close(uint64_t conn_id) = 0;
    virtual void close_all() = 0;
    virtual bool is_connected(uint64_t conn_id) const = 0;

    virtual size_t connection_count() const = 0;

    virtual int get_bound_port() const = 0;
};

CMUniquePtr<ConnectionManager> create_connection_manager(const CMString& type);

}  // namespace fly
