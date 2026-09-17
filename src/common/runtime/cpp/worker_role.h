#pragma once

#include <cstdint>

namespace fly {

// worker role——独立于 attributes（可随时增减、参与调度匹配）的**静态身份**：
// 注册时设定、不可变更（无修改途径）。hybrid=普通 worker（默认）；
// storage_only=存储 worker——调度决策不感知（get_idle_workers 层过滤，scheduler
// 零 role 概念），但仍参与心跳判死/数据面/internal 数据 task（merge/backup）。
//
// 自 task/cpp/worker_manager.h 下沉（2026-09 审计批次 C）：线上消息
// RegisterMessage.role_ 承载本枚举，network 不允许反向依赖 task——下沉后
// network 与 task/agent 共用单一定义。uint8 同宽，wire 编码不变。
enum class WorkerRole : uint8_t {
    HYBRID = 0,
    STORAGE_ONLY = 1,
};

}  // namespace fly
