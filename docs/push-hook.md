# Pre-push Hook

git `pre-push` hook：在每次 `git push` 前自动执行完整校验流水线，仅当全部通过、零失败时才放行 push。

## 触发的流水线

| 阶段 | 命令 | 作用 |
|------|------|------|
| 1/3 BUILD | `./fly.sh build` | bazel build `//src/...` + 刷新 `compile_commands.json` |
| 2/3 UNIT TEST | `./fly.sh test` | bazel test `//src/...`，任一用例失败即非零退出 |
| 3/3 QA TEST | `./fly.sh install && ./qa/runqa -j 4 -t 20` | install 生成 `build/bin/fly`，runqa 跑**全量** QA（4 路并发，单测 20s 超时） |

任一阶段失败立即报错并阻止 push，输出统一的 `PUSH BLOCKED` 横幅并指明失败阶段与退出码。

## 安装

hook 源文件纳入版本控制于 `scripts/pre-push`（带可执行权限位）。本地通过符号链接激活：

```bash
ln -sf ../../scripts/pre-push .git/hooks/pre-push
```

git 会自动发现 `.git/hooks/` 下的 hook，无需额外配置。

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

完整流水线耗时取决于 build 增量和 QA 用例规模。QA 全量约 103 个测试，默认 `-j 4` 并发、单测 20s 超时上限。最慢的 `test_golden_n500_sd4_coarse` 经 solver `-O2` 优化后单跑约 6s、全量 `-j4` 并发约 16s，余量充足。

## 在新环境安装

hook 脚本已纳入版本控制（`scripts/pre-push`，带可执行权限位），新 clone 的仓库只需创建一个符号链接指向它：

```bash
ln -sf ../../scripts/pre-push .git/hooks/pre-push
```

或直接拷贝（保留权限）：

```bash
cp scripts/pre-push .git/hooks/pre-push
chmod +x .git/hooks/pre-push
```

> 将 hook 纳入版本控制确保了脚本内容和权限位（100755）随仓库同步，避免 `.git/hooks/` 不被 git 跟踪导致的权限丢失问题。修改 hook 时直接编辑 `scripts/pre-push`，提交后所有环境一致。
