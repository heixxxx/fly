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
    HEARTBEAT_ACK = 33,
    VAR_SET = 34,        // worker → master: set var (synchronous, awaits VAR_ACK)
    VAR_GET = 35,        // worker → master: get var (synchronous, awaits VAR_ACK)
    VAR_ACK = 36,        // master → worker: unified set/get response
    VAR_REMOVE = 37,     // worker → master: remove var (async, no ack)
    VAR_BROADCAST = 38,  // master → worker: broadcast removal / modification-reject (async)
    DATABASE_FREEZE_ACK = 39,  // master → worker: freeze ack (success / DB_ALREADY_FROZEN)
};

inline bool is_valid_message_type(uint8_t raw) {
    return raw >= 1 && raw <= 39;
}

struct MessageHeader {
    MessageType type_ = MessageType::REGISTER;
    uint32_t message_id_ = 0;
    uint64_t timestamp_ = 0;

    FLY_SERIALIZE(type_, message_id_, timestamp_);
};

struct RegisterMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString hostname_;
    CMString ip_address_;
    CMVector<CMString> attributes_;
    CMString data_server_host_;
    int32_t data_server_port_ = 0;

    static constexpr MessageType msg_type_ = MessageType::REGISTER;

    FLY_SERIALIZE(header_, worker_id_, hostname_, ip_address_, attributes_, data_server_host_, data_server_port_);
};

struct RegisterAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString master_address_;
    int32_t master_port_ = 0;

    static constexpr MessageType msg_type_ = MessageType::REGISTER_ACK;

    FLY_SERIALIZE(header_, worker_id_, master_address_, master_port_);
};

struct HeartbeatMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMVector<uint64_t> running_tasks_;
    CMVector<CMString> attributes_;

    static constexpr MessageType msg_type_ = MessageType::HEARTBEAT;

    FLY_SERIALIZE(header_, worker_id_, running_tasks_, attributes_);
};

struct HeartbeatAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;

    static constexpr MessageType msg_type_ = MessageType::HEARTBEAT_ACK;

    FLY_SERIALIZE(header_, worker_id_);
};

struct DataRequestMessage {
    MessageHeader header_;
    CMString object_name_;
    uint64_t requesting_worker_id_ = 0;
    uint64_t request_id_ = 0;

    static constexpr MessageType msg_type_ = MessageType::DATA_REQUEST;

    FLY_SERIALIZE(header_, object_name_, requesting_worker_id_, request_id_);
};

// DataResponseMessage carries small fields via bitsery; the large compressed_data
// payload is transmitted as a separate raw segment after the encoded message
// (see DataResponseProtocol). This avoids bitsery serializing the large blob.
struct DataResponseMessage {
    MessageHeader header_;
    CMString object_name_;
    bool success_ = false;
    CMString error_message_;
    CMString py_name_;
    CMString write_context_hash_;

    static constexpr MessageType msg_type_ = MessageType::DATA_RESPONSE;

    FLY_SERIALIZE(header_, object_name_, success_, error_message_, py_name_, write_context_hash_);
};

struct DataLocation {
    CMString object_name;
    uint64_t worker_id = 0;
    CMString host;
    int32_t port = 0;

    FLY_SERIALIZE(object_name, worker_id, host, port);
};

// A var name/value pair inlined into TaskAssignMessage, so workers receive declared
// vars in one shot (no extra round-trip to master). value is serialized bytes
// (pickle for Python objects, FLY_ENCODE_TO_BUFFER for C++ exported objects);
// type_name carries the Python type name for deserialization dispatch.
struct VarPayload {
    CMString var_name;
    CMString value;
    CMString type_name;

    FLY_SERIALIZE(var_name, value, type_name);
};

struct TaskAssignMessage {
    MessageHeader header_;
    uint64_t task_id_ = 0;
    CMString task_name_;
    CMString task_module_;
    CMVector<CMString> args_;
    CMString write_context_hash_;
    CMVector<DataLocation> dependency_locations_;  // Pre-fetched locations of input data.
    CMVector<VarPayload> var_payloads_;            // Pre-fetched declared vars.

