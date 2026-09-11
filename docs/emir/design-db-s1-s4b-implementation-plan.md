# design db S1-S4b 实施设计与实现计划

> **定位**：design db 首版实施（范围 S1-S4b）的详细设计与实现计划——[design-db-plan.md](design-db-plan.md)（需求评审稿，裁定 ①-㉓ 已批准）的实施落地文档， coder 子智能体大规模实现的委托依据。
> **创建**：2026-09-10。**状态**：✅ **实施完成并经代码审查（2026-09-10）**——T1-T6 全批次交付，单测 22 用例 + lefdef 基线 4 用例 + QA e2e 2 case 全绿，真实工业数据（ISPD 2018 Nangate45 样例 + Tilos Nangate45 tech lef）烟测 8 项计数对照全部一致；审查修复 2 项缺陷（见文末实施记录），全部验证复跑通过。
> **范围**：S1 tech lef（Stack + tech 级 via）→ S2 cell lef → S3 lib cell ↔ lef cell merge → S4 DEF 头扫描（block cell/port）→ S4b DEF via 定义解析（merge 回 DSDesign）→ freeze 持久化。**不含** S5a/S5b/S6-S10（后续迭代，API 不变、内部阶段追加）。
> **约束来源**：[dev-rules.md](dev-rules.md)（全部适用）、[design-db-plan.md](design-db-plan.md) 裁定 ①-㉓、[emir-data-flow.md](../emir-data-flow.md) 裁定 12（DS 前缀）/18（settings/alpha）。

---

## 0. 未裁定项的实施取值（按评审稿建议值，审查时可改）

| 裁定点 | 取值 |
|--------|------|
| D1 pin id 编码 | 全局平铺单调分配（pin 总量 = cell 数 × 引脚数，百万级可行） |
| D3 block cell/port 来源 | 子 block DEF 头扫描（S4），不支持抽象 LEF（接口留后） |
| D12 id 位宽与坐标 | 各 id 空间独立 uint32；坐标 int32（DBU）；中间量/面积 int64；geometry 模板实例化 {int32, int64, double} |
| D13 VIARULE | 首版支持按参数展开（LEFDEFAGERULE/VIA 语句参数生成几何） |
| D16 LEF 层收录 | 布线层 + 切割层（implant/特殊层跳过计数） |
| D19 OBS 几何 | 入库（cell 定义层，量小） |
| D21 消息前缀 | **DSGN** |
| D28 geometry 归属 | `src/container/geometry/` 子模块 + CM 前缀（CMPoint/CMRect/CMPolygon），与 CMLookupTable 同体系 |

## 1. 前置工作（P0）

### P0-1 CM_PROPERTY 宏（裁定 ㉓）

- `src/common/types/cpp/property_macro.h`：纯头仅 `<utility>`，进 `fly_common_types` 库的 hdrs（该库为公共底座，无重头依赖；**不放 container**——container_aliases.h 聚合全部容器头）。
- 宏体（C++20 abbreviated function template）：

```cpp
#define CM_PROPERTY(attr_name)                                              \
    auto get_##attr_name() const { return attr_name##_; }                   \
    auto& get_ref_##attr_name() { return attr_name##_; }                    \
    const auto& get_cref_##attr_name() const { return attr_name##_; }       \
    void set_##attr_name(const auto& value) { attr_name##_ = value; }       \
    void set_##attr_name##_move(auto&& value) { attr_name##_ = std::move(value); }
```

- 测试 `src/common/types/tests/property_macro_test.cpp`：五接口语义（值拷贝隔离 / get_ref 原地修改 / const 对象 get+get_cref / set 拷贝源完整 / set_move 非 SSO 字符串缓冲区指针转移 + moved-from 空）。

### P0-2 geometry 通用子模块（裁定 ㉑，D28 建议值）

```
src/container/geometry/
├── cpp/
│   ├── BUILD            # cc_library fly_geometry（deps: fly_container_aliases + fly_serialization）
│   └── geometry_types.h # 全模板头文件
└── tests/
    ├── BUILD
    └── geometry_types_test.cpp
```

