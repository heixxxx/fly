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
| `tm_design.twf` | 生成产物·**网络维度**（Innovus 缺省风味，键 = 网名，值取自驱动引脚；7 条） | timing db（解析器单测 + 后续建库 flow e2e）；副本在 `src/emir/timing/tests/data/` |
| `tm_design_pins.twf` | 生成产物·**引脚维度**（`-pin` 风味，键 = 实例/引脚名；19 条，含未连接引脚 IQ/IQN 的 `*` 缺省形态） | 同上 |
| `tm_design_mixed.twf` | 生成产物·**混合维度**（同一 CAUSED_BY 分组内 NET 与 PIN 条目并存；26 条）——解析器必须支持的形态（2026-09-15 裁定） | 同上 |
| `NangateOpenCellLibrary_LICENSE` | 平台文件许可（Apache License 2.0） | 出处声明 |

## 再生成方法

工具链（2026-09-15 实测版本）：

1. **OpenSTA**（The-OpenROAD-Project/OpenSTA，master 5c215d3271，GPLv3）：
   需先构建 CUDD（The-OpenROAD-Project/cudd，autotools `./configure && make install`），
   再 `cmake -B build -DCUDD_DIR=<cudd 安装前缀>`；构建依赖 swig / tcl-dev /
   libeigen3-dev / libgtest-dev / bison / flex。
2. 执行：`cd qa/emir/data/timing && sta -no_init -exit twf_gen.tcl`
   （一次运行产出三份：`tm_design.twf` 网络维度 / `tm_design_pins.twf`
   引脚维度 / `tm_design_mixed.twf` 混合维度；同环境重复运行逐字节一致，
   跨工具链重建数值不变——仅 VERSION 号随脚本版本变化）

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

- 实例数据引脚的 `clocks` 属性为空 → 数据条目归 `CAUSED_BY NULL` 组（Innovus
  按路径时钟归组）；单时钟设计无影响，时钟归属可由消费侧结合时钟表恢复；
- RDr/FDr（源电阻）列恒 `*`（OpenSTA 无对应属性；fly 解析器本就弃收该列）。

### 三版本的维度互补与跨维度一致性（单测断言依据）

- 网络维度条目值取自该网驱动引脚 → 与引脚维度中驱动引脚条目**逐值相同**
  （`n2` ≡ `u_nand/ZN`、`q1` ≡ `u_d1/Q`、`nclk` ≡ `u_cb/Z`，零线负载）；
- 网络维度的已知局限（输入端口网 `d` 窗口列 `*`）在引脚维度**互补补全**
  （数据端点 `u_d1/D`/`u_inv/A` 的到达 = 输入延迟 0.05ns 可查）；
- 混合维度两版条目并存于同一 CAUSED_BY 分组，条目名（网名 vs 实例/引脚
  名）天然不冲突——解析器按 NET/PIN 关键字分派维度标记（pin_kind 位）。

## pg_grid 放大设计族（tm_design 放大版：64×64 阵列 + 时钟缓冲树）

| 文件 | 角色 |
|------|------|
| `gen_pg_grid.py` | 网表/SDC 生成器（确定性：纯算术生成，无随机；缺省 N=64 → 12365 实例） |
| `pg_grid.v` / `pg_grid.sdc` | 生成的逻辑输入件（时钟缓冲树：根/中层 CLKBUF_X3、行缓冲 CLKBUF_X2） |
| `pg_grid_flow.tcl` | OpenROAD 流程：布图（利用率 30%）→ IO 引脚 → 电源网格（官方 M1-M4-M7 策略）→ 布局 → CTS → 布线 → `write_def` + `write_verilog` |
| `pg_grid_twf.tcl` | 独立 OpenSTA 驱动：读布线后网表产出 `pg_grid.twf`（TWF 权威单一路径） |
| `pg_grid.def` | 流程产物：真实自动布局布线结果（6.5MB；双跑逐字节一致） |
| `pg_grid_routed.v` | 流程产物：布线后网表（含 CTS 插入的 H-Tree 缓冲）——TWF 的输入 |
| `pg_grid.twf` | 网络维度 TWF（12943 条，12942 条 RTW/FTW 有值；双跑逐字节一致）；副本在 `src/emir/timing/tests/data/`（解析器单测 `GeneratedPgGridFile`） |

### 三步再生成链（确定性，cwd = 本目录）

```bash
python3 gen_pg_grid.py                                            # 1. 网表+SDC
/root/project/openroad_env/bin/openroad -no_init -exit pg_grid_flow.tcl   # 2. DEF+布线后网表（约 7 分钟）
/root/project/opensta/build/sta -no_init -exit pg_grid_twf.tcl    # 3. TWF
```

第 2/3 步工具位于 `/root/project/` 下（2026-09-15 安装；不随仓库分发）。

### 工具分工与内嵌引擎属性缺失说明

- **OpenROAD**（LiteX-Hub 预编译包，构建 f12e2f47）：只用于布图/PDN/布局/
  CTS/布线与 `write_def`/`write_verilog`。其**内嵌 OpenSTA 为旧版，引脚级
  属性面缺失**（2026-09-15 实测查 arrival 报「pin objects do not have a
  arrival_min_rise property」）——不得由它产出 TWF；
- **独立 OpenSTA 3.1.0**（`/root/project/opensta/build/sta`，master
  5c215d3271）：引脚属性完整（arrival/slew/slack/clocks），TWF 权威路径；
  从布线后网表出发（不读 DEF）——数值语义为单元弧时序 + 零线负载 +
  传播时钟（网表含 CTS 时钟树），确定可复现。

### 确定性与已知坑（再生成前必读）

- `pg_grid.def` 流程双跑逐字节一致；`pg_grid.twf` 驱动双跑逐字节一致
  （2026-09-15 实证）；
- 网表时钟树单元**必须取 Nangate45 实际存在的 CLKBUF_X1/X2/X3**——
  X4/X16 是其它平台的单元，2026-09-15 实测引用后 OpenROAD link 建
  black box、LEF master 缺失丢弃实例，时钟根网零负载致 TritonCTS 报
  「No clock nets」空转不插树，时钟链全断（TWF 全 `*`）；
- TWF 仅 `d` 一条窗口缺省（顶层输入端口属性面局限，与 tm_design 族同
  形态）；C 标记仅时钟源网 `clk` 一条，时钟树 650 条归 `CAUSED_BY "clk"`
  组（与 tm_design 的 `nclk` 同构）。

## 许可

- Nangate45 平台三件（Liberty + 两个 LEF）：Apache License 2.0，随附
  `NangateOpenCellLibrary_LICENSE`（出处：OpenROAD-flow-scripts 仓库
  `flow/platforms/nangate45`，2026-09-15 取自 master）。
- `tm_design.*` 与 `twf_gen.tcl`：fly 自有测试数据/脚本。
