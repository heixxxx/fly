#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <log/cpp/logger.h>  // LogLevel（LogMessage.level_ 用）
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

namespace fly {

enum class MessageType : uint8_t {
    INVALID = 0,      // 哨兵：解析失败 / 未初始化。不参与序列化，不注册 handler。
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
    NET_PROBE_REQUEST = 40,   // worker → peer (data plane): measure RTT/bandwidth
    NET_PROBE_RESPONSE = 41,  // peer → worker: echoes back a payload of requested size
    DELETE_DATA = 42,         // master → worker: 删除本地 data_path 下指定 writer 的 .dat（merge 后清理源）
    DELETE_DATA_ACK = 43,     // worker → master: 删除结果回报
    MERGE_CLEANUP = 44,       // master → worker (broadcast): merge 完成后清理 local_idx/remote_idx 残留
    MERGE_CLEANUP_ACK = 45,   // worker → master: cleanup 完成回报（merge_db 返回前的全局一致性屏障）
    LOG_MESSAGE = 46,         // worker → master: 高价值日志推送（async, no ack）
    MSG_COUNT_REQUEST = 47,   // master → worker (broadcast): 请求上报 message 触发次数（summary 屏障）
    MSG_COUNT_REPORT = 48,    // worker → master: 上报本地 message 触发次数（id/domain 两套计数）
    MSG_LIMIT_SYNC = 49,      // master → worker (broadcast): 同步 message 配额设置（全量快照）
    PEER_RPC_REQUEST = 50,    // worker → peer (业务RPC): 请求（rpc_id + src_worker + payload）
    PEER_RPC_RESPONSE = 51,   // peer → worker (业务RPC): 响应（rpc_id + status + payload）
    WORKER_BACKUP_SUGGEST = 52,  // worker → master: 上报 TIER2 读流量增量，master 聚合后判定 backup
    STORAGE_SPAWN_REQUEST = 53,  // master → worker: 请求在本 host 唤起 storage_only worker（自动补齐 feature）
    STORAGE_SPAWN_ACK = 54,      // worker → master: spawn 动作结果（exec 成败 + 原因；注册到达另计）
    WORKER_PROBE = 55,           // master → worker: 疑似重复注册时探测既有连接活性（回 PROBE_ACK）
    WORKER_PROBE_ACK = 56,       // worker → master: 探测应答（到达即证明该实例活着）
};

inline bool is_valid_message_type(uint8_t raw) {
    return raw >= 1 && raw <= 56;
}

struct MessageHeader {
    MessageType type_ = MessageType::INVALID;
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
    // worker role（静态身份，注册时设定不可变更）：0=hybrid（默认），1=storage_only。
    // 独立于 attributes（可变、参与调度匹配）。
    uint8_t role_ = 0;

    static constexpr MessageType msg_type_ = MessageType::REGISTER;

    FLY_SERIALIZE(header_, worker_id_, hostname_, ip_address_, attributes_, data_server_host_, data_server_port_, role_);
};

struct RegisterAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString master_address_;
    int32_t master_port_ = 0;
    // 重复注册拒绝（用户确认语义）：该 worker_id 已有活跃连接（网络分区恢复
    // 与手动重启的竞态，先到先得），后到者收到 true 后应自行干净退出。
    bool duplicate_ = false;

    static constexpr MessageType msg_type_ = MessageType::REGISTER_ACK;

    FLY_SERIALIZE(header_, worker_id_, master_address_, master_port_, duplicate_);
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

// Response status for data read operations. Replaces the old magic-string
// convention where error_message_ carried "DATA_NOT_READY"/"OBJECT_NOT_FOUND".
enum class ResponseStatus : uint8_t {
    SUCCESS   = 0,
    NOT_READY = 1,   // 写入进行中，client 应轮询重试
    NOT_FOUND = 2,   // 对象不存在
    ERROR     = 3,   // 其他错误
};

// DataResponseMessage carries small fields via bitsery; the large compressed_data
// payload is transmitted as a separate raw segment after the encoded message
// (see DataResponseProtocol). This avoids bitsery serializing the large blob.
struct DataResponseMessage {
    MessageHeader header_;
    CMString object_name_;
    bool success_ = false;
    ResponseStatus status_ = ResponseStatus::SUCCESS;  // 协议状态码（取代 error_message_ 魔法字符串）
    CMString error_message_;                           // 自由文本诊断（仅给人看，不承载状态码）
    CMString py_name_;
    CMString write_context_hash_;

