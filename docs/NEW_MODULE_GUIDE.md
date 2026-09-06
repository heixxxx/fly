# Fly 新模块开发指南

以添加 `pipeline` 模块为例，涵盖 C++ 核心、Python 导出、编译配置、注册到 fly 系统的完整流程。

---

## 1. 创建模块目录结构

```
src/pipeline/
├── cpp/
│   ├── pipeline.h          # C++ 头文件
│   ├── pipeline.cpp        # C++ 实现
│   └── BUILD              # cc_library + cc_shared_library
├── export/
│   ├── pipeline_export.cpp # nanobind 导出
│   └── BUILD              # cc_binary(linkshared=True)
├── py/
│   ├── __init__.py        # Python API 封装
│   └── BUILD              # py_library
└── tests/
    ├── pipeline_test.cpp  # gtest 测试
    └── BUILD              # cc_test
```

---

## 2. C++ 核心 (src/pipeline/cpp/)

### 头文件 `pipeline.h`

```cpp
#pragma once
#include <container/cpp/container_aliases.h>

namespace fly {

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    void add_stage(const CMString& name);
    CMString execute(const CMVector<CMString>& input);
    CMString get_stage(int index) const;
    int stage_count() const;

private:
    CMVector<CMString> stages_;
};

} // namespace fly
```

**规范：**
- 使用 `CMString` / `CMVector` / `CMMap` 等类型别名（定义于 `common_types.h`）
- include 路径使用 `<module/cpp/file.h>` 格式（模块式路径）
- 命名空间统一使用 `fly::`

### 实现文件 `pipeline.cpp`

```cpp
#include <pipeline/cpp/pipeline.h>

namespace fly {

void Pipeline::add_stage(const CMString& name) {
    stages_.push_back(name);
}

CMString Pipeline::execute(const CMVector<CMString>& input) {
    CMString result;
    for (const auto& stage : stages_) {
        result += "[" + stage + "]";
    }
    return result;
}

CMString Pipeline::get_stage(int index) const {
    if (index >= 0 && index < static_cast<int>(stages_.size())) {
        return stages_[index];
    }
    return "";
}

int Pipeline::stage_count() const {
    return static_cast<int>(stages_.size());
}

} // namespace fly
```

### 编译配置 `BUILD`

```python
package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

cc_library(
    name = "fly_pipeline",
    srcs = ["pipeline.cpp"],
    hdrs = ["pipeline.h"],
    strip_include_prefix = "/src",
    copts = ["-std=c++20"],
    linkstatic = False,
    deps = [
        "//src/container/cpp:fly_container_aliases",
    ],
)

cc_shared_library(
    name = "fly_pipeline_so",
    deps = [":fly_pipeline"],
    visibility = ["//visibility:public"],
)
```

**说明：**
- `cc_library` — 编译目标，提供头文件和 .pic.o
- `cc_shared_library` — 产出独立 `libfly_pipeline_so.so`，消除符号重复
- `linkstatic = False` — 确保生成 PIC 代码（供 shared library 使用）
- `strip_include_prefix = "/src"` — 使用模块式 include 路径
- **有跨模块依赖时**，需添加 `dynamic_deps` 并将第三方链接选项加到 `linkopts`：

```python
cc_library(
    name = "fly_pipeline",
    ...
    linkstatic = False,
    linkopts = ["-lsystem_lib"],  # 系统库链接
    deps = [
        "//src/container/cpp:fly_container_aliases",
        "//src/storage/cpp:fly_storage",  # 跨模块依赖
    ],
)

cc_shared_library(
    name = "fly_pipeline_so",
    deps = [":fly_pipeline"],
    dynamic_deps = [
        "//src/storage/cpp:fly_storage_so",  # 动态链接已有 .so
    ],
    visibility = ["//visibility:public"],
)
```

---

## 3. Python 导出 (src/pipeline/export/)

### 导出文件 `pipeline_export.cpp`

```cpp
#include <pipeline/cpp/pipeline.h>
#include <export/cpp/export_macros.h>

FLY_EXPORT_MODULE(_fly_pipeline) {
    FLY_EXPORT_CLASS(fly::Pipeline, "EXPlPipeline")
        FLY_EXPORT_INIT()
        FLY_EXPORT_METHOD("add_stage", &fly::Pipeline::add_stage)
        FLY_EXPORT_METHOD("execute", &fly::Pipeline::execute)
        FLY_EXPORT_READONLY_ATTR("stage_count", &fly::Pipeline::stage_count)
        FLY_EXPORT_METHOD("get_stage", &fly::Pipeline::get_stage);
}
```

