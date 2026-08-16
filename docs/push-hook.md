# Pre-push Hook

git `pre-push` hook：在每次 `git push` 前自动执行完整校验流水线，仅当全部通过、零失败时才放行 push。

## 触发的流水线

| 阶段 | 命令 | 作用 |
|------|------|------|
| 1/3 BUILD | `./fly.sh build` | bazel build `//src/...` + 刷新 `compile_commands.json` |
| 2/3 UNIT TEST | `./fly.sh test //src/... --jobs=2` | bazel test `//src/...` 受控并行（见下方资源约束），任一用例失败即非零退出；阶段末 `bazel shutdown` 释放 Java server 内存 |
| 3/3 QA TEST | `./fly.sh install && ./qa/runqa -j 2 -t 20` | install 生成 `build/bin/fly`，runqa 跑**全量** QA（2 路并发，单测 20s 超时） |

任一阶段失败立即报错并阻止 push，输出统一的 `PUSH BLOCKED` 横幅并指明失败阶段与退出码。

### 资源约束与受控配方（2026-08-16 OOM 实测修正）

开发机为 7.8GB / 6 核 WSL2。默认配方（bazel 按核并行跑单测 + runqa -j 4）在三阶段连续执行时内存峰值超限，多次触发 OOM（会话被杀）。受控配方及实测数据：

| 配置 | 内存最低点（avail） | 结果 |
|------|---------------------|------|
| bazel 默认并行（6-8 测试进程树）+ runqa -j 4 | 触发 OOM | push 反复失败 |
| `bazel test --jobs=2` | 5.1GB | 56/56 全过 |
| `runqa -j 2` | 3.9GB | 162/162 全过 |

OOM 的内存构成：多个 gtest 进程树并行（`agent_network_test` 类用例每个起 master + 多 worker 全栈线程；`data_service_concurrency_bench` 8 线程压测）× 每树数百 MB RSS，叠加 bazel Java server（阶段间 `bazel shutdown` 释放 0.5-1GB）。注意 `Fly Startup Info` 打印的 `proc mem` 是 **VmPeak（虚拟地址空间峰值）**，非物理占用，不能作为内存压力证据。

同根因的另一面：CPU 饱和时 `agent_network_test.DuplicateWorkerRegisterRejectedAfterProbe` 的 probe 往返被调度延迟拉长（正常 <100ms，饥饿时超 60s 等待窗）——见 ISSUES P3-23。降低并行度后两者同步消失。

## 安装

hook 源文件纳入版本控制于 `scripts/pre-push`（带可执行权限位）。git 出于安全考虑**不会随 `clone` 自动安装 hooks**，所以需要本地激活。已集成进 `fly.sh`——运行 `./fly.sh install` 时会幂等地把 `scripts/pre-push` 软链到 `.git/hooks/pre-push`：

```bash
./fly.sh install
# >>> Install complete: /root/fly/build/bin/fly
# >>> Installed pre-push hook: .git/hooks/pre-push -> /root/fly/scripts/pre-push
```

幂等策略（见 `fly.sh::install_hooks`）：

- 软链已存在且 target 正确 → 静默跳过
- 软链断开或指向他处 → 重建
- `.git/hooks/pre-push` 是**普通文件**（用户自定义 hook）→ 打印 WARNING，**不覆盖**
- `scripts/pre-push` 缺失 → 警告，不阻断 install

用软链而非复制，使得 `scripts/pre-push` 的后续修改无需重新安装即对所有环境生效。若必须手动安装（如无 `fly.sh` 的环境）：

```bash
ln -sf ../../scripts/pre-push .git/hooks/pre-push
```

> hook 内容和权限位随 `scripts/pre-push` 同步到远端，编辑后提交即对所有环境生效。

## 绕过

**禁止在任何情况下使用 `git push --no-verify`。** push hook 是质量门禁，唯一目的是防止把回归推到远端。绕过它等同于跳过校验，与引入本 hook 的初衷直接冲突。

如果 hook 某一阶段失败：

- 这是 hook 在正确工作 —— 它捕获了一个真实的回归。
- 正确做法是**修复导致失败的根因**（编译错误 / 失败用例 / flaky 测试），让流水线自然通过，再 push。
- 不允许以"我知道有问题但这次先推"为由使用 `--no-verify`。

## 失败排查

### 某一阶段失败

终端会打印形如：

```
============================== PUSH BLOCKED ==============================
  阶段 [2/3 UNIT TEST] 失败 (exit 1)，push 已阻止。
  请修复失败根因后重试。禁止使用 git push --no-verify 绕过。
==========================================================================
```

按阶段定位：

- **BUILD 失败**：编译错误，按 `./fly.sh build` 输出修复。
- **UNIT TEST 失败**：`./fly.sh test` 会打印 `--test_output=errors` 的失败用例详情。
- **QA TEST 失败**：查看日志
  - 汇总日志：`qa/logs/qa.log`（含每个 case 的 PASSED/FAILED/TIMEOUT 标记）
  - 单个 case 输出：`qa/<dir>/<test_name>/fly.log`（例如 `qa/storage/test_read_cache_basic/fly.log`）

### 耗时参考

完整流水线耗时取决于 build 增量和 QA 用例规模。QA 全量 162 个测试，`-j 2` 并发、单测 20s 超时上限，实测门禁全过耗时约 171s（QA 阶段）；单测阶段 `--jobs=2` 全量约 4 分钟（重量级用例串行化后总时长约为默认并行的 2 倍，属资源安全的设计取舍）。

> **关于超时配置**：`-t 20` 配合 `-j 2`（限并发）是经过实测的稳定配置（2026-08-16 由 `-j 4` 下调——三阶段连续执行的内存峰值在 7.8GB 开发机上超限，见上文资源约束）。`runqa` 默认并行度为 4、上限钳制在 32（`-j 0` 或负数亦钳制到 32）。pre-push 显式带 `-j 2` 以固定门禁行为。

## 在新环境安装

新 clone 的仓库**只需运行一次 `./fly.sh install`**——它会幂等地把 `scripts/pre-push` 软链到 `.git/hooks/pre-push`（详见"安装"段）。无需手动操作。

> 将 hook 纳入版本控制确保了脚本内容和权限位（100755）随仓库同步，避免 `.git/hooks/` 不被 git 跟踪导致的权限丢失问题。修改 hook 时直接编辑 `scripts/pre-push`，提交后所有环境一致（得益于软链，已激活的环境无需重装）。
