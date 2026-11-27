# 推送门禁（`scripts/push` 与 pre-push 钩子）

推送到远端必须经过门禁：**build → unit test → 全量 QA** 全部通过、零失败才能推送。门禁由专用的推送命令 `scripts/push` 在 `git push` **之前**跑完，通过后写一个标记文件；`pre-push` 钩子只核对这个标记，不跑测试。

**推送必须使用 `./scripts/push` 脚本进行——包括所有自动化流程与 agent 的推送工作。** 不要直接用 `git push`：钩子找不到对应的标记会拒绝推送，并提示正确的命令。

## 用法

```bash
./scripts/push                 # 等价于 git push
./scripts/push origin main     # 参数原样传给 git push
```

推送前工作区必须干净（没有未提交、未跟踪的改动），`./scripts/push` 才会开始跑门禁——标记只为一个提交背书，门禁验证的内容必须就是将要推送的提交。

## 流程

`scripts/push`：

| 步骤 | 命令 | 作用 |
|------|------|------|
| 0. 前置检查 | `git status --porcelain` | 工作区必须与当前提交完全一致——没有未提交、未跟踪的改动。标记只为一个提交背书，门禁验证的内容必须就是将要推送的提交 |
| 1/3 BUILD | `./fly.sh build` | bazel build `//src/...` + 刷新 `compile_commands.json` |
| 2/3 UNIT TEST | `./fly.sh test //src/... --jobs=4` | bazel test `//src/...` 阶段内并行 4，任一用例失败即非零退出；阶段末 `bazel shutdown` 释放 Java server 内存 |
| 3/3 QA TEST | `./fly.sh install && ./qa/runqa -j 6 -t 20` | install 生成 `build/bin/fly`，runqa 跑**全量** QA（阶段内并行 6；`-t 20` 保留为兼容，对 `.pyt` 用例不约束时长——子阶段各自超时）；全量运行结束时运行器做引用审计（qa/ 下每个 `test_*.py` 都必须被某个 `.pyt` 调度）；门禁不做基准重录（`--regold` 是显式操作，见 `AGENTS.md`） |
| 4. 写标记 | — | 再核对一次 HEAD 与工作区在门禁期间没有变化，把验证过的提交号写进标记文件（临时文件 + 原子替换） |
| 5. 推送 | `git push "$@"` | 钩子核对标记后放行 |

**两套测试严格串行**：unit test 全部通过后才启动 QA，不做两套并行叠加。各阶段前后打印内存水位（`[mem] xxxxMB available`），OOM 复发时有现场可查。

任一阶段失败立即报错并停止，不写标记、不推送，输出统一的 `PUSH BLOCKED` 横幅并指明失败阶段与退出码。

### 标记文件

位置：`$(git rev-parse --absolute-git-dir)/fly-push-marker`，在 `.git/` 目录里，不入库。内容是验证过的提交号（40 位十六进制，一行）。

标记不是一次性的：门禁通过之后，同一个提交可以重复推送（例如网络失败后重推，直接 `git push` 也能过）。换了提交，标记就对不上，必须重新跑 `./scripts/push`。

### 钩子的核对规则（`scripts/pre-push`）

git 把要更新的引用从标准输入喂给钩子，每行是「本地引用 本地对象号 远端引用 远端对象号」。钩子逐个核对：

- **推送内容的引用**（本地对象号不是全零）：解析到它指向的提交（附注标签的对象号是标签对象自己的，先解析到提交），必须等于标记里的提交号。
- **删除远端引用**（本地对象号全零）不推送内容，跳过逐个核对；但标记文件必须存在——删除远端引用同样要经 `./scripts/push`。
- **标记不存在、内容不是提交号，或者任何一个引用对不上**：退出码 1，打印 `PUSH BLOCKED`，写明标记文件的情况、本次推送的引用，以及正确的推送方式。

门禁验证的是当前检出的提交，所以只能推送指向它的引用（当前分支、指向它的标签、或者把它推到远端的另一个分支名）。

## 为什么门禁不放在钩子里

`git push` 的顺序是「连接远端 → 运行 pre-push 钩子 → 传输」。门禁跑多久，连接就空闲多久；超过约 6 分钟时 GitHub 会关闭空闲连接，钩子跑完放行之后 `git push` 往已经退出的 ssh 子进程里写数据，被 SIGPIPE 杀死（退出码 141），远端没有收到提交，也没有任何收尾提示。

实测记录：

- 2026-11-23：冷构建使钩子跑了约 9.6 分钟，出现一次。
- 2026-11-27：再次出现并定位。构建缓存是冷的，钩子跑了约 9 分钟（构建 336 秒、单测 59 秒、全量 QA 128 秒）；日志里连接建立约 6 分钟后出现 `Connection to github.com closed by remote host.`。重推时构建缓存已热，钩子约 2.5 分钟，没有加保活也成功——决定因素是钩子耗时。

所以门禁挪到 `git push` 之前：`scripts/push` 跑完门禁才调用 `git push`，钩子里只剩一次标记核对，连接不再空闲等待。早先「给 SSH 连接加保活后重推」的处置不再需要。

## 安装

钩子源文件纳入版本控制于 `scripts/pre-push`（带可执行权限位）。git 出于安全考虑**不会随 `clone` 自动安装 hooks**，所以需要本地激活。已集成进 `fly.sh`——运行 `./fly.sh install` 时会幂等地把 `scripts/pre-push` 软链到 `.git/hooks/pre-push`：

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