- `CMPoint<T>{T x, y}`、`CMRect<T>{T x_low, y_low, x_high, y_high}`（左下+右上，半开区间归属判定配套）、`CMPolygon<T>{CMVector<CMPoint<T>>}`。
- 运算：`area()`（int64/double 特化）、`bbox()`、`overlaps(rect, rect)`（半开区间）、`contains(rect, point)`（左闭右开）。
- 序列化：`FLY_SERIALIZE` 支持三类型 × 实例化集合 {int32, int64, double}。
- include 路径 `<container/geometry/cpp/geometry_types.h>`。

### P0-3 lefdef 接入 Bazel（硬前置，src/lefdef README 演进三步走第 1/2 步）

**现状**（2026-09-10 探明）：git 签入 182 个 .cpp/.hpp + gperf 关键字表（`def/def/def_keywords.cpp`、`lef/lef/lef_keywords.cpp`）；**bison 生成物未签入**——`def/def/def.tab.cpp`/`def.tab.h` 为本地构建残留（被 gitignore 的 *.o/*.a 同伴），lef 侧 `lef.tab.cpp` 本地不存在（lef 侧从未构建过）。

**方案：bazel 直接编译源码**（不走预编译 .a——本库是持续魔改的 fork，每次改动重签 .a 不可持续；dev-rules §4.3 的预编译模式适用于不变的上游，此处不适用）：

1. **生成 bison 产物并签入**：分别在 `src/lefdef/def`、`src/lefdef/lef` 跑上游 `make -j1`（README 约定；改 .hpp 后须 make clean），将 `def/def/def.tab.cpp`、`def/def/def.tab.h`、`lef/lef/lef.tab.cpp`、`lef/lef/lef.tab.h` **签入 git**（.gitignore 现忽略 .o/.a 不影响 .cpp/.h；bison 产物随 .y 变更手动再生成——OpenROAD 第三方 lefdef 同款做法）。签入后清理本地 .o/.a 残留不必（已被 ignore）。
2. **`src/lefdef/BUILD`**（模块根，两个 library，**无 export/.so/模块注册六步**——纯静态库经 design 模块 deps 引入，无 Python 绑定）：

```python
# fly_lefdef_lef：lef/lef/*.cpp（22 个：lefrReader/lefi*/lef.tab.cpp/lef_keywords.cpp）
#   hdrs = lef/lef/*.hpp + lef/include（对外头 lefrReader.hpp 等）
# fly_lefdef_def：def/def/*.cpp（33 个：defrReader/defi*/def.tab.cpp/def_keywords.cpp）
#   hdrs = def/def/*.hpp + def/include
# 共同：strip_include_prefix = "/src"，copts = ["-std=c++20", "-O2"] + 上游告警抑制（-w 或精确 -Wno 列表，实施时按实际告警定），不定义 zlib 宏（defzlib/clefzlib 系不接入）
# 排除：TEST/bin/defrw/defwrite/defdiff/perf 等工具目录一律不进 srcs
```

3. **正确性回归基线**（演进第 2 步）：`src/lefdef/tests/`（或 qa 前置单测）跑上游回显金标——`def/TEST/complete.5.8.def` 与 `lef/TEST/complete.5.8.lef` 全量解析计数断言（层/macro/pin/via/DIEAREA/PINS 计数与上游 .au 金标对齐；不做字节级回显，做结构化计数断言——轻量且防回归）。
4. lefdef 源码**零改动**（本批次只接入不改内部）；后续魔改走演进第 3 步单独立项。

**T2 实施记录（2026-09-10，供 T3-T6 遵循的实测结论）**：
- bison 产物已生成并解除忽略（def.tab.cpp/.h、lef.tab.cpp/.h untracked 待审查后签入）；`src/lefdef/BUILD` 两 library（fly_lefdef_lef 23 源 / fly_lefdef_def 33 源，copts -w，零 zlib 宏）；上游源码零改动 C++20 直接编译通过。
- **解析初始化序列（T4/T5 适配层红线）**：LEF 侧必须 `lefrInitSession()` + `lefrSetRegisterUnusedCallbacks()`（仅 `lefrInit()` 在 VIA 段后 SIGSEGV，实测定位）；DEF 侧加 `defrSetRegisterUnusedCallbacks()` 防御。
- **DEF 段头声明计数不可信**（金标声明值与实际条目普遍不一致，如 VIAS 声明 6 实际 11）——S4/S4b/S5 解析与断言一律以回调实际计数为准，不得依赖声明值。
- **defiBox 的 xl/yl/xh/yh 是「前两点」向后兼容赋值而非包围盒**（defiSite.cpp addPoint 注释）——S4 的 DIEAREA 必须经 `getPoint()` 取完整点集自行聚合包围盒。
- lef 对外头在 lef/lef/ 下（设计稿所写 lef/include 不存在）。基线测试 `//src/lefdef/tests:baseline_parse_test` 4 用例（双印证链核定计数）。

