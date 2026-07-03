# Common 模块 — 公共类型定义

## 模块概述

**位置**: `src/common/cpp/`

提供全项目统一使用的容器类型别名和智能指针别名，所有模块依赖此基础层。

---

## 核心文件

| 文件 | 说明 |
|------|------|
| `common_types.h` | 容器类型别名 + 智能指针别名定义 |
| `writer_id.h` | generate_writer_id()（8-char hex UUID） |
| `worker_context.h` | WorkerAgentContext（回调模式） |
| `error_types.h` | WriteErrorType 枚举 |
| `fly_buffer.h` | FlyBuffer + FlyBufferPtr 定义（通用零拷贝字节缓冲区，跨 storage/network/agent/export 共用） |
| `BUILD` | Bazel 构建配置 |

---

## 类型别名

所有类型在 `fly` 命名空间内定义，同时提供全局 `using` 声明。

### 容器类型

| 别名 | 原始类型 | 用途 |
|------|----------|------|
| `CMString` | `std::string` | 统一字符串类型 |
| `CMVector<T>` | `std::vector<T>` | 动态数组 |
| `CMMap<K,V>` | `std::map<K,V>` | 有序映射 |
| `CMUnorderedMap<K,V>` | `std::unordered_map<K,V>` | 无序映射 |
| `CMSet<T>` | `std::set<T>` | 有序集合 |
| `CMUnorderedSet<T>` | `std::unordered_set<T>` | 无序集合 |
| `CMList<T>` | `std::list<T>` | 双向链表 |
| `CMDeque<T>` | `std::deque<T>` | 双端队列 |
| `CMQueue<T>` | `std::queue<T>` | 队列 |
| `CMStack<T>` | `std::stack<T>` | 栈 |
| `CMMapKV<K,V>` | `std::pair<K,V>` | 键值对 |

### 智能指针类型

| 别名 | 原始类型 | 用途 |
|------|----------|------|
| `CMSharedPtr<T>` | `std::shared_ptr<T>` | 共享所有权指针 |
| `CMUniquePtr<T>` | `std::unique_ptr<T>` | 独占所有权指针 |
| `CMWeakPtr<T>` | `std::weak_ptr<T>` | 弱引用指针 |

### 工厂函数

| 函数 | 原始函数 | 用途 |
|------|----------|------|
| `CMMakeShared<T>(args...)` | `std::make_shared<T>(args...)` | 创建 shared_ptr |
| `CMMakeUnique<T>(args...)` | `std::make_unique<T>(args...)` | 创建 unique_ptr |

### 智能指针转换

| 函数 | 原始函数 | 用途 |
|------|----------|------|
| `CMStaticPointerCast<T>(ptr)` | `std::static_pointer_cast<T>(ptr)` | 静态类型转换 |
| `CMDynamicPointerCast<T>(ptr)` | `std::dynamic_pointer_cast<T>(ptr)` | 动态类型转换 |
| `CMConstPointerCast<T>(ptr)` | `std::const_pointer_cast<T>(ptr)` | const 转换 |
| `CMReinterpretPointerCast<T>(ptr)` | `std::reinterpret_pointer_cast<T>(ptr)` | reinterpret 转换 |

---

## FlyBuffer 和 FlyBufferPtr

```cpp
// FlyBuffer: 非拷贝缓冲区，封装 CMString
class FlyBuffer {
public:
    FlyBuffer();
    explicit FlyBuffer(CMString&& data);  // 移动构造

    void take(CMString&& data);  // 零拷贝移动
    CMString release();          // 零拷贝释放

    const char* data() const;
    size_t size() const;
    bool empty() const;
};

// FlyBufferPtr: 共享所有权的压缩字节缓冲区
using FlyBufferPtr = CMSharedPtr<FlyBuffer>;
```

**设计要点**:
- `FlyBuffer` 内部存储为 `CMString`，兼容 bitsery adapter 和 Python pickle
- `take(CMString&&)` 和 `release()` 实现零拷贝移动
- `FlyBufferPtr` 是 `CMSharedPtr<FlyBuffer>`，支持零拷贝共享压缩字节
- 在缓存、读取、serve 路径中实现零拷贝

---

## ErrorTypes

```cpp
// 写入错误类型（与 TaskErrorType 分离）
enum class WriteErrorType {
    OK = 0,
    FROZEN,           // 数据库已冻结
    WRITE_FAILED,     // 写入失败
    SERIALIZATION_ERROR,  // 序列化错误
    COMPRESSION_ERROR,    // 压缩错误
};

// 任务错误类型
enum class TaskErrorType {
    NONE = 0,
    DEPENDENCY_NOT_MET,
    EXECUTION_FAILED,
    TIMEOUT,
    ...
};
```

