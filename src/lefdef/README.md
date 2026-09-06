# src/lefdef — 基于 Cadence LEF/DEF 解析器的 fork 深度改造模块

fly 仓库自有模块。基于 Cadence 开源（Apache 2.0 许可证）的 LEF/DEF 解析器 6.0_62-p004 版本 fork，作为 EMIR（电迁移分析）工具的基础输入库进行持续的深度改造：增强解析性能、按 fly 风格优化 API。LEF（Library Exchange Format，库交换格式）描述工艺与单元物理信息，DEF（Design Exchange Format，设计交换格式）描述布局布线结果，二者是物理设计数据交换的行业事实标准。

**fork 声明**：本目录代码已脱离上游 vendor 语义，按 fly 仓库规范管理（构建走 `./fly.sh`、改动过全量测试、崩溃零容忍）。干净的上游 6.0_62-p004 基线永远可通过 git 历史回溯（vendor 提交 f1c338b，当时位于 `third_party/lefdef/`）；将来上游发布新补丁时，拿该基线做 diff 逐项人工挑选。

## 版本与来源

- **fork 基线**：`6.0_62-p004`（2025 年 9 月发布，Si2 官方最新版本；上一代主线为 5.8，GitHub 上的公开镜像均停留在 5.8，6.0 无任何镜像，仅能从 Si2 官网获取）
- **官方下载页**：<https://si2.org/lef-def-downloads/>（页面要求填表单领限时链接，实际存在直链）
- **基线包直链**：<https://si2.org/wp-content/uploads/2025/09/LEF-DEF_6.0_62-p004.zip>
- **出处证明（Certificate of Origin）**：基线 zip 内附 `COO_Document_2025-03-18_165805.pdf`（未随源码入库）
- **许可**：Apache 2.0。深度改造不免除保留许可与版权声明的义务，`lef/LICENSE.TXT` 与 `def/LICENSE.TXT` 原样保留；对本仓库的修改部分同样按 Apache 2.0 对外

## 目录结构

上游两个源码包各带一份相同的 `lefdefReadme.txt`（解压到同一目录时相互覆盖，此处保留根下一份）：

```
lefdefReadme.txt       上游原始安装说明（仅作基线参考，改造后行为以上游文档为准的部分会失效）
lef/                   LEF 分发根（顶层 Makefile 递归构建各子目录）
  lef/                 解析器核心（lef.y 语法 + lefrReader 读取 + lefi* 数据对象）
  clef/                C 语言封装接口
  lefrw/               命令行读取工具
  lefwrite/           LEF 写出库与工具
  lefdiff/ bin/        文件对比工具（bin/lefdefdiff 为上游打包自带的 shell 包装脚本）
  lefzlib/ clefzlib/   zlib 压缩读写支持
  TEST/                上游自带测试数据（complete.5.8.lef）
def/                   DEF 分发根（结构同上：def.y + defrReader + defi* 核心，cdef、defrw、defwrite、defdiff、defzlib、TEST）
```

基线版本宏：`lef/lef/lefrData.hpp` 中 `#define CURRENT_VERSION 6.0`。

## 构建（上游 Makefile 方式，Bazel 集成前的临时手段）

依赖：`bison`（语法分析器生成工具）、`flex`（词法分析器生成工具）、GNU Make。

```bash
cd src/lefdef/lef && make -j1   # 必须单线程：并行 make 存在 lef.tab.h 生成时序竞争
cd src/lefdef/def && make -j1
```

产物落在各自 `bin/` 下：`lefrw`（LEF 读取）、`lefwrite`、`defrw`（DEF 读取）、`defwrite`、`lefdiff`/`defdiff`/`lefdefdiff`（对比工具）。上游自带测试数据可用于解析烟测：`lefrw TEST/complete.5.8.lef`、`defrw TEST/complete.5.8.def`。

基线已在本仓库开发环境（WSL2，gcc 12，bison 3.5.1，flex 2.6.4）验证编译与烟测通过。

## 性能优化记录（2026-09-06，基于 callgrind 实测热点）

测试口径：Galaxy.def（442MB，91 万 components / 33.7 万 nets / 2.66 万 specialnets，IC Compiler II 写出），全空回调完整解析（defrw_perf），g++ -O3，5 轮平均；正确性回归 = defrw 回显输出逐字节比对（complete.5.8.def / test_escape.def）+ Galaxy 对象计数校验。

| commit | 优化 | 耗时 | 单项收益 |
|---|---|---|---|
| （基线） | — | 4.82s | — |
| c14270b | GETC 内联化（消除词法热路径跨编译单元调用） | 4.36s | +9.5% |
| 1b45a1e | pv_deftoken 延迟复制（仅 ';' 结尾 token 保存全文） | 4.08s | +6.4% |
| 52e459b | dumb_mode 关键字识别首字符分派（strcmp 链 30 次→1-2 次/token） | 3.63s | +11% |
| 7768949 | 连接字符串 arena 池（消除逐连接 malloc/free，约 400 万次） | 3.47s | +4.4% |
| 36b6572 | 纯整数 token 快速解析（替代 strtol） | 3.40s | +2% |

