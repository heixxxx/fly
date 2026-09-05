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

## 演进路线（魔改三步走，每步可回归验证）

1. **原样接 Bazel**：给 lef 与 def 各建 `cc_library` 目标，bison/flex 生成规则写进 BUILD，基线编译纳入 `./fly.sh` 体系。这一步不改任何源码。
2. **建立解析正确性回归基线**：上游 TEST 数据 + 大规模真实 LEF/DEF 文件做"解析结果快照"测试（读入 → 导出 → 对比）。这是后续所有魔改的安全网。
3. **动内部**。性能优先：每记号/对象的小对象内存分配、逐行回调的虚函数开销、字符串拷贝；语法文件（`lef.y` 约 300KB、`def.y` 约 6900 行）风险最高，尽量不动语法层。API 侧保留原 C 风格回调接口做兼容层，其上新增适配 fly 风格（CM* 类型、统一错误处理）的现代 C++ 接口，避免一次性推翻。
