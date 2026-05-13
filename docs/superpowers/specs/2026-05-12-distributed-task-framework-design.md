# 分布式任务执行框架设计

## 一、概述

### 项目目标
构建一个多机多进程分布式任务执行框架，支持：
- Master节点负责任务调度和数据元信息管理
- Worker节点负责具体任务执行和数据存储
- 任务可在任意节点提交，支持递归任务提交
- 任务调度需满足数据依赖准备完毕
- 数据传递依靠分布式文件存储系统

### 技术栈
- **Python**: 流程控制、数据读写、任务定义
- **C++20**: 存储层实现、性能敏感算法（使用C++20 Modules `import`/`export module`机制）
- **nanobind**: C++与Python交互（通过FLY_EXPORT_*宏封装，支持未来替换）
- **zpp_bits**: 二进制序列化（通过FLY_SERIALIZE_*宏封装，支持未来替换）
- **TCP Socket**: 节点间通信（通过TransportLayer抽象层，支持未来替换为UDP/RDMA等）
- **Bazel**: 构建系统（支持C++20 Modules编译）

---

## 二、整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Master Node                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ Task Scheduler│  │ Metadata   │  │ Storage Metadata       │ │
│  │ (FIFO+Locality)│  │ Manager    │  │ (Data blocks, replicas)│ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
│         │                │                    │                 │
│         └────────────────┼────────────────────┘                 │
│                          │                                      │
│                   ┌──────┴──────┐                               │
│                   │ Message Hub │  ← TCP Server (Port 8000)    │
│                   └─────────────┘                               │
└───────────────────────────────┬─────────────────────────────────┘
                                │
          ┌─────────────────────┼─────────────────────┐
          │                     │                     │
          ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   Worker 1      │  │   Worker 2      │  │   Worker 3      │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │
│ │Task Executor│ │  │ │Task Executor│ │  │ │ (Storage     │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ │  Only Mode)  │ │
│ ┌─────────────┐ │  │ ┌─────────────┐ │  │ └─────────────┘ │
│ │Data Storage │ │  │ │Data Storage │ │  │ ┌─────────────┐ │
│ │(Aggregator) │ │  │ │(Aggregator) │ │  │ │Data Storage │ │
│ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

---

## 三、启动流程与进程模型

### 3.1 Binary启动方式

统一使用 `fly` binary启动，通过参数区分Master/Worker模式：

```bash
# Master模式：直接传入用户脚本（无flag）
fly user_tasks.py

# Worker模式：使用--worker_mode区分
fly --worker_mode --master master:8000 --role hybrid
fly --worker_mode --master master:8000 --role storage_only
```

**启动参数说明**：

| 参数 | Master模式 | Worker模式 |
|------|-----------|-----------|
| positional arg | 用户Python脚本路径 | 不适用 |
| `--worker_mode` | 不设置 | 必须设置，标识Worker模式 |
| `--master` | 不设置（自己就是Master） | Master地址 |
| `--role` | 不适用 | hybrid / storage_only |

### 3.2 Master启动流程

1. C++层初始化Master TCP Server
2. 导出Master Python接口（pybind11）
3. 执行用户Python脚本
4. Python脚本中调用业务任务

### 3.3 Worker启动流程

1. C++层初始化Worker TCP Client + Storage
2. 导出Python接口
3. 连接Master，注册
4. 进入等待任务循环

### 3.4 launch_workers接口设计

三个独立接口，支持多种启动方式。**所有接口均为异步非阻塞**：启动Worker进程后立即返回，不等待Worker连接或就绪。

```python
# user_tasks.py
from fly import master, Database, get_config

# 获取全局单例Config并设置参数（必须在启动worker前）
config = get_config()
config.set(
    heartbeat_timeout=120,
    backup_threshold=100,
    aggregation_threshold=1024 * 1024,
    track_writes=1,           # 启用写入跟踪，记录每个任务写入的对象列表
    data_server_threads=2,    # Data Server线程池大小，默认1
)

# Database指定存储路径
db = Database(storage_path="/data")

# 1. SSH方式启动（非阻塞，立即返回）
master.launch_ssh_workers(
    workers=[
        {"host": "192.168.1.10", "role": "hybrid"},
        {"host": "192.168.1.11", "role": "hybrid"},
    ],
    ssh_user="root",
    ssh_key="/path/to/key",
)

# 2. 本地方式启动（非阻塞，立即返回）
master.launch_local_workers(
    workers=[
        {"role": "hybrid"},
        {"role": "storage_only"},
    ],
)

# 3. 自定义方式启动（非阻塞，立即返回）
master.launch_custom_workers(
    workers=[
        {"role": "hybrid"},
    ],
    submit_command="bsub -q normal -P my_project",
)

# 启动worker后再设置config会报错
config.set(heartbeat_timeout=60)  # RuntimeError: Config must be set before workers are launched
```

**启动命令与异步策略**：

| 接口 | 生成的启动命令 | 异步策略 |
|------|--------------|---------|
| `launch_ssh_workers` | `ssh -i {key} {user}@{host} 'nohup fly --worker_mode --master {addr} --role {role} > /dev/null 2>&1 &'` | `subprocess.Popen` + SSH后台执行，立即返回 |
| `launch_local_workers` | `subprocess.Popen(["fly", "--worker_mode", "--master", addr, "--role", role])` | `subprocess.Popen` 创建子进程，立即返回 |
| `launch_custom_workers` | `subprocess.Popen(..., shell=True)` 执行 `{submit_command} 'fly --worker_mode --master {addr} --role {role}'` | `subprocess.Popen`，立即返回 |

**关键点**：
- 所有`launch_*`接口内部统一使用`subprocess.Popen`，创建子进程后立即返回，不等待子进程退出
- SSH方式通过`nohup ... &`确保远程Worker在后台运行，SSH命令本身立即完成
- Worker的注册就绪通过心跳机制确认，而非launch接口阻塞等待
- Master的Reactor事件循环在launch接口返回后才启动

---

## 四、配置管理

### 4.1 Config单例模式

```python
from fly import get_config

# 获取全局单例Config（无需创建实例）
config = get_config()  # 返回唯一单例实例

# 设置参数（必须在启动worker前）
config.set(
    master_port=8000,
    heartbeat_timeout=120,
    heartbeat_interval=5,
    backup_threshold=100,
    aggregation_threshold=1048576,      # 1MB
    large_file_threshold=10485760,      # 10MB
    block_size=134217728,               # 128MB
)

# 再次调用get_config()返回同一个实例
config2 = get_config()  # config2 == config
```

### 4.2 C++层Config实现

```cpp
class Config {
public:
    static Config& instance() {
        static Config config;  // 静态局部变量，保证单例
        return config;
    }
    
    void set_int(const std::string& key, int64_t value) {
        if (workers_launched_) {
            throw std::runtime_error("Config must be set before workers are launched");
        }
        int_values_[key] = value;
    }
    
    void set_str(const std::string& key, const std::string& value) {
        if (workers_launched_) {
            throw std::runtime_error("Config must be set before workers are launched");
        }
        str_values_[key] = value;
    }
    
    int64_t get_int(const std::string& key) const {
        auto it = int_values_.find(key);
        auto default_it = INT_DEFAULTS.find(key);
        if (it != int_values_.end()) return it->second;
        if (default_it != INT_DEFAULTS.end()) return default_it->second;
        throw std::runtime_error("Unknown config key: " + key);
    }
    
    const std::string& get_str(const std::string& key) const {
        auto it = str_values_.find(key);
        auto default_it = STR_DEFAULTS.find(key);
        if (it != str_values_.end()) return it->second;
        if (default_it != STR_DEFAULTS.end()) return default_it->second;
        static const std::string empty = "";
        return empty;
    }
    
    void mark_workers_launched() { workers_launched_ = true; }
    bool is_workers_launched() const { return workers_launched_; }
    
    void reset();  // 测试用
    
private:
    Config() { int_values_ = INT_DEFAULTS; str_values_ = STR_DEFAULTS; }
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    std::map<std::string, int64_t> int_values_;
    std::map<std::string, std::string> str_values_;
    bool workers_launched_ = false;
    
    static const std::map<std::string, int64_t> INT_DEFAULTS;
    static const std::map<std::string, std::string> STR_DEFAULTS;
};

// nanobind导出（通过FLY_EXPORT_*宏封装，方便后续替换）
NB_MODULE(_fly_core, m) {
    nb::class_<Config>(m, "Config")
        .def(nb::init<>())
        .def("set_int", &Config::set_int)
        .def("set_str", &Config::set_str)
        .def("get_int", &Config::get_int)
        .def("get_str", &Config::get_str)
        .def("mark_workers_launched", &Config::mark_workers_launched);
    
    m.def("get_config", []() { return &Config::instance(); });
}
```

---

## 五、任务系统

### 5.1 任务定义

使用 `@as_task` 装饰器将普通函数包装为任务：

```python
@as_task(inputs=lambda db, name: [f"input/{name}"])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = cpp_algorithm(raw)
    db.write_object(f"output/{name}.result", result)

# 直接调用即可提交任务（异步，无返回值）
process_data(db, "a.csv")
```

### 5.2 @as_task装饰器内部逻辑

1. 拦截函数调用
2. 打包函数引用 + 参数 + 依赖列表
3. 发送消息至Master注册任务
4. 立即返回（不阻塞）

### 5.3 递归任务

任务可以递归提交任务，依赖声明模式：

```python
@as_task(inputs=lambda db, name: [f"input/{name}"])
def parent_task(db, name):
    child_task(db, name)  # 异步调用，无返回值
    # 子任务输出路径由业务约定
    
@as_task(inputs=lambda db, name: [f"output/{name}.result"])  # 依赖子任务输出
def next_task(db, name):
    result = db.read_object(f"output/{name}.result")
    ...
```

### 5.4 任务调度策略

- **默认策略**: FIFO
- **Worker选择**: 数据locality优先，尽量调度到数据所在的Worker
- **核心约束**: Worker同一时刻最多执行一个任务。Master仅向`is_busy=false`的Worker派发任务，不向忙碌Worker发送TaskAssignMessage

> **为什么Worker不需要任务队列**：由于Master不向忙碌Worker派发任务，Worker收到TaskAssignMessage时一定处于空闲状态。因此`task_slot_`（而非`task_queue_`）足以担任Main Thread与TaskExecutorThread之间的跨线程信号——最多存放1个任务，无需缓冲。

---