    static constexpr MessageType msg_type_ = MessageType::TASK_ASSIGN;

    FLY_SERIALIZE(header_, task_id_, task_name_, task_module_, args_, write_context_hash_, dependency_locations_, var_payloads_);
};

// task 成功写出的对象条目：对象全名 + 压缩后字节数。
// size_bytes_ 用于 master 的 data locality 调度亲和度打分（数据传输成本）。
struct WrittenObject {
    CMString object_name_;
    int64_t size_bytes_ = 0;

    FLY_SERIALIZE(object_name_, size_bytes_);
};

struct TaskCompleteMessage {
    MessageHeader header_;
    uint64_t task_id_ = 0;
    uint64_t worker_id_ = 0;
    CMVector<WrittenObject> written_objects_;
    CMVector<CMString> frozen_dbs_;
    bool is_internal_ = false;  // backup 等内部任务，不参与依赖图和 metadata

    static constexpr MessageType msg_type_ = MessageType::TASK_COMPLETE;

    FLY_SERIALIZE(header_, task_id_, worker_id_, written_objects_, frozen_dbs_, is_internal_);
};

struct TaskFailedMessage {
    MessageHeader header_;
    uint64_t task_id_ = 0;
    uint64_t worker_id_ = 0;
    bool recoverable_ = false;
    CMString error_message_;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;
    // 失败 task 已写出的脏对象列表（db_id:short_name 全名）。
    // worker 在 task 失败后已本地撤销（idx ABORT + data truncate），master
    // 据此清理 remote_idx / provenance / graph，并广播 OBJECT_REMOVED 通知其他
    // worker 清缓存。
    CMVector<CMString> dirty_objects_;

    static constexpr MessageType msg_type_ = MessageType::TASK_FAILED;

    FLY_SERIALIZE(header_, task_id_, worker_id_, recoverable_, error_message_, error_type_, dirty_objects_);
};

struct DataQueryMessage {
    MessageHeader header_;
    CMString object_name_;

    static constexpr MessageType msg_type_ = MessageType::DATA_QUERY;

    FLY_SERIALIZE(header_, object_name_);
};

struct DataLocationMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString file_path_;
    CMString object_name_;
    CMString data_host_;
    int32_t data_port_ = 0;
    bool success_ = false;
    bool can_still_produce_ = false;

    static constexpr MessageType msg_type_ = MessageType::DATA_LOCATION;

    FLY_SERIALIZE(header_, worker_id_, file_path_, object_name_, data_host_, data_port_, success_, can_still_produce_);
};

struct TaskSubmitMessage {
    MessageHeader header_;
    CMString task_name_;
    CMString task_module_;
    CMVector<CMString> args_;
    CMVector<CMString> inputs_;
    CMVector<CMString> required_capabilities_;
    float attribute_timeout_ = -1.0f;  // <0=死等, 0=立即降级, >0=限时降级
    CMString write_context_hash_;
    CMVector<CMString> vars_;          // declared var names for inline delivery
    static constexpr MessageType msg_type_ = MessageType::TASK_SUBMIT;
    FLY_SERIALIZE(header_, task_name_, task_module_, args_, inputs_, required_capabilities_, attribute_timeout_, write_context_hash_, vars_);
};

struct DbPathRequestMessage {
    MessageHeader header_;
    CMString db_id_;
    static constexpr MessageType msg_type_ = MessageType::DB_PATH_REQUEST;
    FLY_SERIALIZE(header_, db_id_);
};

struct DbPathResponseMessage {
    MessageHeader header_;
    CMString db_id_;
    CMString base_path_;
    CMString data_path_;
    bool success_ = false;
    static constexpr MessageType msg_type_ = MessageType::DB_PATH_RESPONSE;
    FLY_SERIALIZE(header_, db_id_, base_path_, data_path_, success_);
};

struct ShutdownMessage {
    MessageHeader header_;