累计：**4.82s → 3.40s（快 29.5%）**；峰值内存 156MB → 106MB（消除分配器元数据与碎片）；callgrind 总指令数 -24%。外部魔改版的 gperf/section-skip 优化在 dumb_mode 段（COMPONENTS/NETS/SPECIALNETS）不执行、对真实负载无收益（同机对照实测零差异），本组优化针对实测热点故收益显著。

### 12GB 规模验证（2026-09-06）

测试文件：`Galaxy_12GB.def.test`（12.0GB，合法 DEF，由 444MB 修复版全段等比 ×27 合成：2456.8 万 components、909.3 万 nets、68.0 万 specialnets、2644 万 connections、8909 万 wire 路径；解析 status=0、七项计数精确 ×27）。四组对照各 2 轮：

| 组合 | 耗时 | 峰值内存 |
|---|---|---|
| 原版 + glibc | 127.1s | 151.5MB |
| 原版 + tcmalloc | 107.5s | 119.3MB |
| 优化版 + glibc | 91.0s | 102.5MB |
| 优化版 + tcmalloc（最速） | **78.6s** | 114.3MB |

结论：本仓库优化使同分配器下快 28.4%（127.1→91.0s）、峰值内存降 32.3%（151.5→102.5MB）；叠加 tcmalloc 后最速 78.6s（累计快 38.2%，吞吐约 153MB/s）。峰值内存与文件体积无关（流式解析，由单个最大记录——VDD/VSS 巨型网决定），原版与优化版在 442MB 与 12GB 文件上峰值一致。

### 三方对照与外部魔改版收益修正（12GB，同机同 -O3）

| 版本 | 耗时 | 峰值内存 |
|---|---|---|
| Si2 纯原版（6.0_62-p004） | 128.4s | 151.4MB |
| 外部魔改版（lefdef-6.1-mod 原样） | 122.8s | 151.8MB |
| 本仓库当前版（魔改增强 + 五项优化） | 91.0s | 102.5MB |
| 本仓库当前版 + tcmalloc | 78.6s | 114.3MB |

外部魔改版相对纯原版仅快 4.4%。其宣称的 22.4% 提速为编译等级差异假象（上游 Makefile 在 Linux 下默认无优化标志，即 -O0 基线对 -O3 优化版）。机制根源：魔改版的主力优化 gperf 关键字查找作用于 smart 模式的关键字表查询，而 12GB 文件 99% 的数据量（COMPONENTS/NETS/SPECIALNETS 数据区）解析于 dumb_mode（词法器跳过关键字表、按 strcmp 链机械切分，用于消解数据名与保留字同名的歧义），gperf 路径在真实负载中几乎不执行。其真正有价值的部分是选择性解析 API（section skip / NetNameOnly，"少解析数据"的收益）与 NetPartialPathCbk 流式内存控制，两者已随提交迁移保留。

### 第二轮优化实验（对象生命周期清理，负结果）

针对 12GB 剖析中占比约 20% 的 defiPin/defiComponent 生命周期函数（clear/Destroy），实施了两项实验：defiComponent 连接字符串 arena 化、defiPin props 容器原地清空 + antennaModel 对象池复用。实测 442MB 上 3.40s→3.54s、12GB 上 78.6s→79.9s，**无收益反而退化 2-3%**，已全部回退。原因：tcmalloc/glibc 的线程缓存使这类小对象堆操作接近免费，池的重置管理反而引入开销。解析器本体（词法+语法+分配）在本代码基线上已无低风险收益空间，当前最速组合（优化版 + tcmalloc）即为实际最优状态。

注意事项：上游 Makefile 的 `.cpp.o` 规则无头文件依赖跟踪，**修改 .hpp 后必须 `make clean` 全量重建**，否则新旧对象布局混链会 ODR 违例（实测表现为成员读位错乱段错误）。defiPath 的逐元素 malloc（对象级生命周期，池化需 freelist 复用对象）与 defyyparse 状态机为后续可选优化。

## 演进路线（魔改三步走，每步可回归验证）

1. **原样接 Bazel**：给 lef 与 def 各建 `cc_library` 目标，bison/flex 生成规则写进 BUILD，基线编译纳入 `./fly.sh` 体系。这一步不改任何源码。
2. **建立解析正确性回归基线**：上游 TEST 数据 + 大规模真实 LEF/DEF 文件做"解析结果快照"测试（读入 → 导出 → 对比）。这是后续所有魔改的安全网。
3. **动内部**。性能优先：每记号/对象的小对象内存分配、逐行回调的虚函数开销、字符串拷贝；语法文件（`lef.y` 约 300KB、`def.y` 约 6900 行）风险最高，尽量不动语法层。API 侧保留原 C 风格回调接口做兼容层，其上新增适配 fly 风格（CM* 类型、统一错误处理）的现代 C++ 接口，避免一次性推翻。