**规范：**
- 模块名固定为 `_fly_` + 模块名（Python 加载约定）
- 导出类名前缀 `EX` + 模块缩写 + 类型名（如 Pipeline → `EXPlPipeline`，与现行 `EXStgDatabase`/`EXSlvSubdomainSolver` 一致）
- 使用 `FLY_EXPORT_*` 宏（定义于 `export_macros.h`）

### 编译配置 `BUILD`

```python
package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "_fly_pipeline.so",
    srcs = ["pipeline_export.cpp"],
    deps = [
        "@nanobind//:nanobind_src",
        "//src/export/cpp:fly_export_macros",
        "//src/pipeline/cpp:fly_pipeline",
    ],
    copts = ["-std=c++20"],
    linkshared = True,
    dynamic_deps = [
        "//src/pipeline/cpp:fly_pipeline_so",
    ],
)
```

**说明：**
- `linkshared = True` — 产出 Python 可加载的 .so
- `dynamic_deps` — 确保动态链接 `libfly_pipeline_so.so`（DT_NEEDED），而非静态合并
- 如果 pipeline 依赖其他模块（如 storage），还需添加对应的 `dynamic_deps`：
  ```python
  dynamic_deps = [
      "//src/pipeline/cpp:fly_pipeline_so",
      "//src/storage/cpp:fly_storage_so",
      "//src/common/serialization:fly_serialization_so",
  ],
  ```

---

## 4. Python API 层 (src/pipeline/py/)

### `__init__.py`

```python
from _fly_pipeline import EXPlPipeline

# 工厂函数等 Python-friendly 封装（不加 _ 前缀，允许 import * 导出）
def create_pipeline():
    """Python-friendly factory for Pipeline."""
    return EXPlPipeline()

# 禁止 __all__：跨模块导出靠包根 `from .xxx import *` 级联 + 默认行为
```

### `BUILD`

```python
load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "fly_pipeline_py",
    srcs = ["__init__.py"],
    visibility = ["//visibility:public"],
)
```

---

## 5. C++ 单元测试 (src/pipeline/tests/)

### `pipeline_test.cpp`

```cpp
#include <gtest/gtest.h>
#include <pipeline/cpp/pipeline.h>

TEST(PipelineTest, AddStage) {
    fly::Pipeline p;
    p.add_stage("parse");
    p.add_stage("transform");
    ASSERT_EQ(p.stage_count(), 2);
}

TEST(PipelineTest, Execute) {
    fly::Pipeline p;
    p.add_stage("a");
    p.add_stage("b");
    auto result = p.execute({"input"});
    ASSERT_EQ(result, "[a][b]");
}

TEST(PipelineTest, GetStage) {
    fly::Pipeline p;
    p.add_stage("x");
    p.add_stage("y");
    ASSERT_EQ(p.get_stage(0), "x");
    ASSERT_EQ(p.get_stage(1), "y");
}
```

### `BUILD`

```python
package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "pipeline_test",
    srcs = ["pipeline_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/pipeline/cpp:fly_pipeline",
    ],
    copts = ["-std=c++20"],
)
```

**说明：** `cc_test` 直接依赖 `cc_library`（静态链接），不需要 `dynamic_deps`。

---

## 6. 注册到 Fly 系统

### 6.1 添加到 main binary

编辑 `src/main/cpp/BUILD`，在 `fly` 的 `deps` 和 `dynamic_deps` 中添加：

```python
cc_binary(
    name = "fly",
    ...
    deps = [
        "//src/pipeline/cpp:fly_pipeline",  # 新增
        ...existing deps...
    ],
    dynamic_deps = [
        "//src/pipeline/cpp:fly_pipeline_so",  # 新增
        ...existing dynamic_deps...
    ],
)
```

### 6.2 添加到 Python 默认加载

编辑 `src/main/cpp/main.cpp`，在 `setup_sys_path()` 中添加模块路径：

```cpp
// build/ 布局（fly.sh install 后）
ps += "sys.path.insert(0, '" + (py_dir / "pipeline").string() + "')\n";

// bazel-bin/ 布局（开发时）
ps += "sys.path.insert(0, '" + (bazel_bin / "src" / "pipeline" / "export").string() + "')\n";
```

同时在 C++ 模块加载区添加：