    static constexpr MessageType msg_type_ = MessageType::DATA_RESPONSE;

    FLY_SERIALIZE(header_, object_name_, success_, status_, error_message_, py_name_, write_context_hash_);
};

// Bandwidth probe (data plane). Request asks the peer to echo a payload of
// payload_size_ bytes; response carries payload_ so the caller measures the
// round-trip throughput of a real data-plane exchange.
struct NetProbeRequestMessage {
    MessageHeader header_;
    uint32_t payload_size_ = 0;  // bytes the peer should echo back
    uint64_t probe_seq_ = 0;

    static constexpr MessageType msg_type_ = MessageType::NET_PROBE_REQUEST;

    FLY_SERIALIZE(header_, payload_size_, probe_seq_);
};

struct NetProbeResponseMessage {
    MessageHeader header_;
    uint64_t probe_seq_ = 0;
    CMVector<uint8_t> payload_;

    static constexpr MessageType msg_type_ = MessageType::NET_PROBE_RESPONSE;

    FLY_SERIALIZE(header_, probe_seq_, payload_);
};

struct DataLocation {
    CMString object_name;
    uint64_t worker_id = 0;
    CMString host;
    int32_t port = 0;
    // 1 = 目标 worker 为 storage_only（master registry 权威填充；读方回填本地
    // registry 供 TIER2 排序 storage 优先）。缺省 0 与旧语义一致。
    uint8_t storage_only = 0;

    FLY_SERIALIZE(object_name, worker_id, host, port, storage_only);
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
    CMString object_name_;   // full name: "db_path:short_name"（db_path 变长，split 用 rfind(':')）
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
    // 失败 task 已写出的脏对象列表（db_path:short_name 全名）。
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

// Master → worker response to DataQueryMessage. Carries ALL replica locations
// of the requested object (not just one), so the worker can populate its local
// remote_idx and let TIER2 iterate every replica. locations_ is authoritative
// when success_ is true (may be empty if the object exists nowhere yet).
struct DataLocationMessage {
    MessageHeader header_;
    CMString file_path_;
    CMString object_name_;
    CMVector<DataLocation> locations_;
    bool success_ = false;
    bool can_still_produce_ = false;

    static constexpr MessageType msg_type_ = MessageType::DATA_LOCATION;

    FLY_SERIALIZE(header_, file_path_, object_name_, locations_, success_, can_still_produce_);
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
    int priority_ = 10;               // 任务优先级（worker→master 透传，递归提交场景）
    static constexpr MessageType msg_type_ = MessageType::TASK_SUBMIT;
    FLY_SERIALIZE(header_, task_name_, task_module_, args_, inputs_, required_capabilities_, attribute_timeout_, write_context_hash_, vars_, priority_);
};

struct DbPathRequestMessage {
    MessageHeader header_;
    CMString db_path_;
    static constexpr MessageType msg_type_ = MessageType::DB_PATH_REQUEST;
    FLY_SERIALIZE(header_, db_path_);
};

struct DbPathResponseMessage {
    MessageHeader header_;
    CMString db_path_;
    CMString data_path_;
    bool success_ = false;
    static constexpr MessageType msg_type_ = MessageType::DB_PATH_RESPONSE;
    FLY_SERIALIZE(header_, db_path_, data_path_, success_);
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
    CMString db_path_;
    CMString write_context_hash_;
    CMString writer_id_;        // writer 进程标识（db meta recorded_workers_ 登记用）
    int64_t size_bytes_ = 0;    // 压缩后字节数（数据 locality 调度亲和度打分用）

    static constexpr MessageType msg_type_ = MessageType::WRITE_REGISTER;

    FLY_SERIALIZE(header_, worker_id_, object_name_, db_path_, write_context_hash_, writer_id_, size_bytes_);
};

struct WriteRegisterAckMessage {
    MessageHeader header_;
    CMString object_name_;
    CMString db_path_;
    bool success_ = false;
    CMString error_message_;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;

    static constexpr MessageType msg_type_ = MessageType::WRITE_REGISTER_ACK;

    FLY_SERIALIZE(header_, object_name_, db_path_, success_, error_message_, error_type_);
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
    CMString db_path_;

    static constexpr MessageType msg_type_ = MessageType::OBJECT_REMOVED;

