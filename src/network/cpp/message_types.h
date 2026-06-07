#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

namespace fly {

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
    DB_PATH_REQUEST = 19,
    DB_PATH_RESPONSE = 20,
    WRITE_REGISTER = 21,
    WRITE_REGISTER_ACK = 22,
    WORKER_PROPERTY_UPDATE = 23,
    OBJECT_REMOVED = 24,
    IDX_LOAD_COMMAND = 25,
    IDX_LOAD_ACK = 26,
    REMOVE_REQUEST = 27,
    REMOVE_ACK = 28,
    REMOVE_COMMAND = 29,
    BACKUP_REQUEST = 30,
    BACKUP_ASSIGN = 31,
    BACKUP_COMPLETE = 32,
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
    CMString hostname;
    CMString ip_address;
    CMVector<CMString> attributes;
    CMString data_server_host;
    int32_t data_server_port = 0;
    
    static constexpr MessageType msg_type = MessageType::REGISTER;
    
    FLY_SERIALIZE(header, worker_id, hostname, ip_address, attributes, data_server_host, data_server_port);
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
    bool success = false;
    CMString error_message;
    CMString compressed_data;
    CMString py_name;
    CMString write_context_hash;
    
    static constexpr MessageType msg_type = MessageType::DATA_RESPONSE;
    
    FLY_SERIALIZE(header, object_name, success, error_message, compressed_data, py_name, write_context_hash);
};

// Master → Worker: 任务分配
struct TaskAssignMessage {
    MessageHeader header;
    uint64_t task_id = 0;
    CMString task_name;
    CMString task_module;
    CMVector<CMString> args;
    CMString write_context_hash;
    
    static constexpr MessageType msg_type = MessageType::TASK_ASSIGN;
    
    FLY_SERIALIZE(header, task_id, task_name, task_module, args, write_context_hash);
};

// Worker → Master: 任务完成
struct TaskCompleteMessage {
    MessageHeader header;
    uint64_t task_id = 0;
    uint64_t worker_id = 0;
    CMVector<CMString> written_objects;
    CMVector<CMString> frozen_dbs;
    
    static constexpr MessageType msg_type = MessageType::TASK_COMPLETE;
    
    FLY_SERIALIZE(header, task_id, worker_id, written_objects, frozen_dbs);
};

// Worker → Master: 任务失败
struct TaskFailedMessage {
    MessageHeader header;
    uint64_t task_id = 0;
    uint64_t worker_id = 0;
    bool recoverable = false;
    CMString error_message;
    TaskErrorType error_type = TaskErrorType::UNKNOWN;

    static constexpr MessageType msg_type = MessageType::TASK_FAILED;

    FLY_SERIALIZE(header, task_id, worker_id, recoverable, error_message, error_type);
};

// Worker → Master: 数据就绪通知（write_object 时实时发送）
struct DataReadyMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString object_name;   // 完整标识符: "db_id:obj_name"
    CMString db_id;         // 所属 Database
    CMString writer_id;     // Writer's writer_id (from Database instance)
    
    static constexpr MessageType msg_type = MessageType::DATA_READY;
    
    FLY_SERIALIZE(header, worker_id, object_name, db_id, writer_id);
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
    CMString data_host;
    int32_t data_port = 0;
    bool success = false;
    bool can_still_produce = false;  // true if pending/running tasks might produce this object
    
    static constexpr MessageType msg_type = MessageType::DATA_LOCATION;
    
    FLY_SERIALIZE(header, worker_id, file_path, object_name, data_host, data_port, success, can_still_produce);
};

// Worker → Master: 任务提交
struct TaskSubmitMessage {
    MessageHeader header;
    CMString task_name;
    CMString task_module;
    CMVector<CMString> args;
    CMVector<CMString> inputs;
    CMVector<CMString> required_capabilities;
    CMString write_context_hash;
    static constexpr MessageType msg_type = MessageType::TASK_SUBMIT;
    FLY_SERIALIZE(header, task_name, task_module, args, inputs, required_capabilities, write_context_hash);
};

// Worker → Master: 数据库路径查询
struct DbPathRequestMessage {
    MessageHeader header;
    CMString db_id;
    static constexpr MessageType msg_type = MessageType::DB_PATH_REQUEST;
    FLY_SERIALIZE(header, db_id);
};

// Master → Worker: 数据库路径响应
struct DbPathResponseMessage {
    MessageHeader header;
    CMString db_id;
    CMString base_path;
    CMString data_path;
    bool success = false;
    static constexpr MessageType msg_type = MessageType::DB_PATH_RESPONSE;
    FLY_SERIALIZE(header, db_id, base_path, data_path, success);
};

// Master → Worker: 关机
struct ShutdownMessage {
    MessageHeader header;
    
    static constexpr MessageType msg_type = MessageType::SHUTDOWN;
    
