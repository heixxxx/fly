# third_party/lefdef-6.1-mod — 外部魔改版 LEF/DEF 解析器（对照参考）

来自移动硬盘（Windows F: 盘 `lefdef6.1` 目录）的外部魔改版，作者 blank（583957236@qq.com），2026 年 4 月基于 Si2 LEF/DEF 6.0_62-p004 完成（初始提交经 git 历史验证与本仓库 `src/lefdef` 的上游基线逐字节一致）。本目录仅作**对照参考**，不参与构建；吸收优化时以本目录的实现为素材、目标代码落在 `src/lefdef/`。

## 复制时的剔除内容（原始目录约 26GB）

- 超大测试文件约 25.4GB：`def/TEST/Galaxy.def.test`（12G）、`Galaxy.def.au`（12G）、`Galaxy_large.def`（883M）、`test_escape_large.def`（608M）、`Galaxy_ori.def`（442M）、`Galaxy.def`（442M）
- 编译产物：`def/bin`、`def/lib`、`def/include`、`lef/bin`、各 `.o`/`.a`、defrw 系列可执行二进制（defrw_gperf/defrw_perf/defrw_std_map/defrw_unordered_map/test_placement 等 ELF）
- `COO_Document_2025-03-18_165805.pdf`（4.2M，出处证明，与上游基线包一致）

原始 `.git` 历史（11MB，含 9 个魔改提交）另存于 `.work/lefdef6.1_git_history/`（不进本仓库 git，避免嵌套仓库）。

## 内容说明

- `lef/`：与原版无源码差异（魔改全部在 DEF 侧），仅多 bison 生成物（未复制）
- `def/`：魔改主体，见下
- `enhancements/`：NetPartialPathCbk 增强包文档 + 补丁 + 用例；`lefdef6.1_all_enhancements.patch` 为全部魔改的补丁全集
- `def/TEST/*.md`：魔改者留下的 8 份性能分析/对比报告（含 profiling 数据）；`generate_test_def.py` 为测试 DEF 生成器
- `def/defrw/defrw_*.cpp`：5 个对比测试程序源码
- `build.sh`：作者的 -O3 构建脚本（路径硬编码 Windows 侧，仅参考）
- `def/template.mk`：相对上游增加了 OPT_LEVEL（默认 -O2）、KEYWORD_IMPL 关键字实现切换、PROFILING 支持

## 已知遗留问题（吸收时需处理）

1. 调试残留：`defrData.hpp` 的 `IncCurPos` inline 版本每次扩容会 `fprintf(stderr, "DEBUG ...")`；`def_keywords.cpp` 顶部有 pv_realloc_count 等 debug 计数器
2. `def.tab.cpp`/`def.tab.hpp` 是作者环境（新版 bison）的生成物，吸收时应重新生成而非照搬
3. `def/TEST/complete.5.8.def` 相对上游被作者改过（41 行），不可作为原版回归基线