    FLY_SERIALIZE(header_, object_name_, db_path_);
};

struct IdxLoadCommandMessage {
    MessageHeader header_;
    CMString db_path_;
    CMVector<CMString> writer_ids_;

    static constexpr MessageType msg_type_ = MessageType::IDX_LOAD_COMMAND;

    FLY_SERIALIZE(header_, db_path_, writer_ids_);
};

struct IdxLoadAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString db_path_;
    bool success_ = false;
    int32_t loaded_count_ = 0;
    CMString error_message_;
    CMVector<CMString> loaded_writer_ids_;

    static constexpr MessageType msg_type_ = MessageType::IDX_LOAD_ACK;

    FLY_SERIALIZE(header_, worker_id_, db_path_, success_, loaded_count_, error_message_, loaded_writer_ids_);
};

// master → worker：请求在本 host 唤起一个 storage_only worker（自动补齐
// feature）。spawn 的 worker_id 由 master 分配（高基区自增，避开 Python
// launch 的低位 id 序列）；其余参数（master 地址/hostname/config 路径）由
// worker 侧从自身 ProcessInfo/Config 推导，保证同环境同版本。
struct StorageSpawnRequestMessage {
    MessageHeader header_;
    uint64_t spawn_worker_id_ = 0;  // 分配给将 spawn 的 storage worker 的 id

    static constexpr MessageType msg_type_ = MessageType::STORAGE_SPAWN_REQUEST;

    FLY_SERIALIZE(header_, spawn_worker_id_);
};

// worker → master：spawn 动作本身的结果。exec 成功不等于注册到达（注册走
// 正常 RegisterMessage 流程，由检测循环按「该 host 出现 storage」确认）；
// 失败（二进制不可执行/fork 失败等）携带原因供 master 退避。
struct StorageSpawnAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;      // 执行 spawn 的 worker（master 记退避计数）
    CMString hostname_;           // 目标 host（master 按 host 记占位/退避）
    bool success_ = false;
    CMString error_message_;

    static constexpr MessageType msg_type_ = MessageType::STORAGE_SPAWN_ACK;

    FLY_SERIALIZE(header_, worker_id_, hostname_, success_, error_message_);
};

// 疑似重复注册的活性探测：master 对既有连接发送，后到的注册在探测结论
// 前不回 ack（worker 注册 10s 超时自然重发）。ACK 到达 = 旧实例活着 →
// 后到者被拒；旧连接断开（EOF 清连接表）→ 重发注册正常接受。
struct WorkerProbeMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;

    static constexpr MessageType msg_type_ = MessageType::WORKER_PROBE;

    FLY_SERIALIZE(header_, worker_id_);
};

struct WorkerProbeAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;

    static constexpr MessageType msg_type_ = MessageType::WORKER_PROBE_ACK;

    FLY_SERIALIZE(header_, worker_id_);
};

struct DatabaseFreezeNotification {
    MessageHeader header_;
    CMString db_path_;
    uint64_t task_id_ = 0;   // 非 stream 模式下 master 登记 pending frozen 需要（worker 在 begin_task 期间填充）

    static constexpr MessageType msg_type_ = MessageType::DATABASE_FREEZE;

    FLY_SERIALIZE(header_, db_path_, task_id_);
};

// master → worker: freeze 请求的同步 ack。worker 据此判断 freeze 是否被接受。
// 失败（success_=false）通常为 DB_ALREADY_FROZEN（该 db 已被本 task 或其他 task freeze）。
struct DatabaseFreezeAckMessage {
    MessageHeader header_;
    CMString db_path_;
    bool success_ = true;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;

    static constexpr MessageType msg_type_ = MessageType::DATABASE_FREEZE_ACK;

    FLY_SERIALIZE(header_, db_path_, success_, error_type_);
};

struct RemoveRequestMessage {
    MessageHeader header_;
    CMString db_path_;
    CMString object_name_;

    static constexpr MessageType msg_type_ = MessageType::REMOVE_REQUEST;

    FLY_SERIALIZE(header_, db_path_, object_name_);
};

struct RemoveAckMessage {
    MessageHeader header_;
    CMString db_path_;
    CMString object_name_;
    bool success_ = false;

    static constexpr MessageType msg_type_ = MessageType::REMOVE_ACK;

    FLY_SERIALIZE(header_, db_path_, object_name_, success_);
};

struct RemoveCommandMessage {
    MessageHeader header_;
    CMString db_path_;
    CMString object_name_;