    static constexpr MessageType msg_type_ = MessageType::SHUTDOWN;

    FLY_SERIALIZE(header_);
};

struct WriteRegisterMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString object_name_;
    CMString db_id_;
    CMString write_context_hash_;
    CMString writer_id_;        // writer 进程标识（db meta recorded_workers_ 登记用）
    int64_t size_bytes_ = 0;    // 压缩后字节数（数据 locality 调度亲和度打分用）

    static constexpr MessageType msg_type_ = MessageType::WRITE_REGISTER;

    FLY_SERIALIZE(header_, worker_id_, object_name_, db_id_, write_context_hash_, writer_id_, size_bytes_);
};

struct WriteRegisterAckMessage {
    MessageHeader header_;
    CMString object_name_;
    CMString db_id_;
    bool success_ = false;
    CMString error_message_;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;

    static constexpr MessageType msg_type_ = MessageType::WRITE_REGISTER_ACK;

    FLY_SERIALIZE(header_, object_name_, db_id_, success_, error_message_, error_type_);
};

struct WorkerPropertyUpdateMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMVector<CMString> added_properties_;
    CMVector<CMString> removed_properties_;

    static constexpr MessageType msg_type_ = MessageType::WORKER_PROPERTY_UPDATE;

    FLY_SERIALIZE(header_, worker_id_, added_properties_, removed_properties_);
};

struct ObjectRemovedMessage {
    MessageHeader header_;
    CMString object_name_;
    CMString db_id_;

    static constexpr MessageType msg_type_ = MessageType::OBJECT_REMOVED;

    FLY_SERIALIZE(header_, object_name_, db_id_);
};

struct IdxLoadCommandMessage {
    MessageHeader header_;
    CMString db_id_;
    CMString base_path_;
    CMVector<CMString> writer_ids_;

    static constexpr MessageType msg_type_ = MessageType::IDX_LOAD_COMMAND;

    FLY_SERIALIZE(header_, db_id_, base_path_, writer_ids_);
};

struct IdxLoadAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString db_id_;
    bool success_ = false;
    int32_t loaded_count_ = 0;
    CMString error_message_;
    CMVector<CMString> loaded_writer_ids_;

    static constexpr MessageType msg_type_ = MessageType::IDX_LOAD_ACK;

    FLY_SERIALIZE(header_, worker_id_, db_id_, success_, loaded_count_, error_message_, loaded_writer_ids_);
};

struct DatabaseFreezeNotification {
    MessageHeader header_;
    CMString db_id_;
    uint64_t task_id_ = 0;   // 非 stream 模式下 master 登记 pending frozen 需要（worker 在 begin_task 期间填充）

    static constexpr MessageType msg_type_ = MessageType::DATABASE_FREEZE;

    FLY_SERIALIZE(header_, db_id_, task_id_);
};

// master → worker: freeze 请求的同步 ack。worker 据此判断 freeze 是否被接受。
// 失败（success_=false）通常为 DB_ALREADY_FROZEN（该 db 已被本 task 或其他 task freeze）。
struct DatabaseFreezeAckMessage {
    MessageHeader header_;
    CMString db_id_;
    bool success_ = true;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;

    static constexpr MessageType msg_type_ = MessageType::DATABASE_FREEZE_ACK;

    FLY_SERIALIZE(header_, db_id_, success_, error_type_);
};

struct RemoveRequestMessage {
    MessageHeader header_;
    CMString db_id_;
    CMString object_name_;

    static constexpr MessageType msg_type_ = MessageType::REMOVE_REQUEST;

    FLY_SERIALIZE(header_, db_id_, object_name_);
};

struct RemoveAckMessage {
    MessageHeader header_;
    CMString db_id_;
    CMString object_name_;
    bool success_ = false;

    static constexpr MessageType msg_type_ = MessageType::REMOVE_ACK;

    FLY_SERIALIZE(header_, db_id_, object_name_, success_);
};

struct RemoveCommandMessage {
    MessageHeader header_;
    CMString db_id_;
    CMString object_name_;

