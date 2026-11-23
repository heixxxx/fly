# waveform db 测试数据（真实 VCD——Icarus Verilog 仿真产物）

waveform db（EMIR ⑧ 波形数据库，方案
[docs/emir/waveform-db-plan.md](../../docs/emir/waveform-db-plan.md)）的
QA 输入件：全部 `.vcd` 由开源仿真器 **Icarus Verilog**（`iverilog` +
`vvp`，IEEE 1364 标准 VCD）仿真结构化 Verilog 网表产生，**非手写**。
Verilog 网表与 DEF 物理设计由 `gen_design.py` **同一张网表数据结构同源
生成**——「模块例化名 = DEF 实例名、线网名 = DEF 网名」逐字一致，是
VCD 信号名可与 design db 匹配的确定性来源（同 timing db 先例
`../timing/tm_design.v` 与 `tm_design.def` 的对齐手法）。

## 路线声明：路线 B（自建三件套）

立项调研后走 **路线 B**（任务书两路线中的自建路），理由：

1. 既有 design 测试设计（`../timing/tm_design.def` 5 单元、
   `../design_sets/` 三套）全部是 **1 bit 标量网**——没有向量总线、
   没有计数器，无法覆盖 small.vcd 必须的「计数器 + 向量总线拆位 +
   统计值手工精确推导」特征；
2. 分块转储（场景 b）、`$dumpvars(1, inst)` 形态②、x/z 构造都需要
   **定制层级结构与激励**，既有设计层级（set_b/set_c 的 NoC/SoC 阵列）
   过大且引脚结构不匹配；
3. 自建设计的单元库**完全复用既有 Nangate45 数据**
   （`../timing/NangateOpenCellLibrary{.tech.lef,.macro.lef,_typical.lib}`，
   不重复存储）——自建件只有 DEF + Verilog，LEF/lib 建库链与
   timing/design db QA 完全同源。

建库时（waveform db QA 实施时）：

```python
lib_db = proj.build_lib_db(name="...", lib_paths=[TIMING + "NangateOpenCellLibrary_typical.lib"])
design_db = proj.build_design_db(
    name="wf_design",
    def_paths=[DATA + "design/wf_sub.def",   # 层级库：子定义在前
               DATA + "design/wf_blk_a.def",
               DATA + "design/wf_blk_b.def",
               DATA + "design/wf_blk_top.def"],  # 顶层在后（timing 先例）
    lef_paths=[TECH_LEF, MACRO_LEF], lib_db=lib_db)
```

单元引脚名取自 Nangate45 macro LEF（INV_X1: A/ZN；XOR2_X1: A/B/Z；
AND2_X1: A1/A2/ZN；DFFR_X1: D/CK/RN/Q/QN；DFF_X1: D/CK/Q/QN；
BUF_X1/CLKBUF_X1: A/Z；TBUF_X1: A/EN/Z）；`rtl/cells_sim.v` 是这些单元
的行为仿真模型（零延迟、四态语义正确）。

## 文件清单

| 文件 | 说明 |
|------|------|
| `gen.sh` | 一键再生成：gen_design.py → iverilog/vvp 仿真 → 全部 VCD |
| `gen_design.py` | Verilog + DEF 同源生成器（纯文本模板，确定性逐字节一致） |
| `rtl/cells_sim.v` | Nangate45 单元子集行为模型（手写；内部状态 reg 统一命名 `q_r`） |
| `rtl/wf_small.v` 等 8 份 | 各仿真的顶层 Verilog（生成产物；顶层模块名 = DEF DESIGN 名） |
| `design/*.def` 9 份 | design db 输入件（生成产物；与 Verilog 名字逐字一致） |
| `small.vcd` | 主断言载体：时钟 + 4bit 行波计数器（向量总线）+ 组合观测 + 二分频 |
| `part_a1.vcd` `part_a2.vcd` | 场景 a 分段转储：同一 wf_small 电路分两段独立仿真，a2 时间戳从 0 重新开始 |
| `block_b1.vcd` `block_b2.vcd` | 场景 b 分块转储：块内局部 scope + 刻意同实例引脚重合构造 |
| `xz.vcd` | x/z 统计载体：无复位 DFF 初始 x 段 + 三态网 z 段 + force x/1/z 与 release |
| `instpin.vcd` | 形态②直配：`$dumpvars(1, u_blk_a.u_inv1)`——var 名 = cell 端口名（= LEF pin 名） |
| `big.vcd` | 大规模：100×100 个 T-FF 单元 = 20000 实例（DEF 同规模） |