## 六、存储层设计

### 6.1 核心组件

- `Database` 类：统一存储接口（C++实现 + pybind11导出）
- `DataWriter`：单线程写入聚合器
- `DataReader`：从聚合文件中提取数据
- `Serializer`：序列化模块（支持shared_ptr对象）

### 6.2 文件命名规则

统一的聚合文件命名（小文件 + 大文件分块都存储在聚合文件中）：

```
存储目录结构（base_path，共享文件系统）：
/data/
├── aggregated_w1_001.dat      # Worker1的数据文件（可多个）
├── aggregated_w1_002.dat
├── aggregated_w1.idx          # Worker1的唯一索引文件
├── aggregated_w2_001.dat      # Worker2的数据文件
├── aggregated_w2_002.dat
├── aggregated_w2.idx          # Worker2的唯一索引文件
├── merged.idx                 # 合并后的聚合索引（freeze后生成）
├── _FROZEN                    # 冻结标识文件（freeze时创建）
├── _META                      # 数据库元信息（freeze时创建，含Worker信息和launch命令）
└── ...

本地存储目录结构（data_path，可选，Worker本地磁盘）：
/local_ssd/
├── aggregated_w1_001.dat      # Worker1写入的本地数据
├── aggregated_w1.idx          # Worker1的本地索引
└── ...
```

**关键设计**：每个Worker只有一个索引文件（`.idx`），而不是每个数据文件对应一个索引文件。索引条目中包含 `file_name` 字段，指向该条目所属的数据文件。

### 6.3 索引文件结构

每个Worker一个索引文件（二进制格式），记录该Worker所有数据文件中的所有对象：

```
索引条目结构：
object_name (string) | file_name (string) | offset (int64) | size (int64) | is_large (bool) | block_count (int)

示例（aggregated_w1.idx）：
"output/a.result" | "aggregated_w1_001.dat" | 0          | 524288    | false | 0      # 小文件
"large_data.bin"  | "aggregated_w1_001.dat" | 524288     | 134217728 | true  | 10     # 大文件分块存储
"output/b.result" | "aggregated_w1_002.dat" | 0          | 102400    | false | 0      # 另一个数据文件中的小文件
```

- 小文件：按 `file_name` 找到对应 `.dat` 文件，再按 offset/size 读取
- 大文件：is_large=true，分块连续存储在对应 `.dat` 文件中
- 新数据文件滚动创建：当前 `.dat` 文件超过阈值时创建新的

### 6.4 Database接口

```cpp
class Database {
public:
    Database(const std::string& base_path, const std::string& data_path = "");
    
    std::shared_ptr<Object> read_object(const std::string& path);
    
    void write_object(
        const std::string& path,
        std::shared_ptr<Object> data,
        bool backup = false
    );
    
    // 数据库冻结：标记为只读，触发Master后处理
    void freeze();
    
    bool is_frozen() const { return is_frozen_; }
    
    // 元信息加载（用于已冻结Database的恢复）
    DbMeta load_meta() const;               // 读取_META文件
    std::string get_db_id() const { return db_id_; }
    std::string get_base_path() const { return base_path_; }
    std::string get_data_path() const { return data_path_.empty() ? base_path_ : data_path_; }

private:
    std::string base_path_;   // 共享存储路径（所有节点可访问）
    std::string data_path_;    // 本地存储路径（可选，用于高性能本地写入）
    std::string db_id_;        // 路径本身作为唯一标识
    bool is_frozen_ = false;   // 是否已冻结
};

// 数据库元信息
struct DbMeta {
    std::string db_id;
    std::string base_path;
    int64_t created_at;
    int64_t frozen_at;
    struct WorkerInfo {
        uint64_t worker_id;
        std::string host;
        std::string role;
        std::string data_path;
        std::string idx_file;
        int64_t idx_entry_count;
        std::string launch_command;
    };
    std::vector<WorkerInfo> workers;
};
```

**双路径设计**：

| 路径 | 说明 | 必填 |
|------|------|------|
| `base_path` | 共享存储路径，所有Master/Worker可访问。freeze后聚合idx写入此路径 | 是 |
| `data_path` | 本地磁盘路径，可选。启用时write_object写入此路径，read_object优先查找此路径 | 否 |

- 未设置`data_path`时：`data_path = base_path`，所有读写走共享路径
- 设置`data_path`时：写入走本地路径（高性能），读取优先查本地路径再查共享路径
- freeze时：在`base_path`创建标识文件（所有节点可见），`data_path`上的数据文件不需搬迁
```

### 6.5 写入流程

```python
db.write_object(f"output/{name}.result", result)
```

1. 检查数据库是否已冻结：若`is_frozen_==true`则抛出异常
2. 对象序列化（支持shared_ptr零拷贝）
3. 判断大小：小于阈值聚合，大于阈值分块存储
4. 写入`data_path`（若设置）或`base_path`的聚合文件（文件名含worker_id）
5. 更新`data_path`（若设置）或`base_path`的索引文件
6. 向Master发送数据就绪消息

### 6.6 读取流程

```python
data = db.read_object(f"output/{name}")
```

1. 优先使用`merged.idx`（若存在，freeze后生成）
2. 若merged.idx不存在，回退到按Worker idx查找
3. 根据`file_name`定位数据文件：
   - 若设置了`data_path`，优先在`data_path`中查找
   - 若`data_path`中不存在，在`base_path`中查找
4. 按offset和size读取数据

---

## 七、数据副本策略

### 7.1 副本配置

- 用户可在write_object时选择是否备份：`backup=True`
- Master检测高频访问数据，自动发起备份任务
- `track_writes`为全局配置（通过Config设置），启用后write_object自动记录写入对象列表

### 7.2 备份任务调度

- 有专用存储Worker时：优先调度到专用Worker
- 无专用Worker时：使用独立高优先级队列

---

## 七点五、Database Freeze与后处理

### 7.5.1 概述

Database Freeze允许任务在所有数据写入完成后，将数据库标记为只读状态并触发后处理流程：
1. 任务调用`db.freeze()`，数据库变为只读，创建标识文件
2. Worker发送`DatabaseFreezeMessage`至Master
3. Master在后台收集所有相关Worker的idx文件内容
4. Master合并idx文件，写入db的`base_path`目录中
5. 合并后的聚合idx简化后续`load db`流程

### 7.5.2 freeze()流程

```python
db = Database("/data/project_a", data_path="/ssd/local_a")
# ... 写入数据 ...
db.freeze()  # 冻结数据库
```

**freeze()内部执行步骤**：

1. C++侧设置`is_frozen_ = true`
2. 在`base_path`目录创建标识文件`_FROZEN`（所有节点可见），内容为冻结时间戳和db_id
3. 发送`DatabaseFreezeMessage`至Master，携带db_id和当前Worker的idx信息
4. 后续对`db.write_object()`的调用抛出`RuntimeError: Database is frozen`

**标识文件格式**（`base_path/_FROZEN`）：

```
db_id=/data/project_a
frozen_at=1715600000
frozen_by_worker=1
```

### 7.5.3 数据库元信息持久化

为保证freeze后的数据库可在下次运行时正确加载，`base_path`中保存元信息文件`_META`，记录该数据库涉及的所有Worker及其数据分布。

**`_META`文件格式**（JSON，存储在`base_path/_META`）：

```json
{
    "db_id": "/data/project_a",
    "base_path": "/data/project_a",
    "created_at": 1715500000,
    "frozen_at": 1715600000,
    "workers": [
        {
            "worker_id": 1,
            "host": "192.168.1.10",
            "role": "hybrid",
            "data_path": "/ssd/local_a",
            "idx_file": "aggregated_w1.idx",
            "idx_entry_count": 1500,
            "launch_command": "ssh -i /path/to/key root@192.168.1.10 'nohup fly --worker_mode --master {addr} --role hybrid > /dev/null 2>&1 &'"
        },
        {
            "worker_id": 2,
            "host": "192.168.1.11",
            "role": "hybrid",
            "data_path": "/ssd/local_b",
            "idx_file": "aggregated_w2.idx",
            "idx_entry_count": 800,
            "launch_command": "ssh -i /path/to/key root@192.168.1.11 'nohup fly --worker_mode --master {addr} --role hybrid > /dev/null 2>&1 &'"
        }
    ]
}
```

**关键设计**：
- `_META`文件在freeze时由Master写入`base_path`（共享路径），所有节点可读
- `launch_command`记录原始启动命令，下次运行时可复用这些命令启动Worker
- `data_path`记录每个Worker的数据本地存储路径，便于后续读取时定位数据文件

**元信息更新时机**：
1. **Worker注册时**：Master记录Worker的host、role等信息，不写入_META（仅内存）
2. **数据写入时**：Master记录哪些Worker在该db上写入了数据（仅内存）
3. **freeze时**：Master收集所有相关信息，一次性写入`base_path/_META`

### 7.5.4 已冻结数据库的加载与Worker自动启动

当程序打开一个已冻结的Database时（`_FROZEN`文件存在），系统需要根据`_META`中的信息自动启动所需Worker。

**加载流程**：

```
用户脚本打开已冻结Database：
├─ Database(base_path) 构造函数检测到 _FROZEN 文件
├─ 设置 is_frozen_ = true（只读模式）
├─ 读取 _META 文件，获取数据分布信息
├─ 读取 merged.idx（若存在）用于高效读取
└─ 返回只读Database实例

用户显式恢复冻结Database（可选）：
├─ db = Database(base_path)
├─ db.is_frozen() == True → 确认这是一个已冻结的db
├─ master.launch_workers_for_db(db)
│   ├─ 读取 _META 中的 workers 列表
│   ├─ 对每个worker，使用其 launch_command 启动Worker
│   ├─ Worker启动后，连接Master并注册
│   └─ Worker的data_path（本地路径）上的数据文件仍可用
└─ 后续任务可正常 read_object 从该db读取数据
```

**自动启动接口**：

```python
# fly/master.py
def launch_workers_for_db(db: Database) -> None:
    """
    根据已冻结Database的_META信息，自动启动所需Worker。
    仅启动_META中记录的Worker，确保数据可达。
    
    仅当db.is_frozen()为True时有效。
    若_META文件不存在或格式错误，抛出异常。
    """
    if not db.is_frozen():
        raise RuntimeError("Database is not frozen, launch_workers_for_db only works on frozen databases")
    
    meta = db.load_meta()  # 读取 _META 文件
    
    for worker_info in meta['workers']:
        if worker_info['host'] == 'localhost' or worker_info['host'] == '127.0.0.1':
            # 本地Worker
            subprocess.Popen(worker_info['launch_command'].format(addr=master_addr), shell=True)
        else:
            # 远程Worker，使用原始launch_command
            subprocess.Popen(worker_info['launch_command'].format(addr=master_addr), shell=True)
