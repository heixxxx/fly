# design_sets —— design db + timing db 规模化验证输入件（三套）

用户 2026-09-15 需求：三套不同规模、不同功能的 design 输入件，供
design db + timing db（块绑定 / 分 block TWF 读取）规模化验证。
与 `../timing/`（途径二：Nangate45 + OpenSTA 自造链）同一工具链与
语义口径；Nangate45 三件（Liberty + tech/macro LEF）**复用
`../timing/` 文件，不重复存储**。

## 三套总览（数字为 2026-09-17 实测，固定参数确定性生成——重跑逐字节一致）

| 套 | 规模（展开实例） | 拓扑 | 层级 | 物理 | 块绑定形态覆盖 |
|----|----------------|------|------|------|----------------|
| A `set_a/` | 5036（route 后 = 生成器 4893 + CTS 缓冲 143） | 算术链：36 通道 × 22 级流水（DFF→FA→XOR2→AND2→OR2→INV）+ 全通道 OR 归约 + clk_aux 旁路域；双时钟 clk_main 1.0ns / clk_aux 2.5ns | 扁平（单层） | **OpenROAD**：floorplan+place（skip_nesterov，见下）+CTS+global_route（`set_a_flow.tcl`） | 顶层纯路径全名 TWF（mixed，21707 条目） |
| B `set_b/` | 32666（顶层 666 + mesh_cell 50×640） | NoC mesh：mesh_cell 路由节点（链路寄存器 + 路由寄存器 + 交叉开关 + 仲裁）32×20 阵列 + 顶层双时钟缓冲树；clk_noc 1.2ns / clk_cfg 3.0ns | 两级（top + mesh 单元 block） | **place 级**（生成器直写层级 DEF：PLACED 网格、无布线几何） | **block_cell**（mesh_cell 定义级 ×640 复制）+ strip |
| C `set_c/` | 102192（mini 同构子集 690） | 混合：pe_core（3 级 ALU 流水）+ tile_router（5 方路由）→ compute_tile（中间块）30×25 阵列 + ctrl_block（CSR/FSM）+ 三时钟树；clk_core 1.0 / clk_noc 1.6 / clk_cfg 4.0 | ≥3 级（top + compute_tile/ctrl_block + pe_core/tile_router） | **floorplan 级**（生成器直写层级 DEF） | **block_inst**（多级路径 `u_tile_1_0/u_pe_0`）+ block_cell（tile_router ×N 复制）+ strip |

### 关键机制覆盖（用户裁定）

1. **分 block TWF + 块绑定两形态**：C 套每个 leaf/mid block 用独立
   OpenSTA 会话生成块视角 TWF（局部名，无顶层路径）；`build_timing_db`
   消费时 `block_inst`（C 套 pe_core/ctrl_block）与 `block_cell`
   （B 套 mesh_cell ×640、C 套 tile_router ×N）两种形态均覆盖 +
   `strip_prefix` 人为前缀包装形态（`*_strip.twf` = 块 TWF 全条目加
   `tb/u_dut/` 前缀的文本变体）。
2. **Liberty cell 存在性防呆（强制）**：`gen_common.check_cells_available`
   在写盘前把引用 cell 名集合与 Nangate45 typical.lib cell 集合做差集，
   非空立即 fail 并列出缺名（幻影 cell 教训，见 `../timing/README.md`）。
   三套生成器共用此工具。
3. **物理分级策略**：A 套 OpenROAD 物理（布图/布局/CTS/global route）；
   B/C 套层级 DEF 由生成器直写（place / floorplan 级）。两个实测约束：
   - **OpenROAD 预编译包 write_def 输出平铺网表**（2026-09-15 实测
     module 层级展开为 `u_sub/u_dff` 形态、层级丢失）——层级 DEF 无法
     经 OpenROAD 产出；直写 DEF 同时规避 10 万实例的 OpenROAD 内存
     风险（5GB WSL 约束）；
   - **A 套布线取 global route 级**：Nesterov 求值器对本设计发散
     （GPL-0305，三种密度/利用率组合实测），skip_nesterov 布局拥塞高
     使 detailed_route 初始违规 31111、收敛长尾达小时级（2026-09-17
     实测）——GRT 结果已为 DEF 提供真实导线几何（design db NETS 可
     部分锚定 driver：1551/4971 网条目命中），对照面目的不受影响。
   物理分级相应为：A 套 global-route 级 / B 套 place 级 / C 套
   floorplan 级。

## 文件清单