    FLY_SERIALIZE(header);
};

struct WriteRegisterMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString object_name;
    CMString db_id;
    CMString write_context_hash;
    
    static constexpr MessageType msg_type = MessageType::WRITE_REGISTER;
    
    FLY_SERIALIZE(header, worker_id, object_name, db_id, write_context_hash);
};

struct WriteRegisterAckMessage {
    MessageHeader header;
    CMString object_name;
    CMString db_id;
    bool success = false;
    CMString error_message;
    TaskErrorType error_type = TaskErrorType::UNKNOWN;

    static constexpr MessageType msg_type = MessageType::WRITE_REGISTER_ACK;

    FLY_SERIALIZE(header, object_name, db_id, success, error_message, error_type);
};

// Worker → Master: 属性动态更新
struct WorkerPropertyUpdateMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMVector<CMString> added_properties;
    CMVector<CMString> removed_properties;

    static constexpr MessageType msg_type = MessageType::WORKER_PROPERTY_UPDATE;

    FLY_SERIALIZE(header, worker_id, added_properties, removed_properties);
};

// Master → Worker: 对象删除通知（广播）
struct ObjectRemovedMessage {
    MessageHeader header;
    CMString object_name;   // 完整标识符: "db_id:obj_name"
    CMString db_id;

    static constexpr MessageType msg_type = MessageType::OBJECT_REMOVED;

    FLY_SERIALIZE(header, object_name, db_id);
};

// Master → Worker: load_db idx 加载命令
struct IdxLoadCommandMessage {
    MessageHeader header;
    CMString db_id;
    CMString base_path;
    CMVector<CMString> writer_ids;

    static constexpr MessageType msg_type = MessageType::IDX_LOAD_COMMAND;

    FLY_SERIALIZE(header, db_id, base_path, writer_ids);
};

// Worker → Master: idx 加载完成确认
struct IdxLoadAckMessage {
    MessageHeader header;
    uint64_t worker_id = 0;
    CMString db_id;
    bool success = false;
    int32_t loaded_count = 0;
    CMString error_message;
    CMVector<CMString> loaded_writer_ids;

    static constexpr MessageType msg_type = MessageType::IDX_LOAD_ACK;

    FLY_SERIALIZE(header, worker_id, db_id, success, loaded_count, error_message, loaded_writer_ids);
};

// Master → Worker: database frozen notification (broadcast)
struct DatabaseFreezeNotification {
    MessageHeader header;
    CMString db_id;

    static constexpr MessageType msg_type = MessageType::DATABASE_FREEZE;

    FLY_SERIALIZE(header, db_id);
};

// Worker → Master: request to remove object (blocking until ack)
struct RemoveRequestMessage {
    MessageHeader header;
    CMString db_id;
    CMString object_name;

    static constexpr MessageType msg_type = MessageType::REMOVE_REQUEST;

    FLY_SERIALIZE(header, db_id, object_name);
};

// Master → Worker (origin): ack the remove request
struct RemoveAckMessage {
    MessageHeader header;
    CMString db_id;
    CMString object_name;
    bool success = false;

    static constexpr MessageType msg_type = MessageType::REMOVE_ACK;

    FLY_SERIALIZE(header, db_id, object_name, success);
};

// Master → Worker (data holder): command to remove object
struct RemoveCommandMessage {
    MessageHeader header;
    CMString db_id;
    CMString object_name;

    static constexpr MessageType msg_type = MessageType::REMOVE_COMMAND;

    FLY_SERIALIZE(header, db_id, object_name);
};

// Worker → Master: request backup of an object
struct BackupRequestMessage {
    MessageHeader header;
    uint64_t worker_id = 0;      // Source worker (has the data)
    CMString object_name;         // Full name: "db_id:obj_name"
    CMString db_id;

    static constexpr MessageType msg_type = MessageType::BACKUP_REQUEST;
    FLY_SERIALIZE(header, worker_id, object_name, db_id);
};

// Master → Worker: assign backup job
struct BackupAssignMessage {
    MessageHeader header;
    CMString object_name;         // Full name
    CMString db_id;
    CMString source_host;         // Where to fetch data from
    int32_t source_port = 0;
    uint64_t source_worker_id = 0;

    static constexpr MessageType msg_type = MessageType::BACKUP_ASSIGN;
    FLY_SERIALIZE(header, object_name, db_id, source_host, source_port, source_worker_id);
};

// Worker → Master: backup complete notification
struct BackupCompleteMessage {
    MessageHeader header;
    uint64_t worker_id = 0;      // Backup worker (or reading worker for read backup)
    CMString object_name;         // Full name
    CMString db_id;
    bool success = false;
    CMString error_message;

    static constexpr MessageType msg_type = MessageType::BACKUP_COMPLETE;
    FLY_SERIALIZE(header, worker_id, object_name, db_id, success, error_message);
};

}  // namespace fly