```cpp
ps += "import _fly_pipeline\n";
```

编辑 `src/fly/__init__.py`，在模块导入区添加（**裸包根导入**，两种布局物理结构已统一；禁止 try/except 双布局）：

```python
from pipeline import EXPlPipeline  # 包根导出符号
```

### 6.3 添加到 compile_commands.json (可选)

编辑顶层 `BUILD` 的 `refresh_compile_commands` targets 列表：

```python
"//src/pipeline/cpp:fly_pipeline": "",
"//src/pipeline/cpp:fly_pipeline_so": "",
"//src/pipeline/export:_fly_pipeline.so": "",
"//src/pipeline/tests:pipeline_test": "",
```

或运行 `./fly.sh refresh` 自动重新生成。

### 6.4 添加到 fly.sh install（部署模式）

`fly.sh install` 创建 `build/` 目录，将 bazel-bin 产物 symlink 到可移植的目录结构中。新模块只需把模块名加入 `fly.sh` 的 `do_install()` 中 Python 模块的统一 `for mod in ...` 循环：

```bash
for mod in core log network task test storage agent solver message monitor pipeline; do
    ...mkdir + ln -sf ...
done
```

C++ shared libs 由通配符规则自动拾取（`src/*/export/_fly_*.so` 和 `src/*/cpp/libfly_*_so.so`），无需额外操作。

**验证安装**：

```bash
./fly.sh build //src/pipeline/...
./fly.sh install
ls -la build/python/pipeline/   # 应有 _fly_pipeline.so + *.py
ls -la build/lib/                # 应有 libfly_pipeline_so.so + _fly_pipeline.so
./build/bin/fly -c "from pipeline import EXPlPipeline; print('OK')"
```

**build/ 目录结构说明**：

```
build/
├── bin/
│   ├── fly            # wrapper 脚本（设置 LD_LIBRARY_PATH）
│   └── fly.bin → bazel-bin/src/main/cpp/fly
├── lib/               # C++ shared libraries
│   ├── libfly_pipeline_so.so → bazel-bin/src/pipeline/cpp/libfly_pipeline_so.so
│   ├── _fly_pipeline.so → bazel-bin/src/pipeline/export/_fly_pipeline.so
│   └── ...
└── python/            # Python modules (sys.path entry)
    ├── fly/           # fly 包（__init__.py, main.py, runtime.py）
    ├── pipeline/      # _fly_pipeline.so + pipeline/py/*.py
    └── ...
```

---

## 7. 构建与测试

```bash
# 构建所有新目标
./fly.sh build //src/pipeline/...

# 运行 C++ 测试
./fly.sh test //src/pipeline/tests:pipeline_test

# 运行全量测试确保无回归
./fly.sh test //src/...

# 安装并验证 deploy 模式
./fly.sh install
./build/bin/fly -c "from pipeline import EXPlPipeline; print('Pipeline OK')"
```

---

## 8. 检查清单

- [ ] `pipeline.h` 使用 CM* 类型别名和 `fly::` 命名空间
- [ ] `pipeline.cpp` 使用模块式 include 路径 `<pipeline/cpp/pipeline.h>`
- [ ] `BUILD` 有 `load("@rules_cc//cc:defs.bzl", "cc_library")`
- [ ] `cc_shared_library` 存在且有 `visibility = ["//visibility:public"]`
- [ ] export `BUILD` 有 `linkshared = True` 和 `dynamic_deps`
- [ ] export `BUILD` 依赖 `@nanobind//:nanobind_src` 和 `fly_export_macros`
- [ ] 模块名 `_fly_pipeline.so` 遵循 `_fly_` 前缀约定
- [ ] `main.cpp` 的 `setup_sys_path()` 包含新模块路径（build/ 和 bazel-bin/ 两种布局）
- [ ] `main.cpp` 的 C++ 模块加载区包含 `import _fly_pipeline\n`
- [ ] `src/fly/__init__.py` 包含新模块的 Python 导入（裸包根导入 `from pipeline import EXPlPipeline`，禁止 try/except 双布局）
- [ ] `main/cpp/BUILD` 的 `deps` 和 `dynamic_deps` 包含新模块
- [ ] `fly.sh` 的 `do_install()` 统一 `for mod in ...` 循环包含新模块名
- [ ] `nm -CD` 验证无符号重复（新模块的符号只出现在 `libfly_pipeline_so.so` 中）
- [ ] `./fly.sh install` 后 `build/` 目录结构正确