## 再生成方法

```bash
apt-get install -y iverilog        # Icarus Verilog（验证过 10.3）
cd qa/emir/data/waveform && bash gen.sh
```

确定性：全部激励固定、无随机源。重跑产物除 `$date` 头（每次运行时间，
解析器不消费）外**逐字节一致**（2026-10-12 实测双跑验证）。`.build/`
编译中间目录由脚本自清；产物 `.vcd`/`.v`/`.def` 全部落本目录。

## VCD 名字空间结构（实施必读）

实测（Icarus 10.3）各文件的 VCD 结构约定：

1. **根 scope = 顶层模块名 = DEF 的 DESIGN 名**（如 `$scope module
   wf_small`）。即 var 的 scope 全路径形如 `wf_small/u_cnt0/Q`。消费侧
   匹配 design db 名字空间时需剥除根 scope 段（真实商业 VCD 根 scope
   同样千奇百怪——testbench 名/设计名——waveform db 实现需定此口径）。
2. **剥根后的形态**与 design db 名字映射器实测命中（2026-10-12 用
   `build_design_db` + `load_name_mapper` 实测，全部正反查一致）：
   - 形态①net 名：`clk`、`count[3]`（**DEF 逐位独立网**）、层级网
     `u_blk_a/n_a1`、`u_blk_a/u_sub/n1`（`/` 分隔，scope 路径 + var 名）；
   - 形态②inst pin：scope 路径 = 实例层级路径（`u_blk_a/u_inv1`）、
     var 名 = pin 名（`A`/`ZN`——cell 端口名）。
3. **id 码复用**（别名信号共用 id）：端口连接（顶层 `clk` 与
   `u_cnt0/CK`）与 assign 直连别名共用同一 id 码。同 id 多名 = 多个
   名字看到同一份值序列，逐名统计应得相同值（一致性断言点）。
4. **重复值事件行**：组合逻辑毛刺在同刻完成时，Icarus 会记录与前一
   时刻相同的终值行（如实测 `n_cnt2_d` 在 t=55 出现与前值相同的 `1`）。
   统计口径必须对重复值**幂等**（本目录核对脚本已按此口径验证）。
   方案 §2「向量拆位后仅变化位产生事件条目」的假设在真实 VCD 上要
   求拆位器做位值等价过滤——本样张天然覆盖该路径。

## 名字对齐声明（VCD → design db 对照表）

| VCD | 依赖 design（def_paths） | 覆盖的匹配形态与代表条目 |
|-----|------------------------|------------------------|
| `small.vcd` | `design/wf_small.def` | 形态①扁平网全命中：`clk`(PIN 网)、`rn`、向量 `count[3:0]` 拆位 → `count[0..3]` 独立网、`clk_div2`、`n_and`、`n_xor`、内部网 `n_cnt0_d/n_cnt1_d/n_cnt2_d/n_cnt3_d/n_b1/n_b2/n_div_d`；另含 cell 端口 var（`u_cnt0/Q` 等 = 形态②素材，`u_cnt0` 命中实例、`Q` 命中 pin） |
| `part_a1.vcd` / `part_a2.vcd` | `design/wf_small_a1.def` / `design/wf_small_a2.def` | 同 wf_small（DEF 仅 DESIGN 名与激励无关部分不同；**两文件各自绑各 DEF**，或按 DESIGN 名前缀剥离后共用 wf_small.def——两口径均可，见「实施注意事项」） |
| `block_b1.vcd` | `design/` 层级 4 DEF + `block_inst="u_blk_a"` | 局部名：`n_a1`/`n_a2`/`n_out`/`clk`/`rn` → 块 A 自身网/端口 |
| `block_b2.vcd` | 同上 + `block_inst="u_blk_a/u_sub"` | 局部名：`n1`/`n2`/`n3`/`clk`/`rn`/`n_in` → 子块网；**重合构造：第二条 var `n_a1`**（scope 路径 `u_blk_a` 层）换算后与 b1 的 `n_a1` 落同一实体 → 后文件覆盖（WFMG::0006） |
| `xz.vcd` | `design/wf_xz.def` | 形态①：`n_q`/`n_inv`（纯内部网）、`n_tri`/`n_f`（接顶层 PIN 的网）、`en`/`n_seed`（激励网，无 PIN 声明） |
| `instpin.vcd` | 同 blk 4 DEF（无需绑定，纯路径） | 形态②直配：scope `u_blk_a/u_inv1` + var `A`/`ZN` → (实例 `u_blk_a/u_inv1`, pin `A`/`ZN`)；`A`/`ZN` 不在任何 net 名空间（「net 名未命中路径」的真样本） |
| `big.vcd` | `design/wf_big.def` | 形态①扁平 2 万网：`n_q_<row>_<col>`/`n_d_<row>_<col>` ×10000 + `clk`/`rn` |

