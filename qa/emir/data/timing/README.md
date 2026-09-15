# timing db 测试数据（途径二：Nangate45 + OpenSTA 自造链）

timing db 测试数据三途径（2026-09-15 裁定，见 `docs/emir/module.md` timing 行）中
**途径二的产物**：全公开工具链自造的 lib/design/timing 三库配套输入件——真实
Liberty/LEF 工艺库 + 小型确定性设计 + 真实静态时序引擎计算的时序窗口文件，
网名/引脚名与 DEF 完全一致（TWF 名字对齐的确定性来源）。

## 文件清单

| 文件 | 角色 | fly 侧消费 |
|------|------|-----------|
| `NangateOpenCellLibrary_typical.lib` | Nangate45 工艺库 Liberty（typical 角落） | lib db（`build_lib_db`） |
| `NangateOpenCellLibrary.tech.lef` | 工艺 LEF（层定义） | design db（`build_design_db`） |
| `NangateOpenCellLibrary.macro.lef` | 单元 LEF | design db |
| `tm_design.v` | 确定性小型设计网表（5 单元：时钟缓冲 + 反相器 + 与非门 + 2 D 触发器） | OpenSTA 时序引擎输入 |
| `tm_design.sdc` | 时序约束（时钟周期 1.0ns + 输入/输出延迟） | OpenSTA 时序引擎输入 |
| `tm_design.def` | 同网表的物理实现（放置 + 网连接；引脚/网名与网表一致） | design db |
| `twf_gen.tcl` | TWF 生成脚本（OpenSTA 引脚属性 → Innovus 手册 25.10 版格式） | 再生成 |
| `tm_design.twf` | 生成产物（已提交，确定性：同环境两次生成逐字节一致） | timing db（解析器单测 + 后续建库 flow e2e）；副本在 `src/emir/timing/tests/data/` |
| `NangateOpenCellLibrary_LICENSE` | 平台文件许可（Apache License 2.0） | 出处声明 |

## 再生成方法

工具链（2026-09-15 实测版本）：

1. **OpenSTA**（The-OpenROAD-Project/OpenSTA，master 5c215d3271，GPLv3）：
   需先构建 CUDD（The-OpenROAD-Project/cudd，autotools `./configure && make install`），
   再 `cmake -B build -DCUDD_DIR=<cudd 安装前缀>`；构建依赖 swig / tcl-dev /
   libeigen3-dev / libgtest-dev / bison / flex。
2. 执行：`cd qa/emir/data/timing && sta -no_init -exit twf_gen.tcl`

### 已知上游缺陷与绕法（脚本内已内置，再生成时无需处理）

- `Properties::pinArrival` 缺 `ensureGraph()`（`pinSlew` 有）——直接查引脚
  arrival 属性在 `Graph::pinVertices` 内 SIGSEGV；脚本先 `report_checks` 完成
  min/max 双向路径搜索 + 任意引脚一次 slew 查询触发图构建；
- 部分属性对特定对象返回字面 `NULL` 字符串（如 `get_nets -of_objects` 对时钟
  源端口引脚）——脚本按引脚名兜底查询；
- 时钟对象无 `waveform` 属性——POSEDGE 0 / NEGEDGE 周期一半兜底（等价
  `create_clock` 缺省波形）；
- 顶层输入端口对象的属性面缺 `arrival`——普通输入端口驱动网（本设计为 `d`）
  的窗口列发 `*`（缺省值形态本身是解析器需覆盖的路径）；slew/slack 可查照常输出。

### 语义弱化（相对 Innovus 原生 TWF，README 记录在案）

- 实例数据引脚的 `clocks` 属性为空 → 数据网归 `CAUSED_BY NULL` 组（Innovus
  按路径时钟归组）；单时钟设计无影响，时钟归属可由消费侧结合时钟表恢复；
- RDr/FDr（源电阻）列恒 `*`（OpenSTA 无对应属性；fly 解析器本就弃收该列）。

## 许可

- Nangate45 平台三件（Liberty + 两个 LEF）：Apache License 2.0，随附
  `NangateOpenCellLibrary_LICENSE`（出处：OpenROAD-flow-scripts 仓库
  `flow/platforms/nangate45`，2026-09-15 取自 master）。
- `tm_design.*` 与 `twf_gen.tcl`：fly 自有测试数据/脚本。
