# 009 — Dynamic 求解器重投场景的 worker 属性池契约待增强（2026-08-27）

> 状态：**已根治（2026-08-27，ensure_workers 框架增强落地）**。
> 原始问题与演进记录保留如下。关联：`src/solver/py/ras_graph_dynamic.py`
> 三阶段架构、`qa/solver/test_ras_graph_dynamic.pyt` restart 断点 subcase、
> `fly.ensure_workers` / `SolveDb.worker_attr`（新 API）。

---

## 问题

dynamic 多右端项求解器的三阶段架构（setup/solver/controller）引入了跨 task
的 **worker 进程级状态**：

- 成员 worker：LDLT/子域数据缓存 + PeerRpc server 端口；
- check worker：粗校正 LU / A_fine / 子域索引缓存 + 到全部成员的 channel 池。

这些进程级缓存只能被同进程上的 task 访问，因此 setup/check/controller/
cleanup 等 driver 类 task 都带 `requires=["ras_check"]` 或 `["sd_{sd}"]`
硬约束，把它们钉在持有缓存的 worker 上。

后果：**重投/重启场景要求使用者保证"带正确属性的 worker 已先于重投任务
就绪"**。该契约目前靠手工维持，两处脆弱点已被实测撞中：

1. `load_db` 在 meta 的 hostname 上无 worker 时会自动 spawn **空属性**
   worker 补位。restart 脚本若先 `load_db` 再 launch 带属性池，空属性编队
   先注册并抢占 worker 编号；`wait_for_workers(n)` 只数连接数，判定通过时
   `ras_check` 属性的 worker 可能尚未注册完成——`restart_failed_tasks`
   随即执行，重投任务以 `No worker with required capabilities: [ras_check]`
   秒失败并落回 bin。
2. 即便顺序正确（先 launch 后 load_db，现行 QA 规避），契约本身仍依赖使用
   者记得"重投前必须重建与原 run 一致的属性编队"。属性名是 solver 与使用
   者之间的隐式约定，无框架校验。

## 旧版为何不需要

旧版（每时间步短命 task 组重构之前的 v3 实现）中 check/controller 无专属
属性 requires（随机派发到任意 idle worker）：

- check 每步自行经 db 地址对象重连成员（地址重读 + connect 重试窗口），
  无常驻 channel 池；粗校正缓存缺失只是性能损失不是正确性损失；
- 重投的 check 落到任意 worker（含 load_db auto-spawn 的空属性 worker）
  都能工作——进程级状态不存在，也就没有钉住需求。

## 新版为什么需要（收益代价对照）

新版把三件东西提升为跨 task 复用的进程级资产：

| 资产 | 旧版行为 | 新版行为 |
|---|---|---|
| LDLT/子域数据 | 每时间步重新构建 | 建 key=matrix_ref 缓存，全链一次 |
| 粗校正 LU/A_fine | 每步重建 | 建 key=matrix_ref 缓存 |
| 连接 | 每步重建（陈旧端口竞态多发） | per-gen channel 池，长持复用 |

收益：n500 实测每步省 ~0.7s 重建开销，且消除了旧版连接层的三类竞态。
代价即本 issue：driver task 的亲和性从"最好落在持有者上"变成"必须落在
持有者上"，worker 编队成了正确性前提。

## 现行规避（已被框架增强取代）

- ~~`qa/solver/rasgd_restart_run2.py`：launch 先于 load_db（杜绝 auto-spawn
  占位）；launch 完整属性编队（nsd × sd_i + ras_check）后再 restart。~~
  已移除：run2 现为 load_db 先行 → 数量补齐 → `ensure_workers`。

## 根治落地（2026-08-27）

`fly.ensure_workers(workers, timeout=10.0, exclude=None)` + 两项配套：

1. **属性下行通道**（原缺失）：新消息 `WORKER_PROPERTY_ASSIGN=61`——worker
   去重应用后沿既有 WORKER_PROPERTY_UPDATE 上报，视图/调度零新增链路；
2. **就绪原语**：两阶段收集（时限内 IDLE、到点放宽 BUSY 靠调度接管）+
   静态预检立即失败 + 幂等盘点；以 WorkerManager 注册记录为就绪口径
   （脆弱点 1 的"只数连接数"误判消除）；
3. **命名单点规范**：`SolveDb.worker_attr(tag)` = `"rasg:{uid}:{tag}"`
   （脆弱点 2 的隐式契约消除——solver 与使用者共用同一生成入口，uid 跨进程
   持久使 restart 场景 requires 与申请自动闭环；worker 侧可用性由 executor
   "先导入 task 模块完成子类注册、再反序列化 db 参数"的时序保证）。

双 flow 防碰撞三层：属性 uid 命名空间（requires 精确匹配不串池）、exclude
正则物理隔离（solver 默认 `^rasg:`）、资源不足静态预检显式失败。QA 见
qa/scheduling/test_ensure_workers*.py 三 case 与改造后的 rasgd_restart_run2。