**形态②的注**：`instpin.vcd` 的 var 全路径 = `wf_blk_top/u_blk_a/u_inv1/A`；
剥根 + 拆末段后 scope `u_blk_a/u_inv1` 经 instance mapper 命中实例、末段
`A` 经 pin 存在性校验命中（INV_X1 的 LEF pin）。值序列上 `A` ≡ 网名
`u_blk_a/n_a1`（端口连接别名、共 id）——形态①与形态②命中同一物理
信号时统计必然一致，可作交叉断言。

## small.vcd 预期统计推导表（QA 精确断言依据）

**激励**：`clk` 初值 0、每 5ns 翻转（周期 10ns）；`rn` 初值 0、t=23 释放；
仿真终止 t=201ns（finish 落在非沿时刻，避开 t=200 沿的调度竞争）。
`$timescale 1ns`、文件时间范围 [0, 201]。

**推导链**：

1. 时钟沿：rise @ 5,15,…,195（**20 个**）；fall @ 10,20,…,200（**20 个**）。
   T1 = 20×5 = **100**，T0 = 201−100 = **101**。
2. `rn`：t=23 一个 rise → T0=23、T1=178、rise=1、fall=0。
3. 有效计数沿（rn=1 后的 posedge）：**25,35,…,195 共 18 个**，count 每
   沿 +1，计数序列 1..18（mod 16 后末值 2）。
4. 行波计数器（T 触发器链：cnt[k] 每 2^k 个计数翻转一次）：
   - `count[0]` 翻 18 次 → rise 9 / fall 9；T1 = 9×10 = **90**、T0 = **111**；
   - `count[1]` 翻 9 次（沿 2,4,…,18）→ rise 5 / fall 4；
     T1 = [35,55)+[75,95)+[115,135)+[155,175)+[195,201] = 20×4+6 = **86**；
   - `count[2]` 翻 4 次（沿 4,8,12,16 = t 55,95,135,175）→ rise 2 / fall 2；
     T1 = [55,95)+[135,175) = **80**；
   - `count[3]` 翻 2 次（沿 8,16 = t 95,175）→ rise 1 / fall 1；T1 = [95,175) = **80**。
5. 组合观测网（值段由计数序列逐段推出）：
   - `n_and = count[3]&count[2]` → 1 当 count ∈ [12,15] → 1 段 [135,175)：
     T1=40、T0=161、rise 1（@135）/ fall 1（@175）；
   - `n_xor = count[1]^count[0]` → 1 当 count mod 4 ∈ {1,2} → 5 段
     [25,45)+[65,85)+[105,125)+[145,165)+[185,201]：T1=20×4+16=**96**、
     T0=105、rise 5（@25,65,105,145,185）/ fall 4（@45,85,125,165）；
   - `n_b1 = count[0]&count[1]`（进位）→ 1 当 count mod 4 = 3 → 4 段共
     **40**：T0=161、rise 4 / fall 4；
   - `n_b2 = n_b1&count[2]` → 1 当 count mod 8 = 7 → [85,95)+[165,175)：
     T1=**20**、T0=181、rise 2 / fall 2；
   - `n_cnt0_d = ~count[0]`：与 count[0] 对偶 → T0=90、T1=111、rise 9 / fall 9；
   - `n_cnt1_d = count[0]^count[1]`：函数同 `n_xor`（异或交换律）→ 值
     序列相同：T1=96、T0=105、rise 5 / fall 4（与 `n_xor` 交叉一致性断言点）；
   - `n_cnt2_d = n_b1^count[2]` → 1 段 [45,85)+[125,165)：T1=**80**、
     T0=121、rise 2（@45,125）/ fall 2（@85,165）；
   - `n_cnt3_d = n_b2^count[3]` → 1 段 [85,165)（count 7 时 b2=1/c3=0，
     count 8..14 时 c3=1/b2=0）：T1=**80**、T0=121、rise 1 / fall 1；
   - `n_div_d = ~clk_div2`：对偶二分频 → T0=90、T1=111、rise 9 / fall 9。
