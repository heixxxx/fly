# Fly 文档地图

> 2026-08-16 重组：删除 15 份已完成使命的历史文档（locality 三部曲、架构/代码/覆盖率审计快照、旧性能分析、superpowers 过程稿等，git 历史可查），合并相似文档（perf-baseline ×2 → perf-baselines.md；solver 预研 ×5 → solver/rejected-alternatives.md；architecture/overview.md 并入 architecture.md），新增本地图。

## 快速入口（按用途）

| 我想… | 看这里 |
|-------|--------|
| 了解系统整体架构与使用 | [architecture.md](architecture.md)（主文档：分层/线程/数据流/协议/目录） |
| 写代码前熟悉规范 | [DEVELOPMENT_GUIDELINES.md](DEVELOPMENT_GUIDELINES.md) + 根目录 [CLAUDE.md](../CLAUDE.md) / [AGENTS.md](../AGENTS.md) |
| 新建 C++ 模块 | [NEW_MODULE_GUIDE.md](NEW_MODULE_GUIDE.md) |
| 了解用户侧 API | [userdoc.md](userdoc.md) + [python-api/module.md](python-api/module.md)（公开符号权威总表） |
| 打开集群监控 GUI | [monitor-design.md](monitor-design.md)（`fly --serve-monitor <db>` 实时/事后浏览） |
| 查当前待办/路线 | [remaining-todo.md](remaining-todo.md)（欠账清单）+ [roadmap.md](roadmap.md)（决策记录）+ [emir-capability-gap.md](emir-capability-gap.md)（能力差距演进参考） |
| 查已知问题 | [ISSUES.md](ISSUES.md) |
| push 被拦/门禁行为 | [push-hook.md](push-hook.md) |
| 了解某次改动的来龙去脉 | [DOC_CHANGELOG.md](DOC_CHANGELOG.md)（倒序，最新在顶部） |

## 设计与决策（按主题）

| 主题 | 文档 |
|------|------|
| cluster monitor（采集落盘+Web GUI） | [monitor-design.md](monitor-design.md)（SQL 选型/事件全景/口径/GUI 手册） |
| RunSummary 运行摘要 | [run-summary-metrics-design.md](run-summary-metrics-design.md) |
| 架构决策记录 | [adr/](adr/)（0001 db_meta、0002 db_id 废弃） |
| 存储链路设计 | [db-chain-design.md](db-chain-design.md)（db 身份/uid/task 参数编码权威）、[db-merge-design.md](db-merge-design.md)（⚠️ 迁移追踪部分已被 db-chain 取代，见其头注） |
| 大对象分块传输与数据完整性 | [chunked-transfer-design.md](chunked-transfer-design.md)（设计+实施计划合一：L0 帧头/校验/零容忍 → L2 分片 → L3 读流式 → L1 写流式；L0 待批准即开工） |
| 消息日志系统 | [message-system.md](message-system.md)（消息类型语义全表权威在 [network/module.md](network/module.md)「消息类型总表」） |
| Project 机制 | [project-design.md](project-design.md) |
| 优先级调度 | [priority-scheduling-design.md](priority-scheduling-design.md) |
| 内存增长分析（M1 依据） | [memory-growth-analysis.md](memory-growth-analysis.md) |
| 竞品对比与战略边界 | [competitor-analysis.md](competitor-analysis.md)（2026-06 版本，部分状态以 roadmap 修正为准） |
| **能力演进与差距分析** | [emir-capability-gap.md](emir-capability-gap.md)（面向分布式 EMIR 工具的框架能力现状 + 差距路线） |

## 性能与测试

| 主题 | 文档 |
|------|------|
| 性能分析参考 | [performance-analysis.md](performance-analysis.md) |
| V2 chunked-transfer 性能分析（2026-08-30） | [performance-analysis-2026-08-30.md](performance-analysis-2026-08-30.md)（每轮 QA +10s 回归归因：§4.7 缓存裁定代价 + CRC 校验层数；帧 CRC 冗余待裁定项） |
| micro-benchmark 基线数据 | [perf-baselines.md](perf-baselines.md)（DataService 锁分片 S7 + 调度热循环 S8） |
| 覆盖率测量方法 | [coverage-testing.md](coverage-testing.md)（工具：tools/measure_coverage.sh）；最近一次实测报告 [coverage-report-2026-08-16.md](coverage-report-2026-08-16.md) |

## Solver 子系统（[solver/](solver/)）

| 文档 | 内容 |
|------|------|
| [module.md](solver/module.md) | 模块结构 |
| [matrix-solver-analysis.md](matrix-solver-analysis.md)（上级目录） | RAS 算法与收敛理论 |
| [optimization-roadmap.md](solver/optimization-roadmap.md) | **优化决策中枢**（已完成/已否决/方向排序） |
| [rejected-alternatives.md](solver/rejected-alternatives.md) | 备选方向预研决策集（GPU×2/分布式 PCG/树形 Allreduce） |
| [iter-refactor-design.md](solver/iter-refactor-design.md) / [iter-refactor-impl-plan.md](solver/iter-refactor-impl-plan.md) | v2 常驻 daemon + PeerChannelGroup RPC 设计与实施 |
| [perf-n1000-optimization.md](solver/perf-n1000-optimization.md) | n=1000 优化报告（S9） |

## 模块文档（docs/&lt;module&gt;/module.md）

agent、common、core、export、log、network、python-api、serialization、storage、task、test —— 每模块一份，实现细节与内部约定；模块清单见 architecture.md §七。

## 问题追踪体系

- [ISSUES.md](ISSUES.md) —— 总表（P0-P3 + X 类，状态/根因/修复记录）
- [issues/](issues/) —— 单个问题的深度分析（001-009 + check-daemon-shutdown-race）
- [reviews/](reviews/) —— 实施评审记录

## 文档约定

- **一处权威，他处链接**：每类数据/清单/机制描述只在其归属文档维护一份；其他文档需要提及时用链接指明出处，**不复制内容、不复制易失真数字**（消息类型数量、QA case 数、导出符号数等随开发漂移的统计一律不写死，指向权威源码或权威表）。当前各权威落点：
  - 消息类型语义全表 → [network/module.md](network/module.md)「消息类型总表」
  - `from fly import ...` 公开符号总表 → [python-api/module.md](python-api/module.md)「公开符号总表」
  - runqa 并行度/发现机制/case 总数口径 → [qa/README.md](../qa/README.md)「并行度权威口径」
  - task 参数编码（`__fly_db2__`）与 db 身份/uid → [db-chain-design.md](db-chain-design.md)
  - pre-push 流水线配方 → `scripts/pre-push`（源码即权威），行为说明见 [push-hook.md](push-hook.md)
- 所有结构性改动记入 [DOC_CHANGELOG.md](DOC_CHANGELOG.md)（倒序追加）
- 状态标记：❌ 未做 / 🟡 部分 / ✅ 已做 / ⏸ 待触发 / ⛔ 明确不做
- 历史文档删除原则：任务完结且结论已沉淀到活跃文档（roadmap/architecture/CHANGELOG）后删除，git 历史可查；被后续机制部分取代的历史设计文档**不删**，在文头加「部分被取代」注并链接取代者