---

## WriterID

```cpp
// 生成 8-char hex UUID
CMString generate_writer_id();
```

**格式**: 8 个十六进制字符（如 `"a1b2c3d4"`），Database 构造时生成。

---

## WorkerAgentContext

```cpp
// WorkerAgent 上下文（回调模式，解耦 Database 和 WorkerAgent）。
// 全静态 + thread_local：无实例数据，按线程注入回调。
class WorkerAgentContext {
public:
    // ---- 写入跟踪回调（worker task 上下文激活）----
    static void set_record_write_func(std::function<void(const CMString& db_id,
                                                         const CMString& name, int64_t size)> func);
    static void record_write(const CMString& db_id, const CMString& object_name, int64_t size);

    // ---- 注册写入（同步等 master ack，返回 error + error_type）----
    static void set_register_func(std::function<std::pair<CMString, TaskErrorType>(
        const CMString&, const CMString&, int64_t)> func);
    static std::pair<CMString, TaskErrorType> register_write(const CMString& db_id,
        const CMString& object_name, int64_t compressed_size);

    // ---- 其他写入事件回调 ----
    static void set_notify_removed_func(std::function<void(const CMString&, const CMString&)> func);
    static void set_freeze_func(std::function<void(const CMString&)> func);
    static void set_remove_request_func(std::function<void(const CMString&, const CMString&)> func);
    static void set_backup_request_func(std::function<void(const CMString&, const CMString&)> func);

    // ---- Var 服务（轻量 KV，full_var_name = "db_id:short_name"）----
    // value 是已序列化的 FlyBufferPtr（pickle 或 FLY_ENCODE_TO_BUFFER）。
    static void set_set_var_func(std::function<bool(const CMString& full_var_name,
        FlyBufferPtr value, const CMString& type_name)> func);
    static void set_get_var_func(std::function<std::tuple<bool, FlyBufferPtr, CMString>(
        const CMString& full_var_name)> func);
    static void set_remove_var_func(std::function<void(const CMString& full_var_name)> func);

    // ---- 事务模式（worker task 写入需 BEGIN/END 包裹；master 直接写则否）----
    static void set_transaction_mode(bool enabled);
    static bool is_transaction_mode();

    // ---- 错误/哈希上下文（task 执行期间）----
    static void set_last_error_type(TaskErrorType type);
    static TaskErrorType get_last_error_type();
    static void set_current_write_hash(const CMString& hash);

    // ---- 清理（end_task 时）----
    static void clear();

private:
    static inline thread_local std::function<...> record_write_func_;
    static inline thread_local std::function<...> register_func_;
    // ... 其余回调皆为 static inline thread_local
};
```

**设计要点**:
- 使用 `std::function` 回调实现解耦，Database 不依赖 WorkerAgent
- **全静态 + thread_local**：无实例数据，按线程注入回调（worker 主线程在 `begin_task` 设、`end_task`/`clear` 清）
- 放在 `common` 模块是因为 Database（storage 层）和 WorkerAgent（agent 层）都需要访问
- 避免循环依赖：common → (无依赖)，storage → common，agent → common
- Var 服务回调以 `FlyBufferPtr` 为参数类型（本文件 include `<common/cpp/fly_buffer.h>`）

---

## 使用方式

```cpp
#include <common/cpp/common_types.h>

CMString name = "hello";
CMVector<int> ids = {1, 2, 3};
CMMap<CMString, int64_t> config_values;

// 智能指针
auto ptr = CMMakeShared<CMString>("shared");
CMSharedPtr<CMString> shared = ptr;

// FlyBuffer
FlyBuffer buffer;
buffer.take(CMString("data"));
CMString released = buffer.release();  // 零拷贝
```

---

## 设计决策

| 决策 | 原因 |
|------|------|
| `CM` 前缀 (Common) | 统一命名空间，便于全局搜索和替换底层实现 |
| 全局 `using` 声明 | 允许直接使用 `CMString` 而无需 `fly::` 前缀 |
| 无运行时依赖 | 纯头文件类型别名，零开销抽象 |
| CMSharedPtr 而非裸指针 | 统一所有权语义，避免内存泄漏 |
| FlyBuffer 内部存储为 CMString | 消除 char↔uint8_t 阻抗失配，兼容 bitsery |
| WorkerAgentContext 放在 common | 避免 storage ↔ agent 循环依赖 |

---

*文档更新日期: 2026-06-17*