```

**使用示例**：

```python
from fly import Database, master

# 场景1：首次运行，创建数据库并写入数据
db = Database("/shared/project_a", data_path="/local_ssd/project_a")
# ... 任务写入数据 ...
db.freeze()  # 冻结，触发后处理（_META + merged.idx 生成）

# 场景2：下次运行，打开已冻结数据库进行分析
db = Database("/shared/project_a")
if db.is_frozen():
    # 自动启动拥有该db数据的Worker
    master.launch_workers_for_db(db)
    
    # 等待Worker连接就绪（心跳机制确认）
    master.wait_for_workers(db.get_meta_worker_ids())

# 后续任务可正常读取数据
@as_task(inputs=lambda db, name: [f"output/{name}"])
def analyze(db, name):
    data = db.read_object(f"output/{name}")  # 自动查找数据位置
```

### 7.5.3 只读约束

- `db.freeze()`后，所有`write_object()`调用抛出异常
- `db.read_object()`正常工作，允许后续任务读取冻结db中的数据
- 标识文件`_FROZEN`在`base_path`中，所有节点可检测数据库是否已冻结
- Worker启动时扫描`base_path`中的`_FROZEN`文件，对已冻结db直接设置`is_frozen_=true`

### 7.5.4 新增消息类型

```
Worker → Master:
├─ DatabaseFreezeMessage    (数据库冻结通知)

Master → Worker:
├─ IdxRequestMessage        (请求Worker的idx文件内容)

Worker → Master:
├─ IdxResponseMessage       (返回idx文件内容)
```

**消息结构体**：

```cpp
// Worker通知Master数据库已冻结
struct DatabaseFreezeMessage {
    MessageHeader header;
    uint64_t worker_id;
    std::string db_id;              // 冻结的数据库标识
    std::string base_path;          // 共享存储路径
    std::string data_path;          // 实际数据存储路径（可能为本地路径）
    std::string idx_file_path;      // 本Worker的idx文件路径
    int64_t idx_entry_count;        // idx条目数量
};

// Master请求Worker发送idx文件内容
struct IdxRequestMessage {
    MessageHeader header;
    std::string db_id;              // 请求的数据库标识
};

// Worker返回idx文件内容
struct IdxResponseMessage {
    MessageHeader header;
    uint64_t worker_id;
    std::string db_id;
    std::string idx_content;        // idx文件完整内容（二进制）
    int64_t entry_count;            // 条目数量
};
```

### 7.5.5 后处理流程（Master后台执行）

```
db.freeze()触发后处理：

Worker端：
├─ db.freeze() 被调用
├─ 设置 is_frozen_ = true
├─ 在 base_path 创建 _FROZEN 标识文件
├─ 发送 DatabaseFreezeMessage 到 Master
└─ Worker后续对已冻结db的write_object()调用抛出异常

Master端（后台任务）：
├─ 收到 DatabaseFreezeMessage
├─ 记录：db_id + 哪些Worker持有该db的数据
├─ 依次向相关Worker发送 IdxRequestMessage
├─ 收集所有 IdxResponseMessage
├─ 合并所有Worker的idx条目到聚合idx：
│   ├─ 读取每个Worker的idx内容
│   ├─ 合并所有条目，按object_name排序
│   └─ 修正file_name字段：保留原始文件名（含worker_id前缀）
├─ 将聚合idx写入 base_path/merged.idx
├─ 收集所有Worker信息（host、role、data_path、launch_command等）
├─ 将数据库元信息写入 base_path/_META
└─ 后处理完成
```

**聚合idx文件**（`base_path/merged.idx`）：

```
合并后的索引结构（与单个Worker的idx条目格式相同）：
object_name (string) | file_name (string) | offset (int64) | size (int64) | is_large (bool) | block_count (int)

示例（merged.idx）：
"output/a.result" | "aggregated_w1_001.dat" | 0          | 524288    | false | 0
"large_data.bin"  | "aggregated_w1_001.dat" | 524288     | 134217728 | true  | 10
"output/c.result" | "aggregated_w2_001.dat" | 0          | 8192      | false | 0
"output/d.result" | "aggregated_w3_001.dat" | 0          | 4096      | false | 0
```

**关键设计**：
- 聚合idx仅添加，不删除原始idx文件
- 后续`read_object`优先使用`merged.idx`（一次查找代替按Worker查找）
- 若`merged.idx`不存在，回退到按Worker idx查找的原始流程
- `merged.idx`写入`base_path`（共享路径），所有节点可读

### 7.5.6 远程idx收集

当Worker位于不同机器时，`data_path`为本地磁盘，idx文件也在本地。Master通过消息机制收集：

```cpp
// Master端后处理伪代码
void DatabaseFinalizer::finalize(const std::string& db_id) {
    // 1. 查询哪些Worker在该db上写入过数据
    auto workers = metadata_->get_workers_with_db_data(db_id);
    
    // 2. 请求每个Worker的idx内容
    std::vector<std::string> idx_contents;
    for (auto& worker_id : workers) {
        IdxRequestMessage req;
        req.db_id = db_id;
        send(worker_id, encode(req));
        
        // 等待响应
        auto resp = wait_for_response(worker_id, db_id);
        idx_contents.push_back(resp.idx_content);
    }
    
    // 3. 合并idx
    auto merged = merge_idx_entries(idx_contents);
    
    // 4. 写入base_path（共享文件系统）
    std::string base_path = get_db_base_path(db_id);
    write_merged_idx(base_path + "/merged.idx", merged);
}
```

### 7.5.7 使用示例

```python
from fly import Database, get_config

# 创建db，指定共享路径和本地路径
db = Database("/shared/project_a", data_path="/local_ssd/project_a")

@as_task(inputs=lambda db, name: [f"input/{name}"])
def process(db, name):
    raw = db.read_object(f"input/{name}")
    result = cpp_algorithm(raw)
    db.write_object(f"output/{name}", result, backup=True)

# 阶段1：所有数据处理完成
process(db, "file1")
process(db, "file2")

# 阶段2：冻结database，触发后处理
# 必须确保所有写入该db的任务都已完成
db.freeze()

# 后续任务可以读取冻结db的数据，但不能写入
@as_task(inputs=lambda db, name: [f"output/{name}"])
def analyze(db, name):
    data = db.read_object(f"output/{name}")  # 正常工作
    # db.write_object(...)  # RuntimeError: Database is frozen
```

---

## 八、容错机制

### 8.1 任务失败处理

| 错误类型 | 处理方式 |
|---------|---------|
| 可恢复错误（读文件失败等） | 固定次数重试 |
| 不可恢复错误（崩溃、算法错误） | 上报Master，全局退出 |

### 8.2 Worker失联处理

- 心跳间隔：默认5秒，超时后逐步*2，最大30秒
- 超时判定：默认2分钟（可配置）
- 失联Worker上的任务重新调度到其他Worker
- Worker标记为不可达
- 收到失联Worker消息后发送清理任务
- 清理完毕后Worker重新加入可调度节点

### 8.3 任务执行超时

- 不设置最大执行时间限制

---

## 九、通信层设计

### 9.1 通信协议

- TransportLayer抽象层（支持未来替换TCP为UDP/RDMA等）
- 默认使用TCP Socket实现
- 消息通过定义结构体 + 序列化方式发送
- 复用存储层序列化方式，降低维护成本

### 9.2 TransportLayer抽象

```cpp
// src/core/cpp/transport.h

// 传输层抽象接口
class TransportLayer {
public:
    virtual ~TransportLayer() = default;
    
    // 服务端操作
    virtual void listen(const std::string& address, int port) = 0;
    virtual void accept() = 0;
    
    // 客户端操作
    virtual void connect(const std::string& address, int port) = 0;
    
    // 数据收发
    virtual ssize_t send(uint64_t conn_id, const std::string& data) = 0;
    virtual ssize_t recv(uint64_t conn_id, std::string& buffer) = 0;
    
    // 连接管理
    virtual void close(uint64_t conn_id) = 0;
    virtual void close_all() = 0;
    
    // 轮询（用于事件循环）
    virtual std::vector<TransportEvent> poll(int timeout_ms) = 0;
};

// 传输事件
struct TransportEvent {
    enum Type { CONNECT, DATA, DISCONNECT };
    Type type;
    uint64_t conn_id;
    std::string data;
};

// 工厂函数：根据配置创建传输层实例
std::unique_ptr<TransportLayer> create_transport(const std::string& type);

// 默认TCP实现
class TCPTransport : public TransportLayer {
    // ... TCP Socket实现
};
```

**使用方式**：

```cpp
// Reactor通过工厂函数创建传输层
auto transport = create_transport(config.get("transport_type"));  // "tcp" / "udp" / "rdma"
transport->listen("0.0.0.0", config.get("master_port"));

// 事件循环中使用poll
while (running_) {
    auto events = transport->poll(100);  // 100ms超时
    for (const auto& event : events) {
        handle_event(event);
    }
}
```

**Config新增参数**：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `transport_type` | `"tcp"` | 传输层类型：tcp / udp / rdma |

### 9.3 消息类型分类

```
Worker → Master:
├─ RegisterMessage        (Worker注册)
├─ HeartbeatMessage       (心跳)
├─ TaskCompleteMessage    (任务完成)
├─ TaskFailedMessage      (任务失败)
├─ DataReadyMessage       (数据就绪)
├─ UpdateAttributesMessage (更新Worker属性)
├─ DatabaseFreezeMessage  (数据库冻结通知)
├─ IdxResponseMessage     (返回idx文件内容)
└─ CleanupCompleteMessage (清理完成)

Master → Worker:
├─ RegisterAckMessage     (注册确认)
├─ TaskAssignMessage      (任务分配)
├─ DataLocationMessage    (数据位置查询响应)
├─ BackupTaskMessage      (备份任务)
├─ CleanupTaskMessage     (清理任务)
├─ IdxRequestMessage      (请求Worker的idx文件内容)
└─ ShutdownMessage        (全局退出)

