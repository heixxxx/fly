# 011 — TSAN 首轮扫描发现：DataServer::stop / EpollMultiplexerImpl::destroy 数据竞争

> 状态：**OPEN**（2026-09-04 立项，TSAN opt-in 工具链落地时实证）
> 来源：P3-17 批 3 TSAN 配置落地后的首轮扫描（`--config=tsan`）。
> 复现：`./fly.sh test --config=tsan //src/storage/tests:data_service_test`
> （用例 104/104 全过，2 条 TSAN 警告使 bazel 判失败；非 TSAN 构建全绿）。

## 已根治（同轮修复）

- **TcpConnectionManager::ensure_epoll 惰性 epoll 创建**：`epoll_fd_` 的
  「读-判-写」无锁，多线程并发 connect/listen/poll 首调竞争（新增并发测试
  `ConcurrentConnectCloseKeepsRegistryConsistent` + TSAN 实证）。修复 =
  构造期急切创建，ensure_epoll 退化为只读判断。

## 待治理（预存在，触发面 = DataServerStartStop 测试）

TSAN 双报告（触发于既有 `DataServerStartStop` 用例的 start→stop→start 生命周期）：

1. **DataServer::stop()** 与其内部线程（start 时 pthread_create 的
   epoll/send 线程）之间的共享状态竞争——与 8419526 lost-wakeup 家族
   同区域，需用 gdb/TSAN 双栈精确归因；
2. **EpollMultiplexerImpl::destroy(int)**：epoll fd 销毁路径与仍在进行的
   epoll 操作并发。

## 修复方向（待专项）

1. 按 `systematic-debugging` 流程：TSAN 双栈（Write of / Previous read）
   定位无锁共享字段；区分「测试 teardown 时序伪影」与「生产真实竞态」
   （stop_data_server 与 run 中 stop 共用路径——生产 stop 已有 drain 语义）；
2. 根因修复后以 TSAN 连跑 10 轮 data_service_test 收口；
3. 完成后本 issue 关闭，并在 §13.4 记录最终口径。

## 关联

- P3-17 批 3（TSAN opt-in 工具链，.bazelrc `build:tsan`）
- 8419526（DataServer::stop lost wakeup 前科）
