#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace fly {

enum class TaskErrorType {
    UNKNOWN = 0,               // "无错误"哨兵（多处 `!= UNKNOWN` 判失败），非字面"未知错误"
    EXECUTION_ERROR = 1,
    WRITE_TO_FROZEN_DB = 2,
    // 写注册被拒的通用兜底：空 write_context_hash 到达 master（非法请求，非
    // provenance mismatch）/ worker 终止时注册未确认 / master 未运行窗口。
    // 原设想「对象已存在拒绝」（issue 003 方案 A）已被 DUPLICATE_SKIPPED +
    // provenance 体系取代。
    WRITE_REGISTRATION_FAILED = 3,
    WRITE_REGISTRATION_TIMEOUT = 4,
    WRITE_PROVENANCE_MISMATCH = 5,
    WRITE_DUPLICATE_SKIPPED = 6,
    DB_ALREADY_FROZEN = 7,   // freeze requested on an already-(pending-)frozen db (业务流程错误)
};

// Write operation result code. Independent from TaskErrorType (which is the
// task-execution-level cumulative state). WriteErrorType is the per-call return
// value of write_object — callers check it directly.
enum class WriteErrorType {
    OK = 0,                    // success
    FROZEN_DB = 1,             // database is frozen (read-only)
    REGISTRATION_FAILED = 2,   // write registration rejected (e.g. provenance mismatch)
    REGISTRATION_TIMEOUT = 3,  // write registration timed out
    DUPLICATE_SKIPPED = 4,     // duplicate write of same object (benign, not an error)
};

// Remote read result code. Returned by DataClientPool::request /
// DataClient::request_compressed_data and propagated through the
// DirectCompressedReadCallback to the TIER2 multi-replica retry loop in
// DataService::read_raw_compressed. The classification drives retry policy:
//
//   NONE            — success
//   DATA_NOT_READY  — transient: peer is still writing the object; the TIER2
//                     loop may retry indefinitely (data is expected to appear)
//   OBJECT_NOT_FOUND— permanent for THIS replica: this worker no longer holds
//                     the object; TIER2 removes the replica and moves on
//   NETWORK         — transient: connection / decode / protocol error; TIER2
//                     retries the replica but under a bounded deadline (a fully
//                     unreachable peer must not be retried forever)
//   SHUTDOWN        — permanent: the pool is being stopped; abort immediately,
//                     do not retry
//   CHECKSUM        — data corruption (wire root CRC mismatch / frame header
//                     check bits). 工业零容忍语义（§5）：调用方对校验类错误
//                     只允许【一次】对象级重取，仍败即 FATAL——不做静默重试
//                     轮询（持续校验失败 = 内存/硬件/代码缺陷，必须大声暴露）。
enum class ReadError {
    NONE = 0,
    DATA_NOT_READY = 1,
    OBJECT_NOT_FOUND = 2,
    NETWORK = 3,
    SHUTDOWN = 4,
    CHECKSUM = 5,
};

// 数据校验失败且一次重取预算耗尽（零容忍语义，chunked-transfer-design §5）。
// 抛出点：DataService 读路径（tier2 校验预算耗尽）/ Database::read_object_compressed
// （trailer 校验重取后仍败）/ Python 解压出口（块 CRC 重取后仍败）。
// 捕获方必须终止当前 task（TaskFailed / RuntimeError("[FATAL-DATA-CORRUPTION] ...")），
// 不存在降级消费路径。what() 以 [FATAL-DATA-CORRUPTION] 开头。
class DataCorruptionError : public std::runtime_error {
public:
    explicit DataCorruptionError(const std::string& what)
        : std::runtime_error(what) {}
};

}  // namespace fly