Both Direction:
├─ TaskSubmitMessage      (任务提交，任意节点→Master)
└─ DataQueryMessage       (数据位置查询，任意节点→Master)

Worker → Worker:
├─ DataRequestMessage     (数据请求)
└─ DataResponseMessage    (数据响应)
```

### 9.4 核心消息结构体

```cpp
// 消息头
struct MessageHeader {
    MessageType type;
    uint32_t message_id;
    uint64_t timestamp;
};

// Worker注册消息
struct RegisterMessage {
    MessageHeader header;
    uint64_t worker_id;
    std::string role;                        // "hybrid" | "storage_only"
    std::vector<std::string> attributes;     // Worker属性列表
};

// 心跳消息
struct HeartbeatMessage {
    MessageHeader header;
    uint64_t worker_id;
    std::vector<uint64_t> running_tasks;
    std::vector<std::string> attributes;
};

// 任务提交消息（任意节点发送）
struct TaskSubmitMessage {
    MessageHeader header;
    uint64_t task_id;
    std::string task_name;                   // 函数名或@task_name指定
    std::string task_module;
    std::vector<std::string> args;
    std::vector<std::string> inputs;         // 数据依赖列表
    std::vector<std::string> required_attributes;  // Worker属性依赖
};

// 任务分配消息
struct TaskAssignMessage {
    MessageHeader header;
    uint64_t task_id;
    std::string task_name;
    std::string task_module;
    std::vector<std::string> args;
    std::map<std::string, DataLocation> input_locations;
};

// 任务完成消息
struct TaskCompleteMessage {
    MessageHeader header;
    uint64_t task_id;
    uint64_t worker_id;
    std::vector<std::string> written_objects;  // 当Config中track_writes启用时填充
};

// 任务失败消息
struct TaskFailedMessage {
    MessageHeader header;
    uint64_t task_id;
    uint64_t worker_id;
    bool recoverable;
    std::string error_message;
};

// 数据就绪消息
struct DataReadyMessage {
    MessageHeader header;
    uint64_t worker_id;
    std::string data_path;
    DataLocation location;
};

// 更新Worker属性消息
struct UpdateAttributesMessage {
    MessageHeader header;
    uint64_t worker_id;
    std::vector<std::string> add_attributes;
    std::vector<std::string> remove_attributes;
};

// 数据位置信息（Master存储的简化版）
struct DataLocation {
    uint64_t worker_id;
    std::string file_path;
    std::string object_name;
};

// Worker间数据请求
struct DataRequestMessage {
    MessageHeader header;
    std::string object_name;
};

// Worker间数据响应
struct DataResponseMessage {
    MessageHeader header;
    std::string object_name;
    std::shared_ptr<Object> data;
};
```

### 9.5 数据读取流程

```
db.read_object("input/a.csv")：

场景1: 数据在当前Worker存在
├─ Worker查询本地索引（offset、size、分块信息）
├─ 直接从本地磁盘读取
└─ 返回数据

场景2: 数据在其他Worker
├─ Worker向Master发送 DataQueryMessage
├─ Master返回简化 DataLocation（worker_id + file_path + object_name）
├─ Worker向目标Worker发送 DataRequestMessage
├─ 目标Worker查询本地索引，读取数据
├─ 目标Worker发送 DataResponseMessage
└─ 当前Worker接收数据
```

---

## 十、Agent架构设计

### 10.1 架构分层

```
Shared Modules (复用):
├─ Config          (全局配置)
├─ Database        (存储层)
├─ Serializer      (序列化器)
├─ MessageProtocol (消息协议)
├─ TransportLayer  (传输层抽象，支持TCP/UDP/RDMA替换)
├─ TaskRegistry    (任务注册表)
└─ Logger          (日志)

MasterAgent:
├─ TransportLayer  (TCP传输层实例)
├─ TaskScheduler   (任务调度)
├─ MetadataManager (元数据管理)
├─ HeartbeatMonitor (心跳监控)
├─ WorkerManager   (Worker管理)
├─ BackupManager   (备份管理)

WorkerAgent:
├─ TransportLayer  (TCP传输层实例)
├─ TaskRunner      (任务执行)
├─ LocalIndex      (本地索引)
├─ HeartbeatSender (心跳发送)
├─ WorkerContext   (Worker属性管理)
├─ TransportLayer  (数据传输服务，响应其他Worker请求)
```

### 10.2 MasterAgent

```cpp
class MasterAgent {
public:
    MasterAgent();
    void run();  // 主循环
    
private:
    Config& config_;
    std::unique_ptr<TaskRegistry> task_registry_;
    std::unique_ptr<Serializer> serializer_;
    std::unique_ptr<MessageProtocol> protocol_;
    
    std::unique_ptr<TransportLayer> transport_;  // 通过create_transport()创建
    std::unique_ptr<TaskScheduler> scheduler_;
    std::unique_ptr<MetadataManager> metadata_;
    std::unique_ptr<HeartbeatMonitor> heartbeat_;
    std::unique_ptr<WorkerManager> workers_;
    std::unique_ptr<BackupManager> backup_;
    
    void handle_message(const Message& msg);
    void schedule_tasks();
    void check_heartbeats();
};

class TaskScheduler {
public:
    void submit_task(const TaskSubmitMessage& msg);
    TaskAssignMessage get_next_task(const WorkerInfo& worker);
    bool has_ready_tasks() const;
private:
    std::queue<TaskInfo> ready_queue_;
    std::map<uint64_t, TaskInfo> pending_;
    DependencyGraph dependency_graph_;
};

class MetadataManager {
public:
    void record_data(uint64_t task_id, const std::string& path, const DataLocation& loc);
    DataLocation query_data(const std::string& path);
    void record_task(uint64_t task_id, const std::string& task_name);
private:
    std::map<std::string, DataLocation> data_locations_;
    std::map<uint64_t, std::string> task_names_;
};

class WorkerManager {
public:
    void register_worker(uint64_t worker_id, const RegisterMessage& msg);
    void update_attributes(uint64_t worker_id, const UpdateAttributesMessage& msg);
    WorkerInfo get_worker(uint64_t worker_id);
    std::vector<WorkerInfo> get_available_workers();
    void mark_worker_busy(uint64_t worker_id, uint64_t task_id);
    void mark_worker_free(uint64_t worker_id);
private:
    std::map<uint64_t, WorkerInfo> workers_;
};

// Worker状态信息
struct WorkerInfo {
    uint64_t worker_id;
    std::string role;                        // "hybrid" | "storage_only"
    std::vector<std::string> attributes;
    bool is_busy;                             // 是否正在执行任务
    uint64_t current_task_id;                 // 正在执行的任务ID（0表示空闲）
    double last_heartbeat;                    // 最近心跳时间戳
};
```

### 10.3 WorkerAgent

```cpp
class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const std::string& master_addr, const std::string& role);
    void run();  // 主循环
    
private:
    Config& config_;
    std::unique_ptr<TaskRegistry> task_registry_;
    std::unique_ptr<Serializer> serializer_;
    std::unique_ptr<MessageProtocol> protocol_;
    std::unique_ptr<Database> database_;
    
    std::unique_ptr<TransportLayer> transport_;  // 通过create_transport()创建
    std::unique_ptr<TaskRunner> runner_;
    std::unique_ptr<LocalIndex> local_index_;
    std::unique_ptr<HeartbeatSender> heartbeat_;
    std::unique_ptr<WorkerContext> context_;
    std::unique_ptr<TransportLayer> data_transport_;  // Worker间数据传输
    
    void handle_message(const Message& msg);
    void execute_task(const TaskAssignMessage& msg);
};

class WorkerContext {
public:
    void add_attribute(const std::string& attr);
    void remove_attribute(const std::string& attr);
    std::vector<std::string> get_attributes() const;
    uint64_t get_worker_id() const;
private:
    uint64_t worker_id_;
    std::vector<std::string> attributes_;
    TransportLayer* transport_;
};

class IndexEntry {
public:
    std::string object_name;
    std::string file_name;  // 所属数据文件名
    int64_t offset;
    int64_t size;
    bool is_large;
    int block_count;
};

class LocalIndex {
public:
    void add_entry(const std::string& name, const std::string& file_name, 
                   int64_t offset, int64_t size, bool is_large, int block_count);
    IndexEntry query(const std::string& name);
private:
    std::map<std::string, IndexEntry> entries_;
};
```

### 10.4 程序入口

```cpp
int main(int argc, char* argv[]) {
    bool worker_mode = has_flag(argc, argv, "--worker_mode");
    
    if (worker_mode) {
        uint64_t worker_id = generate_worker_id();
        std::string master_addr = get_arg(argc, argv, "--master");
        std::string role = get_arg(argc, argv, "--role");
        
        WorkerAgent agent(worker_id, master_addr, role);
        agent.run();
    } else {
        std::string script_path = argv[1];
        
        init_python();
        execute_script(script_path);
        
        MasterAgent agent;
        agent.run();
    }
}
```

---

## 十一、任务依赖图管理

### 11.1 DependencyGraph结构

```cpp
class DependencyGraph {
public:
    void add_task(uint64_t task_id, const std::vector<std::string>& inputs);
    void mark_data_ready(const std::string& data_path);
    std::vector<uint64_t> get_ready_tasks();
    bool is_task_ready(uint64_t task_id);
    void remove_task(uint64_t task_id);
    
private:
    std::map<uint64_t, std::vector<std::string>> task_dependencies_;
    std::map<std::string, bool> data_ready_status_;
    std::map<uint64_t, int> pending_count_;  // 未满足依赖计数
};
```

### 11.2 依赖图更新流程

```
任务提交:
├─ TaskSubmitMessage到达Master
├─ scheduler.submit_task(msg)
├─ dependency_graph_.add_task(task_id, inputs)
├─ 计算每个依赖的就绪状态
├─ 若全部就绪 → 加入ready_queue_
└─ 否则 → 加入pending_，记录pending_count_

数据就绪:
├─ DataReadyMessage到达Master
├─ dependency_graph_.mark_data_ready(data_path)
├─ 查找依赖此数据的pending任务
├─ 减少这些任务的pending_count_
└─ 若pending_count_归零 → 加入ready_queue_