没有安装钩子的检出不会被拦：`git push` 直接推出去，门禁没有跑。所以新 clone 之后先运行一次 `./fly.sh install`。

## 绕过

**禁止在任何情况下使用 `git push --no-verify`，也禁止手写标记文件。** 钩子只核对标记，这两种做法都能让没过门禁的提交推到远端。门禁唯一目的是防止把回归推到远端，绕过它等同于跳过校验。

如果门禁某一阶段失败：

- 这是门禁在正确工作 —— 它捕获了一个真实的回归。
- 正确做法是**修复导致失败的根因**（编译错误 / 失败用例 / flaky 测试），让流水线自然通过，再推送。
- 不允许以"我知道有问题但这次先推"为由绕过。

## 失败排查

### `scripts/push` 的某一阶段失败

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

### `scripts/push` 在门禁之前或之后停了

- **工作区与当前提交不一致**：门禁没有开始。横幅列出改动，提交（或者移走）之后再运行。
- **门禁运行期间 HEAD 变了，或者门禁之后工作区出现了改动**：验证过的不是将要推送的提交。在要推送的提交上重新运行。

### 钩子拒绝推送（直接用了 `git push`）

终端会打印形如：

```
============================== PUSH BLOCKED ==============================
  本次推送没有对应的门禁标记，push 已阻止。

  标记文件：/path/to/repo/.git/fly-push-marker
    不存在（没有跑过门禁，或者标记被删掉了）
  本次推送的引用（本地引用 本地对象号 -> 远端引用）：
    refs/heads/main  0123abcd…  ->  refs/heads/main

  正确的推送方式：不要直接用 git push。用项目的推送命令——它先跑
  build -> unit test -> 全量 QA，全部通过后把提交号写进上面的标记文件，再推送：

      ./scripts/push                # 参数与 git push 相同，例如 ./scripts/push origin main
  …
```

按提示改用 `./scripts/push`。标记里记录的提交与本次推送不一致时，横幅会同时列出两个提交号：换了提交就要重新跑门禁。

### 耗时参考

完整流水线耗时取决于 build 增量和 QA 用例规模（**QA case 数随开发持续增长，以 runqa 运行输出为准，文档不写死**）。当前脚本配方（`scripts/push` 为唯一权威：单测 `--jobs=4`、QA `-j 6 -t 20`）实测 QA 阶段全量门禁通过约 130 秒；保守降档 `-j 2` 约 171 秒，属资源安全的设计取舍。

> **关于超时/并行配置**：`-t 20` 配合当前并行度是 2026-08-16 OOM 实测后的稳定配方（此前 `-j 4` 下三阶段连续执行内存峰值在 7.8GB 开发机上超限，见下文资源约束）。强制 `.pyt` 包装后 `-t` 对用例时长不再起作用（子阶段各自用 `run_subcase` 的超时），`-t 20` 仅作为兼容参数保留。runqa 的默认并行度、钳制上限与覆盖方式以 [`qa/README.md`](../qa/README.md)「并行度权威口径」与 `qa/runqa` 源码为准；`scripts/push` 显式传 `-j 6` 以固定门禁行为。

## 资源约束与配方依据（2026-08-16 OOM 实测 + 用户裁定）

开发机为 7.8GB / 6 核 WSL2。OOM 根因是旧默认配方（bazel 按核并行 6-8 测试进程树 + runqa -j4 且无阶段内存隔离）峰值超限，多次触发 OOM。当前配方（用户裁定：阶段内并行 unittest -j4 / QA -j6 + 两套严格串行）实测数据：

| 配置 | 内存最低点（available） | 结果 |
|------|-------------------------|------|
| 旧默认：bazel 按核并行 + runqa -j4 | 触发 OOM | push 反复失败 |
| `bazel test --jobs=4`（串行配方下） | 谷值未单独采样（两次运行均稳定） | 56/56 全过 |
| `runqa -j 6 -t 20`（单测结束后） | 3.4GB | 全量全过（70s） |
| 中间验证档：`--jobs=2` / `-j 2` | 5.1GB / 3.9GB | 全量全过（更保守，耗时 2 倍+） |

OOM 的内存构成：多个 gtest 进程树并行（`agent_network_test` 类用例每个起 master + 多 worker 全栈线程；`data_service_concurrency_bench` 8 线程压测）× 每树数百 MB RSS，叠加 bazel Java server（阶段间 `bazel shutdown` 释放 0.5-1GB）。串行化的关键收益是**两套测试的峰值不叠加**。

同根因的另一面：CPU 饱和时 `agent_network_test.DuplicateWorkerRegisterRejectedAfterProbe` 的 probe 往返被调度延迟拉长（正常 <100ms，饥饿时超 60s 等待窗）——见 ISSUES P3-23。

> `Fly Startup Info` 的 `proc mem` 自 2026-08-16 起显示**物理内存**（VmRSS + peak VmHWM）；此前显示 VmPeak（虚拟地址空间峰值）曾在 OOM 排查中被误读为物理占用。

## 测试

钩子的核对规则和 `scripts/push` 的前置检查由 `//src/test/tests:push_hook_test` 覆盖（`./fly.sh test //src/test/tests:push_hook_test`）：在临时 git 仓库里做真实的 `git push`（本地裸仓库当远端，不联网），验证标记缺失、不一致、格式不对、附注标签、多引用、删除远端引用等情况下的放行与拦截，以及报错里写明了正确的推送方式。门禁本身（构建与全量 QA）不在单测里跑。
