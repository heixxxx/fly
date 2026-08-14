#pragma once

namespace fly {

// SIGTERM 优雅退出的进程级信号灯。
//
// main.cpp 启动时 install_graceful_shutdown_handlers() 注册 SIGTERM handler
// （handler 内只做 atomic 写，async-signal-safe）。master 的 heartbeat 检查
// 线程与 worker 的 is_running() 轮询消费该标志：
//   - master：trigger stop() 三阶段 drain（等 RUNNING task → 广播 summary →
//     Shutdown → persist pending）
//   - worker：Python poll 循环观察到 is_running()==false 退出，走 agent.stop()
//
// 不复用旧的 check_shutdown_request（无 drain 等待、drain 线程不 join 的
// 死代码路径）；SIGTERM 的语义 = 请求走完整 stop()，而非立即砍断。
void install_graceful_shutdown_handlers();

// 仅设置标志（信号 handler 与单测使用）。幂等。
void set_graceful_shutdown();

// 标志是否已置位。单测里用于验证/复位（reset 即再次 set 前先 clear 的场景
// 由 reset_graceful_shutdown 提供）。
bool graceful_shutdown_signalled();

// 清除标志（单测隔离用；生产代码不调用）。
void reset_graceful_shutdown();

}  // namespace fly