任务完成:
├─ TaskCompleteMessage到达Master
├─ dependency_graph_.remove_task(task_id)
├─ workers_.mark_worker_free(worker_id)
└─ 清理相关记录
```

### 11.3 Locality优化策略

```cpp
// Worker选择：优先选择输入数据在本Worker的任务
TaskInfo TaskScheduler::select_by_locality(const WorkerInfo& worker) {
    // locality_score = 输入数据在worker本地的比例
    int calculate_locality_score(const TaskInfo& task, const WorkerInfo& worker);
    
    // 从ready_queue中选择得分最高的任务
    TaskInfo best_task;
    int best_score = -1;
    for (const auto& task : candidates) {
        int score = calculate_locality_score(task, worker);
        if (score > best_score) {
            best_score = score;
            best_task = task;
        }
    }
    return best_task;
}
```

---

## 十二、Python与C++交互

### 12.1 大对象传递

- 使用shared_ptr实现内存共享
- 一般对象都使用shared_ptr传递
- 小对象可使用拷贝方式导出

### 12.2 存储层实现

- C++实现存储层核心逻辑
- 通过pybind11导出Database接口
- 支持C++算法直接读写存储层

---

## 十三、任务状态管理

### 13.1 任务状态

- **Pending**: 任务已注册，等待依赖满足
- **Ready**: 依赖已满足，等待调度
- **Running**: 已分配Worker，正在执行
- **Completed**: 执行成功完成
- **Failed**: 执行失败（可恢复）
- **Error**: 不可恢复错误，触发全局退出

### 13.2 数据就绪检测

- write_object完成后立即标记就绪
- 异步备份，不阻塞任务执行

---

## 十四、Python侧任务注册机制

### 14.1 TaskRegistry使用场景

| 节点 | 是否需要TaskRegistry | 作用 |
|------|---------------------|------|
| Master | 不需要 | Master不执行任务，只负责调度 |
| Worker | 需要 | Worker收到TaskAssignMessage后，需根据task_name找到函数执行 |

### 14.2 任务生命周期流程

```
任务定义阶段（Master节点）：
├─ @as_task装饰器解析
├─ 装饰器不做注册操作
├─ 装饰器返回包装函数
└─ 函数标记：_fly_is_task, _fly_task_name

任务提交阶段（调用函数）：
├─ process_data(db, "a.csv")
├─ 包装函数拦截调用
├─ 打包：task_name + task_module + args + inputs
├─ 发送TaskSubmitMessage到Master
└─ 立即返回（无返回值）

任务执行阶段（Worker节点）：
├─ Worker收到TaskAssignMessage
├─ 加载Python模块（task_module）
├─ 模块加载时，@as_task装饰器执行
├─ 从模块获取任务函数
├─ 执行原始函数（_fly_original_func）
└─ 发送TaskCompleteMessage
```

### 14.3 @as_task装饰器实现

```python
# fly/task.py
def as_task(inputs=None):
    def decorator(func):
        task_name = getattr(func, '_fly_task_name', func.__name__)
        module_name = func.__module__
        
        # 标记为任务函数
        func._fly_is_task = True
        func._fly_task_name = task_name
        func._fly_inputs_func = inputs
        
        # 包装函数：拦截调用，立即发送到Master
        def wrapper(*args, **kwargs):
            # 计算依赖
            deps = inputs(*args, **kwargs) if inputs else []
            
            # 序列化参数
            import pickle
            serialized_args = pickle.dumps((args, kwargs))
            
            # 立即发送到Master
            from fly import get_connection
            get_connection().send_task_submit(
                task_name=task_name,
                task_module=module_name,
                args=serialized_args,
                inputs=deps,
            )
            return None  # 无返回值
        
        wrapper._fly_is_task = True
        wrapper._fly_task_name = task_name
        wrapper._fly_module_name = module_name
        wrapper._fly_original_func = func
        
        return wrapper
    return decorator
```

### 14.4 @task_name装饰器

```python
# fly/task.py
def task_name(name):
    def decorator(func):
        func._fly_task_name = name
        return func
    return decorator

# 使用示例
@as_task(inputs=lambda db, name: [f"input/{name}"])
@task_name("data_processor")
def process_data(db, name):
    ...
```

### 14.5 Worker侧任务执行

```python
# Worker收到TaskAssignMessage后
def load_and_execute(task_name, task_module, serialized_args):
    # 1. 加载模块
    if task_module not in loaded_modules:
        import importlib
        module = importlib.import_module(task_module)
        loaded_modules[task_module] = module
    
    # 2. 获取任务函数
    module = loaded_modules[task_module]
    func = getattr(module, task_name)
    
    # 3. 反序列化参数
    import pickle
    args, kwargs = pickle.loads(serialized_args)
    
    # 4. 执行原始函数
    func._fly_original_func(*args, **kwargs)
```

---

## 十五、序列化协议

### 15.1 序列化场景

| 场景 | 序列化方式 | 说明 |
|------|-----------|------|
| 任务参数 | pickle | Python对象，标准库 |
| 通信消息 | zpp_bits | C++结构体，通过FLY_SERIALIZE_*宏封装 |
| 存储对象 | zpp_bits | C++对象，通过FLY_SERIALIZE_*宏封装 |

> **设计原则**：所有C++序列化均通过FLY_SERIALIZE_*宏封装，底层使用zpp_bits实现。宏抽象确保未来可无缝替换为其他序列化库（如cereal、protobuf等），只需修改宏定义，不影响业务代码。

### 15.2 序列化宏定义

```cpp
// fly/serialization_macros.h
// 底层序列化库：zpp_bits（C++20原生，高性能二进制序列化）
// 通过宏抽象，方便后续替换底层实现

#pragma once

#include <zpp_bits.h>
#include <sstream>
#include <string>
#include <vector>
#include <map>

// ==================== 序列化库配置 ====================
// zpp_bits: 单头文件C++20序列化库，使用constexpr + concepts
// 替换序列化库时，只需修改此文件中的宏定义

// ==================== 序列化宏 ====================

// 声明可序列化类型（使用zpp_bits的serialize函数模式）
// 使用方式：在结构体中添加 FLY_SERIALIZE_DECLARE() { FLY_SERIALIZE_FIELDS(x, y, z); }
#define FLY_SERIALIZE_DECLARE() \
    constexpr static auto serialize(auto& archive)

// 序列化多个字段
#define FLY_SERIALIZE_FIELDS(...) archive(__VA_ARGS__);

// 序列化基类（zpp_bits通过模板参数支持基类序列化）
#define FLY_SERIALIZE_BASE(base_class) \
    archive(cereal::base_class<base_class>(this));
// 注意：zpp_bits基类序列化不同于cereal，后续需适配

// 编码消息为字符串
#define FLY_ENCODE(msg, output) \
    do { \
        auto [data, out] = zpp::bits::data_out(); \
        out(msg).or_throw(); \
        output = std::string(data.begin(), data.end()); \
    } while(0)

// 解码消息从字符串
#define FLY_DECODE(data, msg_type, output) \
    do { \
        std::vector<unsigned char> buf(data.begin(), data.end()); \
        auto in = zpp::bits::in(buf); \
        msg_type msg; \
        in(msg).or_throw(); \
        output = std::move(msg); \
    } while(0)

// 流式编码
#define FLY_ENCODE_STREAM(file_stream, msg) \
    do { \
        auto [data, out] = zpp::bits::data_out(); \
        out(msg).or_throw(); \
        file_stream.write(reinterpret_cast<const char*>(data.data()), data.size()); \
    } while(0)

// 流式解码
#define FLY_DECODE_STREAM(file_stream, msg_type, output) \
    do { \
        std::vector<unsigned char> buf; \
        /* 读取数据并反序列化 */ \
        auto in = zpp::bits::in(buf); \
        msg_type msg; \
        in(msg).or_throw(); \
        output = std::move(msg); \
    } while(0)
```

### 15.3 消息结构体示例

```cpp
// 使用宏定义消息结构体（zpp_bits风格）
struct RegisterMessage : MessageBase {
    uint64_t worker_id;
    std::string role;
    std::vector<std::string> attributes;
    
    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(worker_id, role, attributes);
    }
};

struct TaskSubmitMessage : MessageBase {
    uint64_t task_id;
    std::string task_name;
    std::string task_module;
    std::string serialized_args;  // pickle序列化的参数
    std::vector<std::string> inputs;
    std::vector<std::string> required_attributes;
    
    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(task_id, task_name, task_module, serialized_args, inputs, required_attributes);
    }
};
```

> **zpp_bits vs cereal 对比**：
> - **性能**：zpp_bits序列化速度约为cereal的14倍（benchmark: 733ms vs 10777ms）
> - **体积**：zpp_bits生成的二进制更紧凑（8.4KB vs 10.4KB）
> - **C++20原生**：zpp_bits使用concepts和constexpr，与C++20 Modules兼容
> - **模块友好**：单头文件，无宏依赖，可轻松通过`import`或头文件方式引入
> - **宏抽象保障**：FLY_SERIALIZE_*宏封装了所有序列化API调用，替换底层库只需修改宏定义文件

---

## 十六、Python导出宏

### 16.1 导出宏定义

```cpp
// fly/export_macros.h
// 底层绑定库：nanobind（C++20兼容，pybind11继任者）
// 通过宏抽象，方便后续替换底层实现

#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <memory>
#include <string>

namespace nb = nanobind;

// ==================== Pickle导出宏 ====================

#define FLY_EXPORT_PICKLE(class_type) \
    .def(nb::pickle( \
        [](const class_type& obj) { \
            std::string serialized; \
            FLY_ENCODE(obj, serialized); \
            return nb::bytes(serialized); \
        }, \
        [](nb::bytes bytes) { \
            std::string data = bytes.c_str(); \
            class_type obj; \
            FLY_DECODE(data, class_type, obj); \
            return obj; \
        } \
    ))

#define FLY_EXPORT_PICKLE_SHARED_PTR(class_type) \
    .def(nb::pickle( \
        [](const std::shared_ptr<class_type>& obj) { \
            std::string serialized; \
            FLY_ENCODE(*obj, serialized); \
            return nb::bytes(serialized); \
        }, \
        [](nb::bytes bytes) { \
            std::string data = bytes.c_str(); \
            auto obj = std::make_shared<class_type>(); \
            FLY_DECODE(data, class_type, *obj); \
            return obj; \
        } \
    ))

// ==================== 类导出宏 ====================