---

## 2. design 模块数据结构（`src/emir/design/cpp/ds_types.h/.cpp`）

命名遵循总流程裁定 12（DS 前缀类名 / ds_ 前缀函数）；存储形态遵循裁定 ㉒（小对象直接存、大体量/注入用 ptr、**禁止裸指针**——拥有用 CMUniquePtr/CMSharedPtr，观察用引用）。

### 2.1 Stack（独立类型，裁定 ⑭——不进 DSDesign，独立对象持久化）

```cpp
class DSLayer {
    CMString name_;                 // 层名（如 M1、VIA1）
    uint8_t  type_;                 // 布线层 / 切割层（enum DSLayerType）
    uint8_t  direction_;            // 水平 / 垂直 / 无（enum，布线层用）
    int32_t  default_width_;        // 默认线宽（DBU）
    int32_t  pitch_;                // 布线 pitch（DBU）
    CMVector<int32_t> spacing_;     // 间距表（DBU）
    int64_t  min_area_;             // 最小面积（DBU²）
    // CM_PROPERTY 应用（CM_PROPERTY(name) 等逐字段声明）
};
class DSStack {
    CMString dbu_basis_;            // DBU 基准说明（µm 换算系数 dbu_per_micron_）
    int32_t  dbu_per_micron_;       // 1 µm = N DBU
    int32_t  manufacturing_grid_;
    CMVector<DSLayer> layers_;      // 自底向上堆叠顺序（routing/cut 交错，stack 顺序即索引序）
    CMUnorderedMap<CMString, uint32_t> layer_index_;   // name → 下标（运行时索引，不序列化）
    uint32_t find_or_add(...); ...  // 构建期接口
};
// layer id = layers_ 下标（uint32）；邻接关系经堆叠顺序导出（相邻 routing-cut-routing）
```

### 2.2 cell / pin（裁定 ⑯/⑰/⑲/⑳）

```cpp
class DSPin {                        // 简化 pin（⑰）：仅基础属性，序列化
    CMString name_;
    uint8_t  type_;                  // signal / power / ground（enum DSPinType）
    uint8_t  direction_;             // input / output / bidirectional（enum DSPinDirection）
};
class DSCell {                       // 始终单一结构（⑯）
    CMString name_;
    uintString library_name_;        // lib 来源库名（可空 = lef 无 lib 对应）
    double   width_, height_;        // µm（lef SIZE；按 DBU 换算规则实施时定：直接存 DBU int64 亦可——取 DBU int64）
    int64_t  origin_x_, origin_y_;   // DBU（lef ORIGIN）
    CMString class_;                 // CORE/BLOCK/...
    CMString site_;
    CMVector<DSPin> pins_;           // 简化 pin 集（lib+lef 并集，⑯ merge 产出）
    CMVector<CMRect<int32_t>> obs_;  // 禁布区几何（D19 入库；逐层 → 带 layer id 的结构 CMGeometryRef，见 2.4）
    bool     is_fake_;               // ⑲ fake cell 标记（fake 不在 pins_，无 pin）
    // 两个不序列化字段（⑰/⑱）——FLY_SERIALIZE 字段表不含：
    CMSharedPtr<DSPinTables>   pin_tables_;      // lib 功耗/时序表（运行时注入）
    CMSharedPtr<DSPinGeometry> pin_geometry_;    // lef 逐层 pin 几何（运行时注入）
};
```

- pin 表数据集（独立对象）：`DSPinTables { CMUnorderedMap<uint32_t cell_id, CMVector<CMLookupTable*>> internal_power_ / timing_ ... }`——**结构实施期细化**，原则：按 cell id 组织、只存 lib 侧表数据。
- pin 几何数据集（独立对象）：`DSPinGeometry { CMUnorderedMap<uint32_t cell_id, CMVector<CMGeometryRef>> }`——CMGeometryRef = { layer id, 几何变体 }。
- fake cell（⑲/⑳）：**不在此处生成**（S5a 机制），DSCell 预留 `is_fake_` 与 DSDesign 单独集合字段，本批次只需结构就位。

