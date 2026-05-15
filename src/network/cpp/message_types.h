#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

namespace fly {

// 消息类型枚举
enum class MessageType : uint8_t {
    REGISTER = 1,
    REGISTER_ACK = 2,
    HEARTBEAT = 3,
    TASK_SUBMIT = 4,
    TASK_ASSIGN = 5,
    TASK_COMPLETE = 6,
    TASK_FAILED = 7,
    DATA_READY = 8,
    DATA_QUERY = 9,
    DATA_LOCATION = 10,
    DATA_REQUEST = 11,
    DATA_RESPONSE = 12,
    SHUTDOWN = 13,
    DATABASE_FREEZE = 14,
    IDX_REQUEST = 15,
    IDX_RESPONSE = 16,
    CLEANUP_TASK = 17,
    CLEANUP_COMPLETE = 18,
};

// 基础消息头（所有消息继承）
struct MessageHeader {
    MessageType type = MessageType::REGISTER;
    uint32_t message_id = 0;
    uint64_t timestamp = 0;
    
    FLY_SERIALIZE(type, message_id, timestamp);
};

// Worker → Master: 注册
struct RegisterMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString role;
    CMVector<CMString> attributes;
    
    static constexpr MessageType msg_type = MessageType::REGISTER;
    
    FLY_SERIALIZE(header, worker_id, role, attributes);
};

// Master → Worker: 注册确认
struct RegisterAckMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString master_address;
    int32_t master_port = 0;
    
    static constexpr MessageType msg_type = MessageType::REGISTER_ACK;
    
    FLY_SERIALIZE(header, worker_id, master_address, master_port);
};

// Worker → Master: 心跳
struct HeartbeatMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMVector<uint64_t> running_tasks;
    CMVector<CMString> attributes;
    
    static constexpr MessageType msg_type = MessageType::HEARTBEAT;
    
    FLY_SERIALIZE(header, worker_id, running_tasks, attributes);
};

// Worker → Worker: 数据请求（重 I/O）
struct DataRequestMessage {
    MessageHeader header;
    CMString object_name;
    uint64_t requesting_worker_id = 0;
    
    static constexpr MessageType msg_type = MessageType::DATA_REQUEST;
    
    FLY_SERIALIZE(header, object_name, requesting_worker_id);
};

// Worker → Worker: 数据响应（可能较大）
struct DataResponseMessage {
    MessageHeader header;
    CMString object_name;
    CMString data;  // 二进制 payload（可能 MB 级）
    
    static constexpr MessageType msg_type = MessageType::DATA_RESPONSE;
    
    FLY_SERIALIZE(header, object_name, data);
};

// Master → Worker: 任务分配
struct TaskAssignMessage {
    MessageHeader header;
    uint64_t task_id = 0;
    CMString task_name;
    CMString task_module;
    CMVector<CMString> args;
    
    static constexpr MessageType msg_type = MessageType::TASK_ASSIGN;
    
    FLY_SERIALIZE(header, task_id, task_name, task_module, args);
};

// Worker → Master: 任务完成
struct TaskCompleteMessage {
    MessageHeader header;
    uint64_t task_id = 0;
    uint64_t worker_id = 0;
    CMVector<CMString> written_objects;
    
    static constexpr MessageType msg_type = MessageType::TASK_COMPLETE;
    
    FLY_SERIALIZE(header, task_id, worker_id, written_objects);
};

// Worker → Master: 任务失败
struct TaskFailedMessage {
    MessageHeader header;
    uint64_t task_id = 0;
    uint64_t worker_id = 0;
    bool recoverable = false;
    CMString error_message;
    
    static constexpr MessageType msg_type = MessageType::TASK_FAILED;
    
    FLY_SERIALIZE(header, task_id, worker_id, recoverable, error_message);
};

// Master → Worker: 数据就绪通知
struct DataReadyMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString data_path;
    uint64_t offset = 0;
    int64_t size = 0;
    
    static constexpr MessageType msg_type = MessageType::DATA_READY;
    
    FLY_SERIALIZE(header, worker_id, data_path, offset, size);
};

// Master/Worker: 数据位置查询
struct DataQueryMessage {
    MessageHeader header;
    CMString object_name;
    
    static constexpr MessageType msg_type = MessageType::DATA_QUERY;
    
    FLY_SERIALIZE(header, object_name);
};

// Master → Worker: 数据位置响应
struct DataLocationMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString file_path;
    CMString object_name;
    
    static constexpr MessageType msg_type = MessageType::DATA_LOCATION;
    
    FLY_SERIALIZE(header, worker_id, file_path, object_name);
};

// Master → Worker: 关机
struct ShutdownMessage {
    MessageHeader header;
    
    static constexpr MessageType msg_type = MessageType::SHUTDOWN;
    
    FLY_SERIALIZE(header);
};

}  // namespace fly