#define FLY_EXPORT_CLASS_WITH_NAME(module, class_type, export_name, ...) \
    nb::class_<class_type>(module, export_name) \
        .def(nb::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE(class_type)

#define FLY_EXPORT_CLASS(module, class_type, ...) \
    FLY_EXPORT_CLASS_WITH_NAME(module, class_type, #class_type, __VA_ARGS__)

#define FLY_EXPORT_CLASS_SHARED_PTR_WITH_NAME(module, class_type, export_name, ...) \
    nb::class_<class_type, std::shared_ptr<class_type>>(module, export_name) \
        .def(nb::init<>()) \
        __VA_ARGS__ \
        FLY_EXPORT_PICKLE_SHARED_PTR(class_type)

#define FLY_EXPORT_CLASS_SHARED_PTR(module, class_type, ...) \
    FLY_EXPORT_CLASS_SHARED_PTR_WITH_NAME(module, class_type, #class_type, __VA_ARGS__)

// ==================== 属性与方法导出宏 ====================

#define FLY_EXPORT_ATTR(name, member) \
    .def_rw(#name, member)

#define FLY_EXPORT_ATTR_WITH_NAME(name, member) \
    .def_rw(name, member)

#define FLY_EXPORT_METHOD(name, func) \
    .def(#name, func)

#define FLY_EXPORT_METHOD_WITH_NAME(name, func) \
    .def(name, func)

// ==================== 模块导出宏 ====================
// nanobind使用NB_MODULE宏（module_避免C++20 module关键字冲突）

#define FLY_EXPORT_MODULE_BEGIN(module_name) \
    NB_MODULE(module_name, m)

#define FLY_EXPORT_MODULE_END()
```

> **nanobind vs pybind11 对比**：
> - **C++20兼容**：nanobind使用`module_`类名（末尾下划线），避免与C++20 `module`关键字冲突
> - **性能**：编译速度提升4x，二进制体积减少5x，运行时开销降低10x
> - **API兼容**：与pybind11语法几乎相同，迁移成本低
> - **宏抽象保障**：FLY_EXPORT_*宏封装了所有nanobind API调用，替换底层库只需修改宏定义文件

### 16.2 使用示例

```cpp
// C++类定义
struct MyData {
    int a;
    double b;
    std::vector<float> data;
    
    FLY_SERIALIZE_DECLARE() {
        FLY_SERIALIZE_FIELDS(a, b, data);
    }
};

// Python导出（使用nanobind宏）
FLY_EXPORT_MODULE_BEGIN(_fly_example) {
    // 使用类名作为导出名称
    FLY_EXPORT_CLASS(m, MyData,
        FLY_EXPORT_ATTR(a, &MyData::a)
        FLY_EXPORT_ATTR(b, &MyData::b)
        FLY_EXPORT_ATTR(data, &MyData::data)
    );
    
    // 自定义导出名称
    FLY_EXPORT_CLASS_WITH_NAME(m, MyData, "DataObj",
        FLY_EXPORT_ATTR(a, &MyData::a)
        FLY_EXPORT_ATTR(b, &MyData::b)
    );
    
    // shared_ptr类
    FLY_EXPORT_CLASS_SHARED_PTR_WITH_NAME(m, BigData, "Processor",
        FLY_EXPORT_ATTR(values, &BigData::values)
        FLY_EXPORT_METHOD(process, &BigData::process)
    );
}
FLY_EXPORT_MODULE_END()
```

---

## 十七、模块目录结构

### 17.1 三层结构设计

每个模块包含三个子目录：

```
src/
├── core/               # 核心基础模块
│   ├── cpp/            # C++类型定义和实现
│   │   ├── config.cpp
│   │   ├── storage_manager.cpp
│   │   ├── database.cpp
│   │   ├── local_index.cpp
│   │   ├── transport.cpp         # TransportLayer实现（TCP等）
│   │   ├── serializer.cpp
│   ├── export/         # pybind11导出
│   │   ├── core_export.cpp
│   │   ├── CMakeLists.txt
│   ├── py/             # Python包
│   │   ├── __init__.py
│   │   ├── connection.py
│   │   ├── protocol.py
│   │   ├── context.py
│
├── master/             # Master节点模块
│   ├── cpp/            # C++底层实现
│   │   ├── dependency_graph.cpp
│   ├── export/         # pybind11导出
│   │   ├── master_export.cpp
│   ├── py/             # Python主循环
│   │   ├── __init__.py
│   │   ├── master_agent.py    # 主循环
│   │   ├── scheduler.py
│   │   ├── metadata.py
│   │   ├── worker_manager.py
│   │   ├── heartbeat.py
│   │   ├── launcher.py
│
├── worker/             # Worker节点模块
│   ├── cpp/            # C++底层实现
│   │   ├── data_writer.cpp
│   │   ├── data_reader.cpp
│   ├── export/         # pybind11导出
│   │   ├── worker_export.cpp
│   ├── py/             # Python主循环
│   │   ├── __init__.py
│   │   ├── worker_agent.py    # 主循环
│   │   ├── task_executor.py
│   │   ├── heartbeat.py
│   │   ├── data_server.py
│
├── task/               # 任务定义模块
│   ├── py/
│   │   ├── __init__.py
│   │   ├── decorator.py       # @as_task, @task_name
│   │   ├── registry.py
│
├── serialization/      # 序列化模块
│   ├── cpp/
│   │   ├── serialization_macros.h
│   │   ├── message_types.h
│   ├── export/
│   │   ├── serialization_export.cpp
│   ├── py/
│   │   ├── __init__.py
│   │   ├── protocol.py
│
├── export/             # 导出宏模块
│   ├── cpp/
│   │   ├── export_macros.h
│
└── main.cpp            # 程序入口（仅初始化Python解释器）
```

### 17.2 层次职责

| 目录 | 职责 |
|------|------|
| `cpp/` | C++类型定义、核心算法实现，不直接操作py::object |
| `export/` | pybind11导出，将C++类/函数暴露给Python |
| `py/` | Python流程控制、主循环、消息解析 |

---

## 十八、程序入口与启动模式

### 18.1 启动参数

```bash
# Master模式启动
fly user_tasks.py              # 正常模式，执行完毕后自动退出
fly -i user_tasks.py           # 交互模式，进入Python REPL

# Worker模式启动
fly --worker_mode --master addr:port --role hybrid
fly --worker_mode --master addr:port --role storage_only
```

| 参数 | 说明 |
|------|------|
| `-i` | Master交互模式，执行脚本后进入Python REPL |
| `--worker_mode` | Worker模式标识 |
| `--master` | Master地址（Worker模式必填） |
| `--role` | Worker角色：hybrid / storage_only |

### 18.2 main.cpp实现

```cpp
// main.cpp
#include <pybind11/embed.h>

int main(int argc, char* argv[]) {
    py::scoped_interpreter guard{};
    
    bool worker_mode = has_flag(argc, argv, "--worker_mode");
    bool interactive = has_flag(argc, argv, "-i");
    
    py::object fly_module = py::module::import("fly");
    
    if (worker_mode) {
        std::string master_addr = get_arg(argc, argv, "--master");
        std::string role = get_arg(argc, argv, "--role");
        fly_module.attr("start_worker")(master_addr, role);
    } else {
        std::string script_path = argv[interactive ? 2 : 1];
        
        // 执行用户脚本
        py::object importlib = py::module::import("importlib");
        py::object spec = importlib.attr("util").attr("spec_from_file_location")("user_script", script_path);
        py::object user_module = importlib.attr("util").attr("module_from_spec")(spec);
        spec.attr("loader").attr("exec_module")(user_module);
        
        // 根据模式启动
        if (interactive) {
            fly_module.attr("start_master_interactive")();
        } else {
            fly_module.attr("start_master")();
        }
    }
    
    return 0;
}
```

### 18.3 程序退出机制

| 模式 | 退出条件 | 行为 |
|------|---------|------|
| **正常模式** | 用户脚本执行完毕 + 所有任务完成 | 后处理 → 正常退出 |
| **交互模式** | 用户输入 `quit()` | 退出REPL → 等待任务完成 → 后处理 → 正常退出 |
| **交互模式** | 用户输入 `force_quit()` | 立即停止 → 退出（忽略未完成任务） |

### 18.4 Python侧启动实现

```python
# fly/__init__.py
import code
import sys
import threading

def start_master():
    """正常模式启动"""
    from fly_master import MasterReactor
    
    agent = MasterReactor()
    agent.start()
    
    # 等待所有任务完成
    agent.wait_for_all_tasks_complete()
    
    # 后处理
    agent.cleanup()
    
    # 退出
    agent.stop()

def start_master_interactive():
    """交互模式启动"""
    from fly_master import MasterReactor
    
    agent = MasterReactor()
    agent.start()
    
    exit_flag = threading.Event()
    
    def do_quit():
        """正常退出：退出交互模式，等待任务完成"""
        print("Exiting interactive mode...")
        print("Waiting for all tasks to complete...")
        exit_flag.set()
        raise SystemExit(0)
    
    def do_force_quit():
        """强制退出：立即停止，忽略未完成任务"""
        print("Force quitting...")
        agent.force_stop()
        sys.exit(0)
    
    # 导入用户脚本变量
    import user_script
    local_vars = {
        'master': agent,
        'quit': do_quit,
        'exit': do_quit,
        'force_quit': do_force_quit,
    }
    for name in dir(user_script):
        if not name.startswith('_'):
            local_vars[name] = getattr(user_script, name)
    
    console = code.InteractiveConsole(locals=local_vars)
    
    try:
        console.interact(
            banner="Fly Master Interactive Mode\nCommands:\n  quit()        - Exit and wait for tasks\n  force_quit()  - Force exit immediately",
            exitmsg=""
        )
    except SystemExit:
        pass
    
    # 等待任务完成
    print("Waiting for all tasks to complete...")
    agent.wait_for_all_tasks_complete()
    
    print("Running cleanup...")
    agent.cleanup()
    
    print("Stopping Master...")
    agent.stop()

def start_worker(master_addr: str, role: str):
    """启动Worker"""
    from fly_worker import WorkerReactor
    from fly_core import StorageManager
    from fly.worker.task_executor import TaskExecutorThread
    
    storage = StorageManager()
    reactor = WorkerReactor(master_addr, role, storage)
    reactor.start()
    
    executor = TaskExecutorThread(reactor, storage)
    executor.start()
    
    executor.join()
    reactor.stop()
```

### 18.5 MasterReactor C++接口

```cpp
// src/master/cpp/master_reactor.h
class MasterReactor {
public:
    void start();
    void stop();
    
    // 等待所有任务完成（正常模式/交互模式quit使用）
    void wait_for_all_tasks_complete();
    
    // 强制停止（交互模式force_quit使用）
    void force_stop();
    
    // 后处理：通知Worker、保存元数据等
    void cleanup();
    
    // 任务状态查询
    int get_pending_task_count();
    int get_running_task_count();
};
```

```cpp
// src/master/cpp/master_reactor.cpp
void MasterReactor::wait_for_all_tasks_complete() {
    while (running_) {
        int pending = scheduler_->get_pending_count();
        int running = scheduler_->get_running_count();
        
        if (pending == 0 && running == 0) {
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void MasterReactor::force_stop() {
    running_ = false;
    
    // 立即停止所有线程
    if (heartbeat_thread_.joinable()) heartbeat_thread_.detach();
    if (scheduler_thread_.joinable()) scheduler_thread_.detach();
    
    // 关闭连接
    transport_->close_all();
}

void MasterReactor::cleanup() {
    // 1. 通知所有Worker停止
    ShutdownMessage msg;
    msg.header.type = MessageType::Shutdown;
    msg.reason = "normal_exit";
    broadcast(protocol_->encode(msg));
    
    // 2. 保存元数据（可选）
    metadata_->save_to_disk();
    
    // 3. 等待Worker断开
    std::this_thread::sleep_for(std::chrono::seconds(2));
}
```

### 18.6 交互模式使用示例

```python
# user_tasks.py
from fly import master, Database
from fly.task import as_task

db = Database("/data")

master.launch_local_workers([{"role": "hybrid"}])

@as_task(inputs=lambda db, name: [f"input/{name}"])
def process(db, name):
    data = db.read_object(f"input/{name}")
    result = algorithm(data)
    db.write_object(f"output/{name}", result)

process(db, "file1")
process(db, "file2")
```

```bash
# 正常模式
$ fly user_tasks.py
# → 执行脚本 → 等待file1/file2完成 → cleanup → 退出

# 交互模式
$ fly -i user_tasks.py
Fly Master Interactive Mode
Commands:
  quit()        - Exit and wait for tasks
  force_quit()  - Force exit immediately

>>> process(db, "file3")          # 继续提交任务
>>> process(db, "file4")
>>> master.get_pending_task_count()  # 查看状态
2
>>> quit()
Exiting interactive mode...
Waiting for all tasks to complete...
Running cleanup...
Stopping Master...
# → 等待file1-4全部完成 → 正常退出

# 强制退出
>>> force_quit()
Force quitting...
# → 立即退出，忽略未完成任务
```

---

## 十九、Reactor模式架构

### 19.1 线程模型设计原则

- **消息处理全在C++侧**：Master和Worker的消息处理（解析、路由、元数据更新、调度决策）都是快速C++操作，不需要线程池
- **单线程事件循环**：使用select/poll/epoll处理多连接，一个线程即可处理所有Worker的消息
- **唯一GIL线程**：只有Worker的Python任务执行线程涉及GIL
- **任务单slot传递**：Master不向忙碌Worker派发任务，Worker使用`task_slot_`（而非队列）传递任务至TaskExecutorThread
- **数据服务线程池**：Worker的Data Server采用线程池模式，默认单线程，可通过Config配置线程数，以支持同时处理多个远程读请求
- **后台线程**：心跳等定时器驱动模块使用独立线程

### 19.2 整体架构

```
Master Node:
┌─────────────────────────────────────────────────────────────────┐
│                    Main Thread (C++)                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Event Loop:                                                 ││
│  │   1. accept新连接 (epoll/select)                            ││
│  │   2. recv消息 → 解析 → 调用Handler处理                     ││
│  │   3. send响应/广播                                          ││
│  │   4. 回到1                                                  ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                  │
│  ┌──────────────────┐  ┌──────────────────────────────────────┐│
│  │ Heartbeat Thread │  │ Scheduler Thread                      ││
│  │ (心跳检测)       │  │ (定期备份检查/任务调度)               ││
│  └──────────────────┘  └──────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘

Worker Node:
┌─────────────────────────────────────────────────────────────────┐
│                    Main Thread (C++)                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Event Loop:                                                 ││
│  │   1. recv Master消息 → 解析 → 路由处理                      ││
│  │      - TaskAssign → 设置task_slot_，通知TaskExec线程        ││
│  │      - DataRequest → 加入read_request_queue                 ││
│  │      - Shutdown → 设置running=false                         ││
│  │   2. 回到1                                                  ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                  │
│  ┌──────────────────┐  ┌──────────────────────────────────────┐│
│  │ Heartbeat Thread │  │ Data Server Thread Pool                ││
│  │ (心跳发送)       │  │ (响应其他Worker的数据请求)             ││
│  │                  │  │ 默认1线程，可配置 (data_server_threads)││
│  └──────────────────┘  └──────────────────────────────────────┘│
│                                                                  │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Task Execution Thread (Python)                                ││
│  │ (唯一涉及GIL，从task_slot_取任务执行)                        ││
│  └──────────────────────────────────────────────────────────────┘│
│                                                                  │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ Read Request Queue (C++)                                      ││
│  │ DataRequest消息 → 排队 → Data Server线程池取请求执行         ││
│  └──────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘

总计：
  Master: 3线程（1 Main + 1 Heartbeat + 1 Scheduler）
  Worker: 3 + N线程（1 Main + 1 Heartbeat + N DataServer + 1 TaskExec）
          N默认为1（data_server_threads配置）
```

### 19.3 为什么Master不需要消息处理线程池

| 操作 | 性质 | 是否需要多线程 |
|------|------|---------------|
| 消息解析 | 快速C++操作（反序列化+路由） | 不需要 |
| 元数据更新 | 快速内存操作（map查找/插入） | 不需要 |
| 调度决策 | 快速计算（依赖图查询+FIFO） | 不需要 |
| 心跳处理 | 定时器驱动 | 独立线程，不在事件循环中 |

### 19.4 Worker Data Server线程池设计

| 操作 | 性质 | 是否需要多线程 |
|------|------|---------------|
| 消息解析/路由 | 快速C++操作 | 不需要 |
| 任务路由 | 设置task_slot_，通知TaskExec线程 | 不需要 |
| 远程数据读取 | 磁盘I/O + 网络传输，可能较慢 | **需要线程池** |

**为什么Data Server需要线程池**：
- 远程读请求涉及磁盘I/O和网络传输，处理时间不可控
- 默认单线程可满足简单场景，配置多线程支持高并发读取
- 主线程仅将DataRequest加入read_request_queue，不阻塞事件循环
- Data Server线程池从队列取请求执行，线程数通过Config的`data_server_threads`配置

**关键点**：
- Master Main Thread用**select/poll/epoll**处理多连接，一个线程处理所有Worker消息
- Worker Main Thread仅做消息解析和路由，将DataRequest加入队列后立即返回
- Data Server线程池从read_request_queue取请求执行，不阻塞主循环
- 心跳（定时器）使用独立线程
- **任务派发约束**：Master仅向空闲Worker发送TaskAssignMessage，Worker收任务时必然空闲，task_slot_始终为0或1

---

## 二十、Reactor核心实现（C++）

### 20.1 Reactor基类

```cpp
// src/core/cpp/reactor.h
class Reactor {
public:
    Reactor(const std::string& transport_type = "tcp");
    
    void start();    // 启动主循环 + 后台线程
    void stop();     // 停止所有线程
    
    void register_handler(MessageType type, 
        std::function<HandlerResult(const std::string&)> handler);
    
    void send(uint64_t target_id, const std::string& msg_bytes);
    void broadcast(const std::string& msg_bytes);

private:
    std::unique_ptr<TransportLayer> transport_;  // 通过create_transport()创建
    std::unique_ptr<MessageProtocol> protocol_;
    
    std::map<MessageType, std::function<HandlerResult(const std::string&)>> handlers_;
    
    bool running_;
    
    // 主循环（事件循环，单线程处理所有消息）
    void main_loop();
};
```

### 20.2 MasterReactor

```cpp
// src/master/cpp/master_reactor.h
class MasterReactor : public Reactor {
public:
    MasterReactor(BackupManager*, MetadataManager*, WorkerManager*, 
                  TaskScheduler*, HeartbeatMonitor*);
    
    void start();  // 启动主循环 + 后台线程
    void stop();
    
    void wait_for_all_tasks_complete();
    void force_stop();
    void cleanup();

private:
    // 后台线程
    std::thread heartbeat_thread_;
    std::thread scheduler_thread_;
    
    void heartbeat_loop();     // 心跳检测
    void scheduler_loop();    // 定期备份检查 + 任务调度
    void init_handlers();
};
```

### 20.3 WorkerReactor

```cpp
// src/worker/cpp/worker_reactor.h
class WorkerReactor : public Reactor {
public:
    WorkerReactor(const std::string& master_addr, const std::string& role,
                  StorageManager*, LocalIndex*);
    
    void start();  // 启动主循环 + 后台线程
    void stop();
    
    // Python线程调用
    TaskAssignMessage wait_for_task();
    void report_task_complete(const TaskCompleteMessage& msg);
    void report_task_failed(const TaskFailedMessage& msg);

private:
    // 后台线程
    std::thread heartbeat_thread_;
    
    // Data Server线程池
    std::vector<std::thread> data_server_pool_;
    int data_server_thread_count_;  // 从Config读取，默认1
    
    // Python任务信号（单slot，Master不向忙碌Worker派发任务）
    std::optional<TaskAssignMessage> task_slot_;
    std::mutex task_slot_mutex_;
    std::condition_variable task_slot_cv_;
    
    // 读请求队列（主线程生产，Data Server线程池消费）
    std::queue<DataRequestMessage> read_request_queue_;
    std::mutex read_request_mutex_;
    std::condition_variable read_request_cv_;
    
    void heartbeat_loop();           // 心跳发送
    void data_server_worker();       // Data Server工作线程函数
    void init_handlers();
};
```

### 20.4 Data Server线程池工作流程

```
Worker主线程收到DataRequest消息：
├─ 解析消息
├─ 将DataRequest加入read_request_queue_
├─ 通知read_request_cv_
└─ 立即返回事件循环（不阻塞）

Data Server线程池工作线程：
├─ 等待read_request_cv_（阻塞直到有请求）
├─ 从read_request_queue_取出一个DataRequest
├─ 查询本地索引（LocalIndex）
├─ 从磁盘读取数据
├─ 通过TransportLayer发送DataResponse
└─ 回到等待

Config配置：
├─ data_server_threads=1  （默认，单线程处理读请求）
├─ data_server_threads=4  （高并发场景，4线程并行处理读请求）
└─ data_server_threads=0  （不允许，至少1线程）
```

---

### 21.1 BackupManager

```cpp
// src/master/cpp/backup_manager.h
class BackupManager {
public:
    BackupManager(WorkerManager* worker_manager, MetadataManager* metadata);
    
    void on_data_ready(const std::string& data_path, 
                       const DataLocation& location, bool backup);
    void record_access(const std::string& data_path);
    void periodic_check(int threshold, int target_replicas);
    uint64_t create_backup_task(const std::string& data_path, 
                                const DataLocation& source_location);
    void on_backup_complete(uint64_t task_id, const std::string& data_path,
                            const DataLocation& location);
    
    std::vector<BackupTaskInfo> get_pending_tasks();

private:
    WorkerManager* worker_manager_;
    MetadataManager* metadata_;
    
    std::map<std::string, int> access_count_;
    std::map<uint64_t, BackupTaskInfo> backup_tasks_;
    
    uint64_t select_backup_worker(uint64_t exclude_worker_id);
};
```

### 21.2 MetadataManager

```cpp
// src/master/cpp/metadata_manager.h
class MetadataManager {
public:
    void record_data_location(const std::string& data_path, 
                              const DataLocation& location);
    DataLocation query_for_read(const std::string& data_path, 
                                uint64_t requester_worker_id);
    int get_replica_count(const std::string& data_path);
    void add_replica(const std::string& data_path, const DataLocation& location);
    
    void record_task_name(uint64_t task_id, const std::string& task_name);
    std::string get_task_name(uint64_t task_id);

private:
    std::map<std::string, std::vector<DataLocation>> data_locations_;
    std::map<uint64_t, std::string> task_names_;
};
```

---

## 二十二、Python侧任务执行线程

### 22.1 TaskExecutorThread（唯一涉及GIL）

```python
# fly/worker/task_executor.py
import threading
import importlib
import pickle

class TaskExecutorThread(threading.Thread):
    """唯一涉及GIL的Python线程"""
    
    def __init__(self, reactor, storage):
        self.reactor = reactor
        self.storage = storage
        self.loaded_modules = {}
    
    def run(self):
        while True:
            # 1. 从C++等待任务（阻塞在task_slot_cv_，不涉及GIL）
            #    Master保证仅向空闲Worker派发任务，因此task_slot_有值时
            #    TaskExecutorThread必然处于等待状态
            task = self.reactor.wait_for_task()
            
            # 2. 设置执行上下文
            self.storage.clear_written_objects()
            
            # 3. 加载模块并执行（涉及GIL）
            module = self._load_module(task.task_module)
            func = getattr(module, task.task_name)._fly_original_func
            args, kwargs = pickle.loads(task.serialized_args)
            
            # 4. 执行任务
            success, error, recoverable = self._execute(func, args, kwargs)
            
            # 5. 报告完成（通知C++）
            if success:
                self.reactor.report_task_complete({
                    "task_id": task.task_id,
                    "written_objects": self.storage.get_written_objects()
                })
            else:
                self.reactor.report_task_failed({
                    "task_id": task.task_id,
                    "recoverable": recoverable,
                    "error_message": error
                })
    
    def _execute(self, func, args, kwargs):
        try:
            func(*args, **kwargs)
            return True, "", False
        except RecoverableError as e:
            return False, str(e), True
        except Exception as e:
            return False, str(e), False
```

### 22.2 启动入口

```python
# fly/__init__.py
def start_worker(master_addr: str, role: str):
    from fly_worker import WorkerReactor
    from fly_core import StorageManager
    from fly.worker.task_executor import TaskExecutorThread
    
    # 1. C++ Reactor（启动所有后台线程）
    storage = StorageManager()
    reactor = WorkerReactor(master_addr, role, storage)
    reactor.start()
    
    # 2. Python任务执行线程（唯一涉及GIL）
    executor = TaskExecutorThread(reactor, storage)
    executor.start()
    
    executor.join()
    reactor.stop()

def start_master():
    from fly_master import MasterReactor
    
    agent = MasterReactor()
    agent.start()
    agent.wait_for_all_tasks_complete()
    agent.cleanup()
    agent.stop()
```

---

## 二十三、多Database支持

### 23.1 Database轻量级设计

```cpp
// database.h
class Database {
public:
    Database(const std::string& base_path, const std::string& data_path = "");
    
    std::shared_ptr<Object> read_object(const std::string& object_name);
    void write_object(const std::string& object_name, std::shared_ptr<Object> data, ...);
    void freeze();
    
    bool is_frozen() const { return is_frozen_; }
    DbMeta load_meta() const;               // 读取_META文件（用于已冻结Database的恢复）
    std::string get_db_id() const { return db_id_; }
    std::string get_base_path() const { return base_path_; }
    std::string get_data_path() const { return data_path_.empty() ? base_path_ : data_path_; }
    
private:
    std::string base_path_;   // 共享存储路径（所有节点可访问）
    std::string data_path_;    // 本地存储路径（可选，用于高性能本地写入）
    std::string db_id_;        // 路径本身作为唯一标识
    bool is_frozen_ = false;   // 是否已冻结
};

// 数据库元信息
struct DbMeta {
    std::string db_id;
    std::string base_path;
    int64_t created_at;
    int64_t frozen_at;
    struct WorkerInfo {
        uint64_t worker_id;
        std::string host;
        std::string role;
        std::string data_path;
        std::string idx_file;
        int64_t idx_entry_count;
        std::string launch_command;
    };
    std::vector<WorkerInfo> workers;
};
```

### 23.2 StorageManager动态创建

```cpp
// storage_manager.h
class StorageManager {
public:
    static StorageManager& instance();
    
    void set_worker_id(uint64_t worker_id);
    
    WriteResult write(const std::string& db_id, const std::string& base_path, 
                      const std::string& object_name, const std::string& data,
                      const std::string& data_path = "");
    
    std::string read(const std::string& db_id, const std::string& base_path,
                     const std::string& object_name,
                     const std::string& data_path = "");
    
    // 数据库冻结相关
    bool is_db_frozen(const std::string& base_path);
    void freeze_db(const std::string& db_id, const std::string& base_path);
    
private:
    std::map<std::string, std::unique_ptr<DataWriter>> writers_;  // db_id -> writer
    std::set<std::string> frozen_dbs_;                              // 已冻结的db_id集合
    
    DataWriter* get_or_create_writer(const std::string& db_id, 
                                      const std::string& base_path,
                                      const std::string& data_path);
};
```

### 23.3 使用示例

```python
# 创建多个Database（轻量级）
db_a = Database("/data/project_a")                           # 仅共享路径
db_b = Database("/data/project_b", data_path="/ssd/local_b") # 共享+本地路径

# 任务中使用不同db
@as_task(inputs=lambda db, name: [f"input/{name}"])
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = cpp_algorithm(raw)
    db.write_object(f"output/{name}.result", result)

process_data(db_a, "file1")
process_data(db_b, "file2")

# 所有数据处理完成后，冻结database
db_a.freeze()  # 标记db_a为只读，触发Master后处理
```

---

## 二十四、设计完成总结

### 24.1 已完成模块

- [x] 整体架构设计
- [x] 启动流程与进程模型
- [x] 配置管理（单例模式）
- [x] 任务系统（@as_task、@task_name装饰器）
- [x] 存储层设计（小文件聚合、大文件分块）
- [x] 通信层消息类型定义
- [x] 序列化协议（cereal统一，宏封装）
- [x] Python导出宏（支持自定义名称）
- [x] Agent架构设计
- [x] 任务依赖图管理
- [x] Python侧任务注册机制
- [x] 多Database支持
- [x] Database Freeze与后处理（双路径、只读冻结、idx合并、_META元信息持久化、Worker自动启动恢复）
- [x] 备份任务管理（C++实现）
- [x] Reactor模式架构（规避GIL）
- [x] 数据副本策略
- [x] 容错机制

### 24.2 核心设计原则

1. **Python主进程**：用户脚本执行、任务定义、装饰器
2. **C++20底层实现**：存储、通信、消息处理、调度（使用C++20 Modules `import`机制）
3. **Reactor模式**：Main Thread事件循环 + 后台线程（心跳/调度/数据服务）
4. **唯一GIL线程**：Worker的Python任务执行线程
5. **任务单slot传递**：Master不向忙碌Worker派发任务，task_slot_而非task_queue_
6. **动态创建**：Database轻量级，StorageManager按需创建writer
7. **Database Freeze**：db.freeze()触发只读冻结，Master后台合并idx，Worker数据不搬迁
8. **双路径存储**：base_path共享路径+data_path本地路径，写入走本地、读取优先本地
9. **宏封装**：
   - FLY_SERIALIZE_*宏封装zpp_bits序列化（支持未来替换为cereal/protobuf等）
   - FLY_EXPORT_*宏封装nanobind绑定（支持未来替换为pybind11/CPython API等）
10. **传输层抽象**：TransportLayer接口支持未来替换TCP为UDP/RDMA
11. **Data Server线程池**：Worker数据服务采用线程池，默认单线程，可配置以支持高并发读请求
12. **C++20 Modules友好架构**：
   - zpp_bits：单头文件、C++20原生、module兼容
   - nanobind：显式避开C++20 `module`关键字冲突
   - Bazel 9.0+支持cc_library的module_interfaces属性

### 24.3 用户使用示例

```python
# user_tasks.py
from fly import master, Database, get_config
from fly.task import as_task, task_name
import cpp_module

# 配置
# 设置配置（track_writes启用写入跟踪）
config = get_config()
config.set(heartbeat_timeout=120, track_writes=1)

# 启动Workers
master.launch_local_workers([
    {"role": "hybrid"},
    {"role": "storage_only"},
])

# 定义任务
@as_task(inputs=lambda db, name: [f"input/{name}"])
@task_name("processor")
def process_data(db, name):
    raw = db.read_object(f"input/{name}")
    result = cpp_module.algorithm(raw)
    db.write_object(f"output/{name}.result", result, backup=True)

# 创建Database并提交任务
db_a = Database("/data/project_a")
db_b = Database("/data/project_b", data_path="/ssd/local_b")

process_data(db_a, "file1")
process_data(db_b, "file2")

# 数据处理完成后冻结
db_a.freeze()
```