### 2.3 via cell（裁定 ⑩/⑫）

```cpp
class DSViaCell {
    CMString name_;                  // 登记名：tech/cell lef 原名；DEF 来源 = "design_name::原名"（⑫）
    uint32_t bottom_layer_id_, top_layer_id_;
    CMVector<CMRect<int32_t>> cut_rects_;       // 切割层矩形集
    CMVector<CMRect<int32_t>> bottom_enclosure_, top_enclosure_;  // 上下层包围矩形
};
// 独立编号空间（非 cell）；VIARULE 生成式 via 在解析期按参数展开为 DSViaCell（D13）
```

### 2.4 block（S4 产出）

```cpp
class DSBlock {
    CMString design_name_;           // DEF 的 DESIGN 名
    CMString def_path_;              // 来源 DEF 完整路径（可追溯，dev-rules §7）
    CMRect<int64_t> die_area_;       // DIEAREA 包围盒（DBU）
    double   def_units_per_micron_;  // DEF UNITS DIST MICRONS（DBU 换算系数）
    CMVector<DSPort> ports_;         // port 集
};
class DSPort {                       // 本质是 block cell 的 pin（进 pin id 空间）
    CMString name_;
    uint8_t  type_;                  // signal / power / ground
    uint8_t  direction_;
    uint8_t  placement_status_;      // FIXED/COVER/PLACED
    CMVector<CMGeometryRef> geometry_;   // port 逐层几何（block 局部坐标）
};
```

### 2.5 DSDesign 容器（裁定 ⑬；构建时序见评审稿 §1.3）

```cpp
class DSDesign {
    // —— 序列化字段 ——
    CMVector<DSCell> cells_;                     // 正常 cell（lef macro + block cell 同空间，⑯）
    CMVector<uint32_t> fake_cell_ids_;           // fake cell 单独字段（⑳，指向 cells_ 内 is_fake_ 条目）
    CMVector<DSViaCell> via_cells_;              // 权威表（⑫：tech lef + cell lef + 各 DEF 全集）
    CMUnorderedMap<uint32_t, CMString> lib_link_;// cell id → lib cell 名（lib 关联；lib 独有 cell 不入 id 空间）
    CMVector<DSBlock> blocks_;                   // block 定义表（S4）
    // namemap（裁定 ②）：map name→id + vector id→name（cell/pin/via cell/block 四类；
    //   pin namemap 键 = "cell_name/pin_name" 组合；instance/net 本批次无）
    CMUnorderedMap<CMString, uint32_t> cell_name_to_id_;   CMVector<CMString> cell_id_to_name_;
    ... (pin / via_cell / block 同构四对)
    // —— 运行时专用字段（⑰/⑱，不序列化）——
    CMSharedPtr<DSPinTables>   pin_tables_;
    CMSharedPtr<DSPinGeometry> pin_geometry_;
    // —— 接口（后续功能向此类增强，⑬）——
    const DSCell& get_cell(uint32_t cell_id) const;   // 统一入口：从 pin_tables_/pin_geometry_ 指针注入 cell 的两个不序列化字段（非 const 版本同）
    ...查询/索引接口随迭代增强
};
```

- 序列化：全部经 `FLY_SERIALIZE`（字段表显式排除运行时字段）；`write_object` 由 flow 冻结前一次完成（评审稿 §1.3 时序）。
- **对象组织**：design db 内对象 = DSDesign（单对象）+ DSStack（单对象）+ DSPinTables（单对象）+ DSPinGeometry（单对象）；S5a 后续追加块数据对象。

### 2.6 消息注册（D21 前缀 DSGN，`ds_register_msg.py`）

| id | 级别 | 场景 |
|----|------|------|
| DSGN::0001 | WARN | 跨文件重复 macro（保留首份抛弃后续 + 两处来源路径，同 LIBR::0001 语义） |
| DSGN::0002 | WARN | lef cell 有 / lib cell 无（EMIR 无电流模型，逐 cell 名清单汇总一条） |
| DSGN::0003 | WARN | lib cell 有 / lef cell 无（未覆盖清单汇总） |
| DSGN::0004 | WARN | cell 的 pin 集合 lib/lef 不一致（逐 cell：缺哪些 pin 名） |
| DSGN::0005 | WARN | via cell 重名冲突（保留首份 + 两处来源，lef 侧；DEF 侧带前缀天然隔离，同 DEF 内重名才报） |
| DSGN::0006 | WARN | DEF 内 PINS 段 port 名重复 / DIEAREA 缺失兜底（取 PINS 包围盒） |

