---
name: fly-build
description: Fly 项目专用构建命令，必须使用 ./fly.sh 而非裸 bazel build，否则 compile_commands.json 不会更新，clangd 无法工作
---

<SUBAGENT-STOP>
If you were dispatched as a subagent to execute a specific task, skip this skill.
</SUBAGENT-STOP>

# Fly 项目构建规范

## 核心规则

**永远不要直接使用 `bazel build` 或 `bazel test` 命令。** 必须使用 `./fly.sh` 脚本。

## 原因

`compile_commands.json` 由 `hedron_compile_commands` 工具生成，需要两步：
1. 将所有 C++ 目标注册到顶层 BUILD 文件
2. 运行 `bazel run //:refresh_compile_commands`

`fly.sh` 自动完成这两个步骤。直接使用 `bazel build` 不会触发刷新，导致 clangd 找不到新目标或报大量错误。

## 可用命令

```bash
# 构建 + 自动刷新 clangd（默认行为）
./fly.sh build [target...]

# 测试 + 自动刷新 clangd
./fly.sh test [target...]

# 仅构建，不刷新 clangd（快速迭代时用）
./fly.sh buildonly [target...]

# 仅刷新 compile_commands.json
./fly.sh refresh

# 构建 + 测试 + 刷新
./fly.sh check [target...]
```

## 何时跳过刷新

仅在以下场景可以安全使用 `bazel build` 直接构建：
- 只修改了 `.cpp` 文件，不涉及新文件或 BUILD 变更
- 后续会手动运行 `./fly.sh refresh`

## 验证

构建完成后检查 compile_commands.json 是否更新：
```bash
ls -la compile_commands.json        # 确认文件存在
stat compile_commands.json           # 确认时间戳是最近的
```

如果 clangd 仍报错，运行 `./fly.sh refresh` 手动刷新。