    static constexpr MessageType msg_type_ = MessageType::REMOVE_COMMAND;

    FLY_SERIALIZE(header_, db_path_, object_name_);
};

struct BackupRequestMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString object_name_;
    CMString db_path_;

    static constexpr MessageType msg_type_ = MessageType::BACKUP_REQUEST;
    FLY_SERIALIZE(header_, worker_id_, object_name_, db_path_);
};

struct BackupAssignMessage {
    MessageHeader header_;
    CMString object_name_;
    CMString db_path_;
    CMString source_host_;
    int32_t source_port_ = 0;
    uint64_t source_worker_id_ = 0;

    static constexpr MessageType msg_type_ = MessageType::BACKUP_ASSIGN;
    FLY_SERIALIZE(header_, object_name_, db_path_, source_host_, source_port_, source_worker_id_);
};

struct BackupCompleteMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString object_name_;
    CMString db_path_;
    bool success_ = false;
    CMString error_message_;

    static constexpr MessageType msg_type_ = MessageType::BACKUP_COMPLETE;
    FLY_SERIALIZE(header_, worker_id_, object_name_, db_path_, success_, error_message_);
};

// worker → master: 上报本 worker 自上次 suggest 以来的 TIER2 读增量（count/bytes）。
// master 用 EWMA 聚合多 worker suggest → score = cumulative/replicas → 判定 backup。
// object_name_ 为 FULL name（db_path:short_name）；size_bytes_ 为最新观测的压缩后大小。
struct WorkerBackupSuggestMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString object_name_;
    uint64_t delta_count_ = 0;     // worker 自上次 suggest 的增量次数
    uint64_t delta_bytes_ = 0;     // worker 自上次 suggest 的增量字节
    int64_t size_bytes_ = 0;       // 对象大小（最新观测，压缩后）

    static constexpr MessageType msg_type_ = MessageType::WORKER_BACKUP_SUGGEST;
    FLY_SERIALIZE(header_, worker_id_, object_name_, delta_count_, delta_bytes_, size_bytes_);
};

// =============================================================================
// Var service messages — lightweight small-object KV bypassing write_object's
// compression / cache / WriteBackQueue / dependency-graph machinery.
// value_ carries already-serialized bytes (pickle or FLY_ENCODE_TO_BUFFER).
// =============================================================================

// worker → master: set var (synchronous, awaits VAR_ACK).
// var_name_ is the FULL name (db_path:short_name); db_path is split on the master.
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
// var_name_ is the FULL name (db_path:short_name).
struct VarGetMessage {
    MessageHeader header_;
    CMString var_name_;

    static constexpr MessageType msg_type_ = MessageType::VAR_GET;
    FLY_SERIALIZE(header_, var_name_);
};

// master → worker: unified response for VAR_SET / VAR_GET.
// var_name_ is the FULL name (db_path:short_name).
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
// var_name_ is the FULL name (db_path:short_name). Empty means clear all.
struct VarRemoveMessage {
    MessageHeader header_;
    CMString var_name_;

    static constexpr MessageType msg_type_ = MessageType::VAR_REMOVE;
    FLY_SERIALIZE(header_, var_name_);
};

// master → worker: broadcast a var removal or modification-reject (async).
// var_name_ is the FULL name (db_path:short_name). Empty means clear all.
struct VarBroadcastMessage {
    MessageHeader header_;
    CMString var_name_;
    bool is_modification_reject_ = false;

    static constexpr MessageType msg_type_ = MessageType::VAR_BROADCAST;
    FLY_SERIALIZE(header_, var_name_, is_modification_reject_);
};

// =============================================================================
// DB Merge — merge_db 主动 API 的源清理消息。
// merge_db 把分散在各源 host 本地 data_path 的 .dat 集中到 master host 后，
// master 向各源 host worker 发 DELETE_DATA 命令删除已迁移的源 .dat。
// 详见 docs/db-merge-design.md §4.1。
// =============================================================================

// master → worker: 命令 worker 删除本地 data_path 下的 .dat 文件（merge 后清理源）。
// data_path_ 显式指定待删目录（源 data_path），worker 不再依赖 db_registry 解析 ——
// 因为 cleanup_after_merge 会把 master 的 db_registry 更新到 merge 路径，若删源在
// cleanup 之后执行，db_registry 解析会拿到错误的（merge 后的）路径。显式传 data_path
// 消除该隐式顺序依赖（详见 docs/issues/006-merge-db-review-findings.md 遗漏点 1）。
struct DeleteDataMessage {
    MessageHeader header_;
    CMString db_path_;
    CMString data_path_;            // 显式指定待删 .dat 所在目录（源 data_path）
    CMVector<CMString> writer_ids_; // 待删 writer 列表

