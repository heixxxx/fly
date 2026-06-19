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
    HEARTBEAT_ACK = 33,
};

inline bool is_valid_message_type(uint8_t raw) {
    return raw >= 1 && raw <= 33;
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

struct TaskAssignMessage {
    MessageHeader header_;
    uint64_t task_id_ = 0;
    CMString task_name_;
    CMString task_module_;
    CMVector<CMString> args_;
    CMString write_context_hash_;
    CMVector<DataLocation> dependency_locations_;  // Pre-fetched locations of input data.

    static constexpr MessageType msg_type_ = MessageType::TASK_ASSIGN;

    FLY_SERIALIZE(header_, task_id_, task_name_, task_module_, args_, write_context_hash_, dependency_locations_);
};

struct TaskCompleteMessage {
    MessageHeader header_;
    uint64_t task_id_ = 0;
    uint64_t worker_id_ = 0;
    CMVector<CMString> written_objects_;
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

    static constexpr MessageType msg_type_ = MessageType::TASK_FAILED;

    FLY_SERIALIZE(header_, task_id_, worker_id_, recoverable_, error_message_, error_type_);
};

struct DataReadyMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString object_name_;
    CMString db_id_;
    CMString writer_id_;

    static constexpr MessageType msg_type_ = MessageType::DATA_READY;

    FLY_SERIALIZE(header_, worker_id_, object_name_, db_id_, writer_id_);
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
    CMString write_context_hash_;
    static constexpr MessageType msg_type_ = MessageType::TASK_SUBMIT;
    FLY_SERIALIZE(header_, task_name_, task_module_, args_, inputs_, required_capabilities_, write_context_hash_);
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

    static constexpr MessageType msg_type_ = MessageType::WRITE_REGISTER;

    FLY_SERIALIZE(header_, worker_id_, object_name_, db_id_, write_context_hash_);
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

    static constexpr MessageType msg_type_ = MessageType::DATABASE_FREEZE;

    FLY_SERIALIZE(header_, db_id_);
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

}  // namespace fly