6. `clk_div2`（u_div 每 posedge 翻转）：18 沿翻 18 次 → rise 9 / fall 9、
   T1 = 9×10 = **90**、T0 = **111**。

**汇总表**（实测 = 本表，58 项断言全过；时长单位 ns）：

| 信号 | T0 | T1 | TX | TZ | rise | fall |
|------|----|----|----|----|------|------|
| clk | 101 | 100 | 0 | 0 | 20 | 20 |
| rn | 23 | 178 | 0 | 0 | 1 | 0 |
| count[0] | 111 | 90 | 0 | 0 | 9 | 9 |
| count[1] | 115 | 86 | 0 | 0 | 5 | 4 |
| count[2] | 121 | 80 | 0 | 0 | 2 | 2 |
| count[3] | 121 | 80 | 0 | 0 | 1 | 1 |
| clk_div2 | 111 | 90 | 0 | 0 | 9 | 9 |
| n_and | 161 | 40 | 0 | 0 | 1 | 1 |
| n_xor | 105 | 96 | 0 | 0 | 5 | 4 |
| n_cnt0_d | 90 | 111 | 0 | 0 | 9 | 9 |
| n_cnt1_d | 105 | 96 | 0 | 0 | 5 | 4 |
| n_cnt2_d | 121 | 80 | 0 | 0 | 2 | 2 |
| n_cnt3_d | 121 | 80 | 0 | 0 | 1 | 1 |
| n_b1 | 161 | 40 | 0 | 0 | 4 | 4 |
| n_b2 | 181 | 20 | 0 | 0 | 2 | 2 |
| n_div_d | 90 | 111 | 0 | 0 | 9 | 9 |

cell 端口 var 对偶（形态②素材，值序列由连接别名决定）：`u_cnt0/Q` ≡
count[0]、`u_cnt0/QN` = 90/111 rise 9 / fall 9、`u_and1/ZN` ≡ n_and、
`u_xor1/Z` ≡ n_xor。x 全程为 0（DFFR_X1 复位即清 x——x 构造在 xz.vcd）。

## 其余 VCD 的核对值（2026-10-12 实测裁决）

以下各表同样经独立脚本逐项核对（时长单位 ns）。

**part_a1.vcd**（同电路分段 1：[0,101]；rn@23 释放，有效沿 25..95 共 8 个）：

| 信号 | T0 | T1 | rise | fall |
|------|----|----|------|------|
| clk | 51 | 50 | 10 | 10 |
| rn | 23 | 78 | 1 | 0 |
| count[0] | 61 | 40 | 4 | 4 |
| count[1] | 61 | 40 | 2 | 2 |
| count[2] | 61 | 40 | 1 | 1 |
| count[3] | 95 | 6 | 1 | 0 |
| clk_div2 | 61 | 40 | 4 | 4 |
| n_and | 101 | 0 | 0 | 0 |
| n_xor | 61 | 40 | 2 | 2 |

**part_a2.vcd**（分段 2：时间戳从 0 重新开始，[0,101]；count 初值 8 =
a1 末值（经 force/release 注入 DFF 内部状态）、rn 恒 1；有效沿 5..95
共 10 个，计数 9..18）：

| 信号 | T0 | T1 | rise | fall |
|------|----|----|------|------|
| clk | 51 | 50 | 10 | 10 |
| rn | 0 | 101 | 0 | 0 |
| count[0] | 51 | 50 | 5 | 5 |
| count[1] | 55 | 46 | 3 | 2 |
| count[2] | 61 | 40 | 1 | 1 |
| count[3] | 26 | 75 | 0 | 1 |
| clk_div2 | 51 | 50 | 5 | 5 |
| n_and | 61 | 40 | 1 | 1 |
| n_xor | 45 | 56 | 3 | 2 |

**场景 a 直和关系**：count[0..2]/clk_div2/n_xor 的 rise/fall 直和 =
small.vcd 对应值（8+10=18 沿等）；count[3] 因 a2 初值非 0，直和语义按
SAIF 原始量口径自然成立（rise 1+0、fall 0+1）。注意 a1/a2 各自的
finish 收尾（非沿时刻）使 DURATION 直和 = 101+101 = 202 ≠ 单跑 201，
且 a2 偏移后相位整体 +1ns——分段转储的固有语义，断言按「直和 + 偏移
后全局单调」写，不要求拼接 == 单跑（见实施注意事项 3）。