可 raise 场景（dev-rules §7 仅两类）：文件不可读；语法错误（含 lef 间 DBU 不一致——裁定 D15）。

---

## 3. 解析适配层（C 回调 → 领域对象）

### 3.1 `ds_lef_adapter.h/.cpp`（S1 + S2）

- S1 tech lef 模式：装配 lefrReader 回调——LAYER（type/direction/width/pitch/spacing/area → DSLayer）、VIA/VIARULE（→ DSViaCell，tech 级）、UNITS（DATABASE MICRONS → DSStack.dbu_per_micron_）、MANUFACTURINGGRID。仅布线层+切割层收录（D16），implant 等跳过计数。
- S2 cell lef 模式：MACRO（SIZE/ORIGIN/CLASS/SITE/PIN/OBS → DSCell + DSPin + obs 几何）、MACRO 内 VIA（→ DSViaCell）、VIARULE（参数化 → 展开 DSViaCell，D13）。
- 两模式一套适配类两个入口函数：`ds_parse_tech_lef(path, DSStack&, CMVector<DSViaCell>&)`、`ds_parse_cell_lef(path, DSStack&, 中间产物结构&)`。
- 中间产物以 name 为键（裁定 ①：并行任务各自产出，全局汇总统一编号）。
- lef 间 DBU 不一致 → raise（D15）；pin USE（POWER/GROUND）→ DSPin.type_。

### 3.2 `ds_def_adapter.h/.cpp`（S4 + S4b）

- S4 头扫描模式：`defrSetSkipComponents` + 跳过 NETS/SPECIALNETS 段；回调 DIEAREA、PINS（port：名/use/direction/place 状态/逐层几何）、UNITS（DIST MICRONS）、DESIGN 名、VIAS（→ 中间 via 集）。产出 DSBlock + 中间 via 定义集。
- S4b via 模式：与 S4 同遍（实施选择：同一回调集一次读取，VIAS 段天然在前头部区）——vi as 登记 `design_name::via_name`（⑫）→ 全局汇总 merge 进 DSDesign.via_cells_（含 tech/cell lef 侧既有条目，重名保留首份 + DSGN::0005）。
- **实现注意**：S4 与 S4b 在实现上可同遍回调，但语义上保持两个产出通道（block 定义 / via 定义集），全局汇总分别处理。

---

## 4. flow 编排（py 七文件，`src/emir/design/py/`）

| 文件 | 内容 |
|------|------|
| `ds_export.py` | `_fly_emir_design.so` 唯一导入点（EXDSDesign/EXDSStack/EXDSViaCell… + ds_parse_* + merge 函数） |
| `ds_register_msg.py` | DSGN::0001-0006 注册（全局区直书） |
| `ds_db.py` | `DesignDb(Database)`（role="design"）+ `build_design_db(name, def_path, lef_paths, lib_db, settings: dict, alpha: dict)`（⑦；`@register_flow(EMIRProject)`）+ UserDoc header + Schema 校验器（文件存在性校验前置） |
| `ds_flow.py` | 阶段链装配（**无 MapReduceJob**，裁定 ①）：S1 单 task → S2 每 lef 文件一 task + 汇总 task → {S3 merge task（读 lib db）∥ S4+S4b 每 DEF 一 task + 汇总 task} → freeze task（依赖 DSDesign/DSStack/DSPinTables/DSPinGeometry 四对象写完） |
| `ds_utils.py` | worker 侧解析任务函数（内部不导出） |
| `ds_functions.py` | `load_design(db)` / `load_design_stack(db)` / `load_design_with(db, pin_tables=False, pin_geometries=False)`（⑱ 统一加载封装 load+set） |
| `__init__.py` | 聚合导出（dev-rules §2） |