    static constexpr MessageType msg_type_ = MessageType::DELETE_DATA;
    FLY_SERIALIZE(header_, db_path_, data_path_, writer_ids_);
};

// worker → master: 删除结果回报。
struct DeleteDataAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString db_path_;
    bool success_ = false;
    int32_t deleted_count_ = 0;     // 实际删除的 .dat 文件数
    CMString error_message_;
    CMVector<CMString> deleted_writer_ids_;  // 成功删除的 writer 列表

    static constexpr MessageType msg_type_ = MessageType::DELETE_DATA_ACK;
    FLY_SERIALIZE(header_, worker_id_, db_path_, success_, deleted_count_, error_message_, deleted_writer_ids_);
};

// master → worker (broadcast): merge 全部确认成功后，命令各 worker 清理旧 local_idx_[db_path]
// + remote_idx_[db_path]，并按新路径（db_path + data_path）重建 local_idx，使同 host / 共享 FS
// 的 worker 能直接本地读 .dat（不再走远程读）。
//
// merge 把源数据迁到 master host 的 data_path 后：
//  - 各 worker 旧的 local_idx/remote_idx 残留指向已删源 .dat 的位置 → 读必失败
//  - 新 idx 在共享 db_path（merge worker 写的 <merge_writer_id>.idx）
//  - 新 .dat 在 data_path（master host 本地或共享盘）
// worker 收到此消息后清旧索引，并尝试 load 新 idx 重建 local_idx：
//  - 若 data_path 可达（同机本地盘或共享 FS）→ 本地直读 .dat
//  - 若不可达 → local_idx 为空，读走 TIER2/TIER3 回源到持有数据的 worker
// 详见 docs/db-merge-design.md §5.5（merge 后状态清理）。
struct MergeCleanupMessage {
    MessageHeader header_;
    CMString db_path_;       // 源 db_path（worker 清旧索引用 + ack 匹配 pending key）
    CMString data_path_;       // merge 后 data_path（.dat 所在，master host 本地）
    CMString target_db_path_;  // merge 产物 db_path（idx 所在目录，cross-path 时 != db_path_）
    CMVector<uint64_t> exempt_worker_ids_;  // merge target worker（已持有效 local_idx，跳过重建）
    bool purge_target_ = false;  // 失败清理模式：源命名空间全保留（重 merge 支撑）；
                                 // 持有 target_data_path merge writer 的 worker 删除自己的产物

    static constexpr MessageType msg_type_ = MessageType::MERGE_CLEANUP;
    FLY_SERIALIZE(header_, db_path_, data_path_, target_db_path_, exempt_worker_ids_, purge_target_);
};

// worker → master: cleanup 完成回报。这是 merge_db 返回前的"全局一致性屏障"：
// master 广播 MergeCleanup 后，必须等待所有 worker 回此 ack，才能保证 merge_db 返回时
// 全局状态一致（worker 已清旧索引 + 按新路径重建 local_idx）。否则用户在 merge_db 返回后
// 立即读，可能命中 worker 正在清理的中间态。master 收齐 ack 后才重建自身 remote_idx，
// 保证 master 与 worker 的最终状态一致。
struct MergeCleanupAckMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMString db_path_;

    static constexpr MessageType msg_type_ = MessageType::MERGE_CLEANUP_ACK;
    FLY_SERIALIZE(header_, worker_id_, db_path_);
};

// =============================================================================
// Message 日志系统 — 高价值信息的远程推送与 summary 收集。
// worker 把配额内的高价值 message 推送到 master；master 集中写 message.log +
// 输出 terminal。进程结束前 master 主动广播请求，收集各 worker 的 message 触发
// 次数，合并打印 summary。详见 docs/message-system.md。
// =============================================================================

