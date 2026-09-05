# third_party/lefdef — Cadence LEF/DEF 解析器源码

Cadence 开源（Apache 2.0 许可证）的 LEF/DEF 文件解析与生成库，作为 fly 仓库中 EMIR（电迁移分析）工具的基础输入库。LEF（Library Exchange Format，库交换格式）描述工艺与单元物理信息，DEF（Design Exchange Format，设计交换格式）描述布局布线结果，二者是物理设计数据交换的行业事实标准。

## 版本与来源

- **版本**：`6.0_62-p004`（2025 年 9 月发布，Si2 官方最新版本；上一代主线为 5.8，GitHub 上的公开镜像均停留在 5.8，6.0 无任何镜像，仅能从 Si2 官网获取）
- **官方下载页**：<https://si2.org/lef-def-downloads/>（页面要求填表单领限时链接，实际存在直链）
- **本版本直链**：<https://si2.org/wp-content/uploads/2025/09/LEF-DEF_6.0_62-p004.zip>
- **出处证明（Certificate of Origin）**：<https://si2.org/wp-content/uploads/2025/09/LEF-DEF_6.0_62-p004.zip> 解压后内附 `COO_Document_2025-03-18_165805.pdf`（未随源码入库）
- **许可**：Apache 2.0，见 `lef/lef/LICENSE.TXT` 与 `def/def/LICENSE.TXT`

## 目录结构

上游两个源码包各带一份相同的 `lefdefReadme.txt`（解压到同一目录时相互覆盖，此处保留根下一份）：

```
lefdefReadme.txt       上游原始安装说明
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

源码内版本宏：`lef/lef/lefrData.hpp` 中 `#define CURRENT_VERSION 6.0`。

## 构建（上游 Makefile 方式，尚未集成 Bazel）

依赖：`bison`（语法分析器生成工具）、`flex`（词法分析器生成工具）、GNU Make。

```bash
cd third_party/lefdef/lef && make -j1   # 必须单线程：并行 make 存在 lef.tab.h 生成时序竞争
cd third_party/lefdef/def && make -j1
```

产物落在各自 `bin/` 下：`lefrw`（LEF 读取）、`lefwrite`、`defrw`（DEF 读取）、`defwrite`、`lefdiff`/`defdiff`/`lefdefdiff`（对比工具）。自带测试数据在各自 `TEST/` 目录，可用 `lefrw TEST/complete.5.8.lef`、`defrw TEST/complete.5.8.def` 做解析烟测。

已在本仓库开发环境（WSL2，gcc 12，bison 3.5.1，flex 2.6.4）验证编译与烟测通过。

## 集成计划

当前仅做源码纳管（vendor），未写 Bazel BUILD 目标、未接入 `src/` 任何模块。EMIR 工具开发启动时，参照 `third_party/sqlite/BUILD` 的做法补 `cc_library` 目标（lef/def 各一个，zlib 子目录视需要纳入），并在 `docs/` 下补集成说明。
