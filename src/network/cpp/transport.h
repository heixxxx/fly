#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace fly {

// 传输事件类型
enum class TransportEventType : uint8_t {
    CONNECT = 0,      // 新连接建立
    DATA = 1,         // 数据接收
    DISCONNECT = 2,   // 连接关闭
    ERROR = 3         // 连接错误
};

// 传输事件（poll() 返回）
struct TransportEvent {
    TransportEventType type;
    uint64_t conn_id = 0;       // 连接标识符
    CMString data;              // 数据缓冲（DATA 事件）
    int error_code = 0;         // 错误码（ERROR 事件）
    
    FLY_SERIALIZE(type, conn_id, data, error_code);
};

// 抽象传输接口 - 实现可替换
class TransportLayer {
public:
    virtual ~TransportLayer() = default;
    
    // 服务端操作
    virtual void listen(const CMString& address, int port) = 0;
    virtual void stop_listening() = 0;
    
    // 客户端操作
    virtual uint64_t connect(const CMString& address, int port) = 0;
    
    // 非阻塞 I/O
    virtual ssize_t send(uint64_t conn_id, const CMString& data) = 0;
    virtual ssize_t recv(uint64_t conn_id, CMString& buffer, size_t max_size) = 0;
    
    // 事件轮询（Reactor 核心）
    virtual CMVector<TransportEvent> poll(int timeout_ms) = 0;
    
    // 连接管理
    virtual void close(uint64_t conn_id) = 0;
    virtual void close_all() = 0;
    virtual bool is_connected(uint64_t conn_id) const = 0;
    
    // 统计
    virtual size_t connection_count() const = 0;
    
    // 获取 listen 绑定的实际端口 (port=0 时由内核分配)
    virtual int get_bound_port() const = 0;
};

// 工厂函数 - 根据配置创建实现
std::unique_ptr<TransportLayer> create_transport(const CMString& type);

}  // namespace fly