```
gen_common.py       共用工具：Liberty 防呆 + Module（Verilog/DEF 同源渲染）
                    + strip 前缀变体
gen_set_a.py        A 套生成器（网表 + SDC + stats.json）
set_a_flow.tcl      A 套 OpenROAD 完整物理流程
gen_set_b.py        B 套生成器（块网表/SDC + 层级 DEF 两份 + stats.json）
gen_set_c.py        C 套生成器（4 块定义 + 全量/mini 顶层，-- 无 CLI 参数；
                    mini 与全量同函数生成）
twf_gen_hier.tcl    TWF 生成器（语义映射与 ../timing/twf_gen.tcl 同源；
                    增加多文件 Verilog 读入 + get_pins -hierarchical *
                    层级引脚枚举（TWF_PIN_HIER=1）+ 输出前缀参数）
verify_full.sh      三套全量再生成 + 建库验证（手动跑；QA 只覆盖
                    mini/存在性断言）
verify_full_impl.py 全量建库断言实现（verify_full.sh 调用）
set_a/ set_b/ set_c/ 产物（分档规则见下）
```

## 产物分档（>5MB 单文件不入 git）

| 文件 | 大小 | 入库 |
|------|------|------|
| set_a/*（def 0.7MB + mixed TWF 1.9MB + routed.v 等 8 件） | 全部 <5MB | ✅ 全部（QA 依赖） |
| set_b/mesh_cell.*、noc_mesh.{v,sdc,def}、mesh_cell_strip.twf | <1MB | ✅（QA 依赖） |
| set_b/noc_mesh{,_pins,_mixed}.twf | mixed/pins 各 14.3MB（net 维度 3.2KB 入库） | ❌ 仅大件（verify_full.sh 再生成） |
| set_c/ 块定义件 + mini 件 + 块 TWF + strip | <1MB | ✅（QA 依赖） |
| set_c/hybrid_soc.{v,def}（全量顶层） | 0.47/0.55MB | ✅ |

## 再生成方法（cwd = 本目录）

```bash
# 一键全量（A 物理 + B/C 数据 + 全部 TWF + 建库验证）
bash verify_full.sh all          # 或 a / b / c 单套

# 分步：
python3 gen_set_a.py                     # A 网表+SDC
/root/project/openroad_env/bin/openroad -no_init -exit set_a_flow.tcl
/root/project/opensta/build/sta -no_init -exit twf_gen_hier.tcl \
  （env TWF_TOP=arith_chain TWF_V_FILES=set_a/arith_chain_routed.v ...）
python3 gen_set_b.py && python3 gen_set_c.py    # B/C 数据件
# TWF / strip 变体：见 verify_full.sh 各 run_stage 命令
```

工具链说明与已知上游缺陷（report_checks 预热、字面 NULL 回退、
waveform 兜底、顶层输入端口 arrival 缺失）同 `../timing/README.md`。

## QA 断言锚点（2026-09-17 实测）

- **C mini**（`qa/emir/test_emir_design_sets_c.py`，2.6s）：树 18 节点；
  四绑定文件 entry=1154 hit=787 dangling=303（含 rt block_cell ×4 =
  36×4）skip_pin=172；三时钟合并零冲突；block_cell 复制 4 实例全命中；
  strip 剥离后命中 u_pe_1；分区落库 300 实例。
- **B**（`test_emir_design_sets_b.py`，4.7s）：树 641 节点、展开 32666、
  顶层网 10372；block_cell 绑定 entry=262 hit=175 dangling=47×640=
  30080（落库复制口径）；strip 单实例 hit=175 strip_miss=0。
- **A**（`test_emir_design_sets_a.py`，3.6s）：单层 5036 实例、网空间
  4972；mixed entry=21707 hit=16573 dangling=3420（GRT 级几何部分锚定
  口径）skip_pin=1714；双时钟 clk_main/clk_aux 归属点查；分区并集
  5036 实例（mixed 维度 hit 为条目级、落库按实例恰一份）。

### hit / dangling 计数口径（timing db 实测语义）

- hit 为**条目级 bool 计数**：一条目在 ≥1 实例命中计 1（block_cell 复制
  不放大 hit）；落库数据与 dangling 按实例复制放大（×实例数）；
- 无布线几何套的网维度条目全部 TIMG::0003 悬空（合法兜底口径，非数据
  缺陷）；DFF 的 IQ/IQN 引脚不在 LEF pin 集 → TIMG::0002（skip_pin）。

## 许可

Nangate45 三件复用 `../timing/`（Apache 2.0，出处见彼处）；本目录
生成器与产物为 fly 自有测试数据。