- S3 merge：读 `load_lib_library(lib_db)`（lib_functions API）↔ S2 汇总的 cell 集——单一 cell 记录整合 lib 字段（⑯），非 1:1 场景 DSGN::0002/0003/0004 提醒不 crash（⑯）；**本阶段在 S4/S4b 并行轨之前启动判断其依赖：S2 汇总完成后即可**。
- freeze 语义：S4b 汇总完成 + 四对象落盘 → freeze design db。
- `settings` 首版空 dict 占位；`alpha` 键表首版仅 `layer_density_weights`（保留默认全 1，本范围 S8 未实施、仅登记键）——键表随阶段实施逐步激活。

## 5. 模块注册六步（AGENTS.md）

1. `src/emir/design/cpp/BUILD`：cc_library fly_emir_design_ds_types + cc_shared_library
2. `src/emir/design/export/BUILD`：cc_binary linkshared（dynamic_deps）
3. `src/main/cpp/BUILD`：fly 目标 deps + dynamic_deps 追加
4. `src/main/cpp/main.cpp`：setup_sys_path 追加 `import _fly_emir_design`
5. `fly.sh` do_install：install 循环追加 design
6. `src/emir/__init__.py` 追加 `from emir.design import *`（emir 聚合，main.cpp 已 import emir）

## 6. 测试计划

| 层 | 内容 |
|----|------|
| 单测 P0 | property_macro_test（五接口）；geometry_types_test（三类型 × 三实例化 + area/bbox/半开区间判定 + 序列化往返） |
| 单测 lefdef | 接入基线：complete.5.8.lef/def 全量解析结构化计数断言（层/macro/pin/via/DIEAREA/PINS 计数对齐上游金标 .au） |
| 单测 ds_types | 序列化往返（DSDesign/DSStack/DSViaCell/DSPinTables/DSPinGeometry）；专用字段不序列化（⑱：注入后 write→load 字段为空） |
| 单测 适配层 | 小 lef：层表/宏/pin 类型方向/obs；小 def：DIEAREA/port/via（含 design_name:: 前缀）；fake cell 结构字段就位断言（生成机制 S5a 实施） |
| QA e2e | `qa/emir/test_emir_project_design.py`：多 lef（tech + cells）+ 多 DEF（两个 block：子 block DIEAREA/PINS、父 block）→ `build_design_db` → wait_frozen → load 断言：Stack 层序与 DBU、cell 集与简化 pin、lib merge（含不匹配 DSGN 提醒断言）、block/port 表、via cell 权威表（tech + lef + `design_name::` 前缀 DEF via）、namemap 双向、load_design_with 按需注入、load_project 还原 |
| 真实数据验证 | Nangate45（tech lef + 全 cell lef）+ ISPD 2018 sample def（.work 下）——`fly script.py` 烟测：全量解析计数与抽样断言（不进 QA 常规，人工验证记录进本文档实施记录） |

QA 数据：小规模自制 lef/def 摘要（由真实文件裁剪）入 `qa/emir/data/design/`；大体量真实文件仅 .work 本地验证（no-tmp 规则）。

## 7. 实施批次（coder 委托任务拆分，每批 TDD + ./fly.sh test 通过 + 不 commit）

| 批次 | 内容 | 验收 |
|------|------|------|
| T1 | P0-1 CM_PROPERTY + P0-2 geometry 子模块 | 两套单测绿 |
| T2 | P0-3 lefdef 接入（bison 产物生成签入 + BUILD + 基线单测） | complete.5.8 计数断言绿；`./fly.sh build //src/lefdef:...` |
| T3 | ds_types 数据结构 + 序列化 + export 骨架 | 序列化往返单测绿 |
| T4 | ds_lef_adapter（S1/S2）+ 单测 | 小 lef 断言绿 |
| T5 | ds_def_adapter（S4/S4b）+ 单测 | 小 def 断言绿 |
| T6 | py 七文件 + flow + 模块注册六步 + QA e2e + 真实数据烟测 | ./fly.sh build //src/main/cpp:fly + install + runqa qa/emir 全绿 |

依赖序 T1→T2→T3→{T4,T5}→T6；T4/T5 可并行（同一 coder 串行亦可）。

## 8. 实现红线（coder 必须遵守）

