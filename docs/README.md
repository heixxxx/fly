# Fly 文档地图

> 2026-08-16 重组：删除 15 份已完成使命的历史文档（locality 三部曲、架构/代码/覆盖率审计快照、旧性能分析、superpowers 过程稿等，git 历史可查），合并相似文档（perf-baseline ×2 → perf-baselines.md；solver 预研 ×5 → solver/rejected-alternatives.md；architecture/overview.md 并入 architecture.md），新增本地图。

## 快速入口（按用途）

| 我想… | 看这里 |
|-------|--------|
| 了解系统整体架构与使用 | [architecture.md](architecture.md)（主文档：分层/线程/数据流/协议/目录） |
| 写代码前熟悉规范 | [DEVELOPMENT_GUIDELINES.md](DEVELOPMENT_GUIDELINES.md) + 根目录 [CLAUDE.md](../CLAUDE.md) / [AGENTS.md](../AGENTS.md) |
| 新建 C++ 模块 | [NEW_MODULE_GUIDE.md](NEW_MODULE_GUIDE.md) |
| 了解用户侧 API | [userdoc.md](userdoc.md) + [python-api/module.md](python-api/module.md) |
| 打开集群监控 GUI | [monitor-design.md](monitor-design.md)（`fly --serve-monitor <db>` 实时/事后浏览） |
| 查当前待办/路线 | [remaining-todo.md](remaining-todo.md)（欠账清单）+ [roadmap.md](roadmap.md)（决策记录） |
| 查已知问题 | [ISSUES.md](ISSUES.md) |
| push 被拦/门禁行为 | [push-hook.md](push-hook.md) |
| 了解某次改动的来龙去脉 | [DOC_CHANGELOG.md](DOC_CHANGELOG.md)（倒序，最新在顶部） |

## 设计与决策（按主题）

| 主题 | 文档 |
|------|------|
| cluster monitor（采集落盘+Web GUI） | [monitor-design.md](monitor-design.md)（SQL 选型/事件全景/口径/GUI 手册） |
| 架构决策记录 | [adr/](adr/)（0001 db_meta、0002 db_id 废弃） |
| 存储链路设计 | [db-chain-design.md](db-chain-design.md)、[db-merge-design.md](db-merge-design.md) |
| 消息日志系统 | [message-system.md](message-system.md) |
| Project 机制 | [project-design.md](project-design.md) |
| 优先级调度 | [priority-scheduling-design.md](priority-scheduling-design.md) |
| 内存增长分析（M1 依据） | [memory-growth-analysis.md](memory-growth-analysis.md) |
| 竞品对比与战略边界 | [competitor-analysis.md](competitor-analysis.md)（2026-06 版本，部分状态以 roadmap 修正为准） |

## 性能与测试

| 主题 | 文档 |
|------|------|
| 性能分析参考 | [performance-analysis.md](performance-analysis.md) |
| micro-benchmark 基线数据 | [perf-baselines.md](perf-baselines.md)（DataService 锁分片 S7 + 调度热循环 S8） |
| 覆盖率测量方法 | [coverage-testing.md](coverage-testing.md)（工具：tools/measure_coverage.sh） |

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
- [issues/](issues/) —— 单个问题的深度分析（001-007 + check-daemon-shutdown-race）
- [reviews/](reviews/) —— 实施评审记录

## 文档约定

- 所有结构性改动记入 [DOC_CHANGELOG.md](DOC_CHANGELOG.md)（倒序追加）
- 状态标记：❌ 未做 / 🟡 部分 / ✅ 已做 / ⏸ 待触发 / ⛔ 明确不做
- 历史文档删除原则：任务完结且结论已沉淀到活跃文档（roadmap/architecture/CHANGELOG）后删除，git 历史可查
