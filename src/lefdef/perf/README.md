# src/lefdef/perf — 性能基准与大规模测试文件工具

解析器性能测试的脚本与 driver 源码。实际测试时在 `.work/lefdef_perf/` 下执行（大文件不入 git），本目录只入库脚本源码。

## 文件说明

- `run_bench.sh`：基准 + 回归脚本。`baseline` 模式用当前构建产物生成回显金标；`verify` 模式做金标比对、Galaxy 对象计数校验、5 轮计时。依赖 `.work/lefdef_perf/` 下有 `Galaxy.def`（442MB）与 `galaxy_gold/` 金标。
- `defrw_perf_src.cpp`：性能测试 driver（与 `def/defrw/defrw_perf.cpp` 相同，注册全部空回调做"纯解析 + 数据填充"，零业务开销）。编译示例：
  `g++ -O3 -std=c++17 -I def/def defrw_perf_src.cpp def/def/*.cpp -o bench`
- `bench_driver.cpp`：计数型 driver（无回显、带对象计数与计时，另有传统/流式两种 partial-path 模式的变体，用于校验 wire 路径计数）。
- `gen_def.py`：合成 DEF 生成器（components/nets/specialnets 规模可参数化，坐标与连接为语法合法的伪随机数据）。

## 12GB 等比放大文件

正式的 12GB 测试文件（`Galaxy_12GB.def.test`，12.0GB）不入 git（超 GitHub 100MB 单文件限制），由 444MB 修复版种子全段等比 ×27 生成：全部段（COMPONENTS/PINS/PINPROPERTIES/BLOCKAGES/SPECIALNETS/NETS）体 ×27、声明数 ×27、各段 END 行仅保留一份。等比生成脚本为一次性使用，核心逻辑：逐段定位声明行 → 段体循环复制 27 次 → 声明数字段 ×27 → END 行只写一次。规模：2456.8 万 components、909.3 万 nets、68.0 万 specialnets、2644 万 connections、8909 万 wire 路径；解析 status=0、七项计数精确 ×27。

## 已知结论（详见 README.md 性能优化记录）

- 同机同 -O3 公平对照：Si2 原版 128.4s ≈ 外部魔改版（lefdef-6.1-mod）122.8s——魔改版主力优化（gperf 关键字查找）不在 dumb_mode 热路径上执行，收益 4.4% 主要来自残差。
- 本仓库五项优化：91.0s（相对纯原版快 29.1%）；叠加 tcmalloc 78.6s（快 38.8%，吞吐约 153MB/s）。
- 峰值内存与文件体积无关（流式解析，由最大单记录决定）：151.5MB（原版）→ 102.5MB（优化版）。