1. **不 commit / 不 push**——实现完毕由主智能体代码审查后统一处理；
2. 一律 `./fly.sh`（build/test/buildonly），禁裸 bazel；
3. dev-rules 全部约束（七文件 / 命名 / 异常语义仅两类可 raise / 消息系统 / 职责边界）；
4. 裁定 ㉒ 存储形态：逐字段 ptr vs 对象分析；**禁止裸指针**；
5. lefdef 上游源码零改动（本批次只接入）；
6. 测试数据不污染仓库根（.work/ 下）；QA 临时目录走 `qa_tmp`；
7. 单测 case 预算 <10s（既有准则）；
8. 遇设计与现实冲突（lefdef API 细节、序列化宏限制等）：记录到实施记录节，不擅自偏离设计。

---

## 9. 实施记录（2026-09-10 完成）

### 批次交付（coder 子智能体 T1-T6，全部未 commit 留待统一处理）

| 批次 | 内容 | 验证 |
|------|------|------|
| T1 | CM_PROPERTY（common/types）+ geometry 子模块（container/geometry，CMPoint/CMRect/CMPolygon/CMGeometryRef） | 6+13 用例 |
| T2 | lefdef 接入 Bazel（bison 产物生成解除忽略；fly_lefdef_lef 23 源 / fly_lefdef_def 33 源，上游零改动）+ 基线单测 | 4 用例（双印证链计数） |
| T3 | ds_types（十类 DS* 结构 + FLY_SERIALIZE + CM_PROPERTY）+ export 骨架 | 8 用例 |
| T4 | ds_lef_adapter（S1/S2） | 6 用例 |
| T5 | ds_def_adapter（S4+S4b 同遍双通道） | 5 用例 |
| T6 | ds_merge（S2/S4 汇总 + S3 merge_lib）+ py 七文件 + as_task 阶段链 + 模块注册 + QA e2e + 真实数据烟测 | 3 用例 + QA 2 case + 烟测计数对照 |

### 真实数据烟测（ISPD 2018 官方样例 Nangate45 + Tilos Nangate45 tech lef，.work/design_test_data/）

8 项计数对照全部一致：Nangate45 tech lef（层 22 收录 19 跳过 MASTERSLICE/OVERLAP、VIA 27、VIARULE 19、DBU 2000）；ispd18 lef（层 18 收录 17、VIA 22 双路径重名保留首份 DSGN::0005×22、MACRO 16、grid 1 DBU）；ispd18 def（block 登记、COMPONENTS 22/NETS 11 parser 层真跳过零产出）。DSGN 兜底消息路径全部实测触发。

### 代码审查（主智能体，2026-09-10）

**发现并已修复 2 项缺陷（修复后全量复验通过）**：
1. ds_def_adapter：`def_units` 缺省 0 的除零风险——DEF 规范 UNITS 语句可选、缺省 100，无 UNITS 的文件在 DIEAREA/PINS 换算时除以 0；修复为初始 100。
2. ds_def_adapter / ds_lef_adapter：回调内异常（层引用缺失 raise）穿越读取器时 FILE* 与 parser session 泄漏；修复为 try/catch 清理后重抛。

**审查确认的关键实现红线（T2-T5 实施记录所列）全部落实**：lefrInitSession 后注册回调、DIEAREA 经 getPoint() 聚合、DEF 段头声明计数不依赖、PORT 块级几何优先、⑫ design_name:: 前缀、⑰/⑱ 不序列化字段与 get_cell 指针注入（单测专项断言）、㉒ 全 CMSharedPtr 无裸指针、D15 仅四类可 raise。

**备注（非缺陷，记录在案）**：
- merge_lib 的功耗/时序表提取为 cell 级聚合（pin 粒度折叠）——后续 ⑩ power db 立项时按需细化到 pin 粒度。
- block cell 会计入 DSGN::0002（lef 有 lib 无）名单——提醒噪音，语义无害。
- pin id 平铺空间允许空洞（重复 macro 抛弃时）——register_pin 按 id 落位设计保证无错位无碰撞，D1 稠密性未被破坏性违反。

### 待后续批次（S5a 起）
- DEF 第一段（COMPONENTS 全量 + 网名扫描）与 fake cell 生成机制（⑲/⑳ 结构已就位）
- S5b 网内容解析（section skip 家族已探明：SkipNetDetails/NetNameOnly 可直接用）
- instance/net namemap（大体量，裁定 ②）与 hierarchy tree（⑮ 四接口）