**xz.vcd**（[0,101]；n_seed 不赋初值保持 x；en@30↑/60↓；n_f 全程经
force 构造）：

| 信号 | T0 | T1 | TX | TZ | rise | fall | 关键转换 |
|------|----|----|----|----|------|------|---------|
| n_q | 40 | 46 | 15 | 0 | 2 | 1 | x→0@15（不计）、rise@25,75、fall@45 |
| n_inv | 46 | 40 | 15 | 0 | 1 | 2 | ~n_q；x→1@15（不计） |
| n_tri | 15 | 15 | 0 | 71 | 1 | 0 | z→0@30（EN 导通，不计）、rise@45、1→z@60（不计） |
| n_f | 23 | 23 | 35 | 20 | 0 | 0 | force x@18、force 1@38、force z@58、release@78——净转换全被 x/z 吞掉（极端构造） |
| en | 71 | 30 | 0 | 0 | 1 | 1 | |
| n_seed | 42 | 51 | 8 | 0 | 2 | 1 | x→0@8（不计） |
| clk | 51 | 50 | 0 | 0 | 10 | 10 | |

cell 端口对偶：`u_dff/Q` ≡ n_q、`u_inv1/ZN` ≡ n_inv、`u_tri/Z` ≡ n_tri、
`u_tri/EN` ≡ en。**断言口径**：rise/fall 不含任何 x/z 参与的转换（方案
裁定 7）；TX/TZ 分列如表。

**block_b1.vcd**（`$dumpvars(1, u_blk_a)`——块 A 一层；[0,84]；有效沿
25..75 共 6 个）：u_blk_a 是纯 T-FF（D=~Q），n_a1 每沿翻转：

| var（局部名） | T0 | T1 | rise | fall |
|--------------|----|----|------|------|
| n_a1 | 54 | 30 | 3 | 3 |
| n_a2（=~n_a1） | 30 | 54 | 3 | 3 |
| n_out（=~n_a1） | 30 | 54 | 3 | 3 |
| clk | 44 | 40 | 8 | 8 |
| rn | 23 | 61 | 1 | 0 |

**block_b2.vcd**（`$dumpvars(0, u_blk_a.u_sub)` 子块全 dump + 单条
`u_blk_a/n_a1`；激励时长刻意缩到 [0,74]，沿 25..65 共 5 个——重合条目
n_a1 在两文件中**统计值不同**，覆盖语义可观测）：

| var | T0 | T1 | rise | fall |
|-----|----|----|------|------|
| u_blk_a/n_a1（重合条目，b2 版） | 45 | 29 | 3 | 2 |
| u_sub/n1 | 45 | 29 | 2 | 1 |
| u_sub/n2（=~n3） | 20 | 54 | 1 | 1 |
| u_sub/n3（=n1^n_in） | 54 | 20 | 1 | 1 |
| u_sub/clk | 39 | 35 | 7 | 7 |
| u_sub/rn | 23 | 51 | 1 | 0 |
| u_sub/n_in（≡n_a1 别名） | 45 | 29 | 3 | 2 |

**覆盖断言**：绑定 `block_inst="u_blk_a"`（b1）与 `block_inst=
"u_blk_a/u_sub"`（b2）联合建库后——(1) 重合实体 `u_blk_a/n_a1` 取 b2
值（T0=45/T1=29/rise3/fall2，≠ b1 的 54/30/3/3）+ WFMG::0006 计数 1；
(2) b1 独有条目（n_a2/n_out）保留 b1 值；(3) b2 独有条目（u_sub 网全部）
为新值。

**instpin.vcd**（`$dumpvars(1, u_blk_a.u_inv1)`；[0,84]，同 b1 激励）：

| var | T0 | T1 | rise | fall |
|-----|----|----|------|------|
| u_inv1/A（≡n_a1） | 54 | 30 | 3 | 3 |
| u_inv1/ZN（≡n_a2） | 30 | 54 | 3 | 3 |

形态②命中：(实例 `u_blk_a/u_inv1`, pin `A`/`ZN`) 单引脚独享波形；
与 b1 的形态①网统计交叉一致（A ≡ u_blk_a/n_a1）。

**big.vcd**（[0,36]；rn@3 释放，有效沿 5/15/25/35 共 4 个；每 T-FF 单元
Q 翻 4 次）：

