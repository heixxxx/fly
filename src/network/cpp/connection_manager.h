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

    // 监听 address:port。返回 false = bind/epoll 失败（调用方决定是否致命），
    // 不抛异常（与 connect 的 0 哨兵错误通道对称）。
    virtual bool listen(const CMString& address, int port) = 0;
    virtual void stop_listening() = 0;

    // Connect to remote. Returns conn_id (>=1) on success, 0 on failure.
    // Never throws — caller decides whether failure is fatal.
    // (conn_id allocation starts at 1, so 0 is an unambiguous failure sentinel.)
    virtual uint64_t connect(const CMString& address, int port) = 0;

    virtual ssize_t send(uint64_t conn_id, const CMString& data) = 0;

    virtual CMVector<TransportEvent> poll(int timeout_ms) = 0;

    virtual void close(uint64_t conn_id) = 0;
    virtual void close_all() = 0;
    virtual bool is_connected(uint64_t conn_id) const = 0;

    virtual size_t connection_count() const = 0;

    virtual int get_bound_port() const = 0;

    // 调试：conn_id 的对端指纹（fd + remote addr:port）。fd 已失效/未知时
    // 返回错误描述。用于连接漂移排查（send 目标与预期对端比对）。
    virtual CMString get_peer_info(uint64_t conn_id) const {
        return "unsupported";
    }
};

CMUniquePtr<ConnectionManager> create_connection_manager(const CMString& type);

}  // namespace fly
