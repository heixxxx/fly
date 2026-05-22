方案梳理与待澄清设计问题
1. _DB_META 增量更新 — 全量写可行性
数据量估算：
单个 WorkerInfo 字段估算：
字段	典型大小
hostname	~20B
ip_address	~15B
launch_command	~100-200B
worker_id × N	~8B × N
单条 WorkerInfo 约 200-500B。10,000 workers = 2-5MB。
bitsery 序列化后约 150-400B/条（无 JSON 开销），10K 条 ≈ 1.5-4MB。
结论：全量重写完全可行。_DB_META 尺寸远小于任何数据文件，1-5MB 级别的文件读写延迟微秒到毫秒级，不会成为性能瓶颈。
但需要写入原子性保证：先写临时文件，再 rename 替换，防止写入中途崩溃导致元数据丢失。
// 建议的更新方式
void update_db_meta(const CMString& base_path, const DbMeta& meta) {
    CMString encoded;
    FLY_ENCODE(meta, encoded);
    CMString tmp_path = base_path + "/_DB_META.tmp";
    CMString meta_path = base_path + "/_DB_META";
    
    std::ofstream ofs(tmp_path, std::ios::binary);
    ofs.write(encoded.data(), encoded.size());
    ofs.close();
    
    std::filesystem::rename(tmp_path, meta_path);  // atomic on POSIX
}
2. hostname vs ip 唯一性
建议使用 hostname 作为唯一标识：
维度	hostname	IP
唯一性	集群环境下保证唯一（DNS/hosts管理）	一机多IP
稳定性	重启后不变	DHCP 环境可能变
可定位性	需 DNS 解析	直接可连接
用途	唯一标识机器	网络通信地址
建议 DbMeta 中存储 两者：hostname 作为逻辑唯一键，ip 作为网络通信地址。结构改为：
struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString hostname;       // 唯一标识机器
    CMString ip_address;     // 网络通信地址
    CMString launch_command; // Master 生成
    // idx_file, idx_entry_count 不再需要——由 worker_id 和共享目录推导
};
3. 触发时机确认
写入完成时更新 — 对应 WriteRegisterAckMessage 成功后，或 DataReadyMessage 发送时。
但这里有一个微妙时序问题：
当前流程：
Worker: write_object() → enqueue WriteBackQueue
WriteBackQueue: execute (write data + flush idx) → complete (DataService mark COMPLETE+flushed)
Worker: send DataReadyMessage to Master
DataReadyMessage 在 complete 回调中发送，此时数据已落盘。但 _DB_META 更新应该在哪做？
- 方案A：Worker 在 DataReadyMessage 中携带 {hostname, ip, worker_id}，Master 收到后检查并更新 _DB_META
- 方案B：Master 在 write_register 流程中就记录 host 信息
建议方案A，因为 DataReadyMessage 才标志着数据真正落盘完成。
4. _DB_META 结构设计
struct DbMeta {
    CMString db_id;
    CMString base_path;
    int64_t created_at = 0;
    int64_t frozen_at = 0;
    CMVector<WorkerInfo> workers;   // ← 之前为空，现在要填充
    CMVector<CMString> data_paths;  // ← 新增：共享存储上可见的所有 data_path
};
疑问：当前 DbMeta 中 workers 包含 idx_file 和 idx_entry_count 字段。在新设计中：
- idx_file 可以通过 hostname + worker_id 推导（worker_{id}.idx），是否还需要？
- data_path 已经在顶层，每个 WorkerInfo 里的 data_path 是否还需要？
建议精简为：
struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString hostname;
    CMString ip_address;
    CMString launch_command;
    FLY_SERIALIZE(worker_id, hostname, ip_address, launch_command)
};
idx_file 路径 = base_path + "/worker_" + worker_id + ".idx"，由约定推导，无需存储。
5. load_db 流程中的关键问题
5.1 新旧 worker_id 映射
这是最关键的设计问题。
上一个 run 中 worker_1.idx, worker_2.idx 对应 worker_id=1, 2。新 run 启动时：
1. Master 读 _DB_META → 得到 {hostname_A: [1, 3], hostname_B: [2]}
2. Master 在 hostname_A 上 launch 新 worker（新 worker_id=10）
3. Master 在 hostname_B 上 launch 新 worker（新 worker_id=11）
问题：新 worker_id=10 需要读取 worker_1.idx 和 worker_3.idx，但这些文件名用的是旧 ID。
需要一个映射机制：Master 告诉新 worker {old_worker_id → 需要加载哪些 idx 文件}。
当前 RegisterMessage 没有 host 字段，需要扩展。建议：
- RegisterMessage 增加 hostname 字段
- Master 在新 worker 注册后，根据 hostname 匹配旧 worker_id 列表
5.2 idx 文件的共享访问
你说"idx 文件一定存放在共享目录下"。这意味着：
- base_path 在共享存储（如 NFS/Lustre）上
- 所有机器都能访问 base_path/worker_1.idx
新 worker 加载旧 idx 的流程：
1. Master 发送消息给 worker：{需要加载的 idx 文件列表: ["worker_1.idx", "worker_3.idx"]}
2. Worker 从共享目录读取这些 idx 文件，加载到 local_idx
3. Worker 标记所有已加载 entry 为 COMPLETE + flushed
4. Worker 创建新的 worker_{new_id}.idx 用于后续写入
问题：新 worker 的 DataReader 构造时只加载 worker_{自己的writer_id}.idx。但 load_db 后，worker 需要加载旧 worker 的 idx。这需要新接口：
// 新接口：加载指定 idx 文件到 DataService
void load_foreign_idx(const CMString& idx_path, uint64_t old_worker_id);
5.3 idx 落盘时序确认
已验证：数据文件先刷盘，idx 后刷盘。
// DataWriter::flush() — database.h 模板中的 execute 回调
file_stream_.flush();   // 1. 先刷数据
index_->save();         // 2. 再刷索引
所以在 load_db 中加载的 idx，其对应的数据一定已经完整落盘。可以直接标记为 COMPLETE + flushed。
但有一个细节：idx 中是否有"写入中但未 flushed"的记录？
分析流程：
write_typed_object() → 内存 add_entry
flush()              → 数据刷盘 + idx 刷盘(pending_adds 写入文件)
complete callback    → on_write_completed + on_object_flushed
在 flush() 之前，idx 只有内存中的 pending_adds，尚未落盘。所以 idx 文件中的记录，一定对应已经完全写入磁盘的数据。
但还有一层：DataService::local_idx_ 有三种状态：INCOMPLETE → COMPLETE → flushed。idx 文件中的记录不包含这些状态信息。load_db 恢复时，需要统一标记为 COMPLETE + flushed。
5.4 重建 remote_idx
Master 加载所有 idx 文件后，需要重建 DataService::remote_idx_。当前 remote_idx_ 存储 {object_name → {worker_id, host, port}}。
问题是：idx 中的 IndexEntry 包含 host 字段（当前是写入者自己 host），但没有 port。新 worker 注册时才分配 port。
所以 load_db 流程应该是：
1. Master 读 _DB_META + 所有 idx → 得到 {object_name → {old_worker_id, host}}
2. Master 启动所有必要 worker → 新 worker 注册，{hostname → {new_worker_id, port}}
3. Master 更新 remote_idx：{object_name → {new_worker_id, hostname, port}}
即 remote_idx 重建必须在所有 worker 注册完成后才能完成。
6. 总结待确认设计决策
#	问题	现状	建议方案
1	_DB_META 写入方式	一次性(freeze时)	写入完成时全量重写（tmp + rename 保证原子性）
2	唯一机器标识	无	hostname 作为唯一键，同时存 ip_address 供通信
3	触发时机	无	DataReadyMessage 时检查并更新
4	launch_command 来源	无	Master 生成，存入 _DB_META
5	新旧 worker_id 映射	无	Master 发送 {old_worker_ids, idx_files} 给新 worker
6	新 worker 加载旧 idx 方式	无	新增消息类型 + DataService 接口，标记 COMPLETE+flushed
7	remote_idx 重建时序	无	必须在所有 worker 注册完成后重建（需要 host→new_port 映射）
8	idx 落盘时序	数据先刷 → idx 后刷	✅ 已验证，load_db 可安全标记 COMPLETE+flushed
9	WorkerInfo.idx_file 字段	存在	可移除（由 base_path + "/worker_" + worker_id + ".idx" 推导）
核心设计问题：第 5、6、7 点互相依赖——load_db 流程必须顺序执行：读 _DB_META → 启动 worker → 等待注册 → 发送 idx 加载命令 → 重建 remote_idx。