| var | T0 | T1 | rise | fall |
|-----|----|----|------|------|
| n_q_<r>_<c>（×10000） | 16 | 20 | 2 | 2 |
| n_d_<r>_<c>（×10000，=~Q） | 20 | 16 | 2 | 2 |
| clk | 20 | 16 | 4 | 3 |
| rn | 3 | 33 | 1 | 0 |

规模：100002 var 声明、200010 事件行、4.33MB（<5MB 入库红线）；
DEF 20000 COMPONENTS、20002 NETS、2.57MB。**断言口径**（万级下限）：
实例数 ≥ 10000、`u_dff_0_0`/`u_dff_99_99` 名字命中、命中条目数下限、
分区路由非空——不逐信号精确断言。

## waveform db QA 实施注意事项

1. **根 scope 剥除口径**：所有样张根 scope = DESIGN 名（`wf_small`/
   `wf_blk_top`/`wf_xz`/`wf_big`）。实现按「剥根后名字匹配」或「DESIGN
   名纳入名字空间」二选一定死；b1/b2/instpin 共用根名 `wf_blk_top`
   （三份仿真顶层同名模块、独立编译）。
2. **a2 时间戳从 0 重新开始**（分段仿真的真实形态）：场景 a 偏移对齐
   逻辑「嗅探各文件首末时间戳 + 前文件时长累计」实测会得到 a2 全局
   相位 = [101,202]（真实相位 [100,201] +1ns）——样张故意保持 10ns
   干净网格、不为对齐而错位激励。断言写「直和成立 + 偏移后全局单调
   不减」，不要写「拼接 == 单跑 small.vcd」。
3. **场景 b 重合构造的具体信号名**：`u_blk_a/n_a1`（b1 的块自身网 vs
   b2 的单条额外 var，scope 路径同为 `u_blk_a` 层）——两文件中该实体
   统计值刻意不同（见上表），后文件覆盖可精确断言；其余条目两文件
   理论不相交（b1 = 块 A 一层，b2 = 子块内部）。
4. **id 码复用**：别名信号（端口连接、assign 直连）共用 id——解析器
   按 (scope, var 名) 建 var 表、id 仅作值载体；同 id 多名逐名统计必须
   一致（交叉断言点）。
5. **重复值事件行**：真实 VCD 含与前一时刻终值相同的记录行（组合毛刺
   同刻聚合），统计口径需幂等；向量拆位同样会遇到「位值未变的重复
   记录」，方案 §2「仅变化位产生事件条目」要求拆位器做等价过滤。
6. **xz.vcd 的 force 语义**：n_f 的值全部经 testbench force 注入
   （x→1→z→release），净 rise/fall 为 0 而各态时长非平凡——验证
   「x/z 参与的转换不计入 rise/fall」的极端样本。
7. **instpin.vcd 的形态②路径**：var 名（A/ZN）不在任何 net 名空间，
   形态①必然未命中 → 落形态②；若实现先试①再拆末段试②，此样张覆盖
   完整路径。
8. **建库前置**：lib db 用 `../timing/NangateOpenCellLibrary_typical.lib`
   （6.7MB，建库数分钟级）；LEF 用同目录 tech+macro 两件。Liberty 中
   TAPCELL_X1 等 cell 不在 LEF 覆盖内属 Nangate45 数据固有（DSGN::0003
   warn），与本数据无关。
9. **`$dumpvars` 快照形态**：t=0 的 `$dumpvars` 段记录全部 var 初值
   （含 x/z）——文件首事件在 #0；时间戳全程单调（runqa 口径的破损流
   /重复时间戳由手写样张负责，见方案 §11.1 末行）。

## 规模一览（2026-10-12 实测）

| 文件 | 大小 | var 数 | 事件行 | DEF 实例数 |
|------|------|--------|--------|-----------|
| small.vcd | 3.9KB | 68 | 450 | 14 |
| part_a1.vcd | 3.1KB | 68 | 227 | 14 |
| part_a2.vcd | 3.2KB | 68 | 257 | 14 |
| block_b1.vcd | 521B | 5 | 40 | 2+4+3（层级） |
| block_b2.vcd | 1.0KB | 18 | 41 | 同上 |
| instpin.vcd | 365B | 2 | 14 | 同上 |
| xz.vcd | 1.0KB | 19 | 62 | 4 |
| big.vcd | 4.3MB | 100002 | 200010 | 20000 |

## 许可

单元模型 `rtl/cells_sim.v` 与全部生成器/产物为 fly 自有测试数据；
Nangate45 三件（lib/LEF）复用 `../timing/`（Apache 2.0，出处声明见彼处
README）。
