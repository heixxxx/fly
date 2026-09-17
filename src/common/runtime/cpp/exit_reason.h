#pragma once

#include <cstdint>

namespace fly {

// worker 退出性质（用户裁定：master/worker 双侧显式区分正常退出与异常退出，
// 不靠 reason 字符串猜测）。枚举值仅内部与 WorkerExitMessage 诊断字段使用，
// 外部观测方看进程退出码（exit_code：graceful=0 / abnormal=3）。
//
// 自 agent/cpp/worker_agent.h 下沉（2026-09 审计批次 C）：线上消息
// WorkerExitMessage.exit_reason_ 承载本枚举，network 不允许反向依赖
// agent——下沉后 network 与 agent 共用单一定义。uint8 同宽，wire 编码不变。
enum class ExitReason : uint8_t {
    MASTER_SHUTDOWN = 0,       // master ShutdownMessage 优雅关停 → graceful
    LOCAL_STOP = 1,            // stop() API 本地显式停止 → graceful
    MASTER_LOST = 2,           // 心跳超时/连接丢失/重连宽限耗尽 → abnormal
    REGISTRATION_REJECTED = 3, // 重复 worker id 被 master 拒绝 → abnormal
};

}  // namespace fly