    static constexpr MessageType msg_type_ = MessageType::REMOVE_COMMAND;

    FLY_SERIALIZE(header_, db_id_, object_name_);
};

struct BackupRequestMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString object_name_;
    CMString db_id_;

    static constexpr MessageType msg_type_ = MessageType::BACKUP_REQUEST;
    FLY_SERIALIZE(header_, worker_id_, object_name_, db_id_);
};

struct BackupAssignMessage {
    MessageHeader header_;
    CMString object_name_;
    CMString db_id_;
    CMString source_host_;
    int32_t source_port_ = 0;
    uint64_t source_worker_id_ = 0;

    static constexpr MessageType msg_type_ = MessageType::BACKUP_ASSIGN;
    FLY_SERIALIZE(header_, object_name_, db_id_, source_host_, source_port_, source_worker_id_);
};

struct BackupCompleteMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString object_name_;
    CMString db_id_;
    bool success_ = false;
    CMString error_message_;

    static constexpr MessageType msg_type_ = MessageType::BACKUP_COMPLETE;
    FLY_SERIALIZE(header_, worker_id_, object_name_, db_id_, success_, error_message_);
};

// =============================================================================
// Var service messages — lightweight small-object KV bypassing write_object's
// compression / cache / WriteBackQueue / dependency-graph machinery.
// value_ carries already-serialized bytes (pickle or FLY_ENCODE_TO_BUFFER).
// =============================================================================

// worker → master: set var (synchronous, awaits VAR_ACK).
// var_name_ is the FULL name (db_id:short_name); db_id is split on the master.
struct VarSetMessage {
    MessageHeader header_;
    CMString var_name_;
    // mutable: the reactor hands msg as const T&, but the decoded msg is a
    // local that is destroyed right after the handler returns. Marking value_
    // mutable lets the handler std::move it into a FlyBuffer (zero-copy) without
    // changing the reactor's const-handler contract.
    mutable CMString value_;
    CMString type_name_;

    static constexpr MessageType msg_type_ = MessageType::VAR_SET;
    FLY_SERIALIZE(header_, var_name_, value_, type_name_);
};

// worker → master: get var (synchronous, awaits VAR_ACK).
// var_name_ is the FULL name (db_id:short_name).
struct VarGetMessage {
    MessageHeader header_;
    CMString var_name_;

    static constexpr MessageType msg_type_ = MessageType::VAR_GET;
    FLY_SERIALIZE(header_, var_name_);
};

// master → worker: unified response for VAR_SET / VAR_GET.
// var_name_ is the FULL name (db_id:short_name).
// On VAR_GET: success_=false means the var does not exist.
// On VAR_SET: success_=false means rejected (frozen or immutable).
struct VarAckMessage {
    MessageHeader header_;
    CMString var_name_;
    bool success_ = false;
    // mutable: same rationale as VarSetMessage.value_ — allows the worker
    // handler to std::move the value bytes into a FlyBuffer (zero-copy).
    mutable CMString value_;
    CMString type_name_;
    CMString error_message_;

    static constexpr MessageType msg_type_ = MessageType::VAR_ACK;
    FLY_SERIALIZE(header_, var_name_, success_, value_, type_name_, error_message_);
};

// worker → master: remove var (async, no ack expected).
// var_name_ is the FULL name (db_id:short_name). Empty means clear all.
struct VarRemoveMessage {
    MessageHeader header_;
    CMString var_name_;

    static constexpr MessageType msg_type_ = MessageType::VAR_REMOVE;
    FLY_SERIALIZE(header_, var_name_);
};

// master → worker: broadcast a var removal or modification-reject (async).
// var_name_ is the FULL name (db_id:short_name). Empty means clear all.
struct VarBroadcastMessage {
    MessageHeader header_;
    CMString var_name_;
    bool is_modification_reject_ = false;

    static constexpr MessageType msg_type_ = MessageType::VAR_BROADCAST;
    FLY_SERIALIZE(header_, var_name_, is_modification_reject_);
};

}  // namespace fly