// worker → master: 推送高价值 message（async, no ack）。
// domain_id_ 格式 "DOMAIN::NNNN"。worker 在本地 debug log 已打印（带 [DOMAIN::NNNN] 前缀），
// master 收到后写 message.log + 输出 terminal（受 master 侧 message id/domain 两层配额控制）。
// source_ 用于区分同一 id 的不同触发位置（不参与配额，仅作打印标注）。
struct LogMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    LogLevel level_ = LogLevel::INFO;
    CMString domain_id_;   // "DOMAIN::NNNN"
    int32_t source_ = 0;   // 触发位置标识（业务自定义，打印为 [DOMAIN::NNNN] <source> msg）
    CMString msg_;

    static constexpr MessageType msg_type_ = MessageType::LOG_MESSAGE;
    FLY_SERIALIZE(header_, worker_id_, level_, domain_id_, source_, msg_);
};

// master → worker (broadcast): 请求 worker 上报本地 message 触发次数（summary 屏障）。
// master stop() 在发 ShutdownMessage 之前广播此消息，等所有 worker 回 MSG_COUNT_REPORT。
struct MessageCountRequestMessage {
    MessageHeader header_;

    static constexpr MessageType msg_type_ = MessageType::MSG_COUNT_REQUEST;
    FLY_SERIALIZE(header_);
};

// worker → master: 上报本地 message 触发次数（id 级 + domain 级两套计数）。
// keys_/values_ 一一对应；master 收齐所有 worker 后合并 + 自身计数打印 summary。
struct MessageCountReportMessage {
    MessageHeader header_;
    uint64_t worker_id_ = 0;
    CMVector<CMString> id_keys_;          // 如 ["SOLVER::0047", "SYS::0001"]
    CMVector<uint64_t> id_values_;        // 与 id_keys_ 一一对应
    CMVector<CMString> domain_keys_;      // 如 ["SOLVER", "SYS"]
    CMVector<uint64_t> domain_values_;    // 与 domain_keys_ 一一对应

    static constexpr MessageType msg_type_ = MessageType::MSG_COUNT_REPORT;
    FLY_SERIALIZE(header_, worker_id_, id_keys_, id_values_, domain_keys_, domain_values_);
};

// master → worker (broadcast): 同步 message 配额设置（全量快照）。
// master 在用户 set_*_limit 后（或新 worker 注册补发时）广播当前所有配额设置。
// worker 收到后整体替换本地 Registry 配额（不清零已触发计数，支持运行时动态修改）。
// 三层配额链式优先级（per-id > per-domain > global），详见 docs/message-system.md §3。
struct MessageLimitSyncMessage {
    MessageHeader header_;
    int32_t global_limit_ = 20;                  // global 默认配额
    CMVector<CMString> domain_keys_;             // per-domain 显式配额的 domain 名
    CMVector<int32_t> domain_values_;            // 与 domain_keys_ 一一对应
    CMVector<CMString> id_keys_;                 // per-id 显式配额的 message id（"DOMAIN::NNNN"）
    CMVector<int32_t> id_values_;                // 与 id_keys_ 一一对应

    static constexpr MessageType msg_type_ = MessageType::MSG_LIMIT_SYNC;
    FLY_SERIALIZE(header_, global_limit_, domain_keys_, domain_values_, id_keys_, id_values_);
};

// ── 业务 RPC（worker ↔ peer，独立业务端口）──────────────────────
// PeerChannelGroup 的底层传输：请求-响应式 RPC，payload 是裸 bytes
// （业务自定义序列化，不经 bitsery/pickle 的 DB 包装）。
// rpc_id 匹配请求与响应；status 区分正常响应与主动失败通知（notify_failure）。
struct PeerRpcRequestMessage {
    MessageHeader header_;
    uint64_t rpc_id_ = 0;           // 请求 id（PendingRpcMap 匹配响应）
    uint64_t src_worker_id_ = 0;    // 发送方 worker id（调试/路由用）
    CMString payload_;              // 裸 bytes（业务序列化）

    static constexpr MessageType msg_type_ = MessageType::PEER_RPC_REQUEST;
    FLY_SERIALIZE(header_, rpc_id_, src_worker_id_, payload_);
};

// status_: 0=正常响应, 1=主动失败通知（notify_failure，payload_ 是 reason）
struct PeerRpcResponseMessage {
    MessageHeader header_;
    uint64_t rpc_id_ = 0;
    uint8_t status_ = 0;            // 0=ok, 1=error(notify_failure)
    CMString payload_;              // 裸 bytes（业务序列化 / 失败 reason）

    static constexpr MessageType msg_type_ = MessageType::PEER_RPC_RESPONSE;
    FLY_SERIALIZE(header_, rpc_id_, status_, payload_);
};

}  // namespace fly
