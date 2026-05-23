问题 1：如何确保始终读到新数据
当前问题根源
LocalIndex::find_entry() 返回 front()（最早条目），而 DataService::try_read_local_or_wait() 在本地读时使用 DataReader(paths.base_path, paths.data_path, paths.writer_id) 创建一个新的 DataReader，它只加载自己的 writer_id 的 idx 文件。
关键发现：try_read_local_or_wait 创建的 DataReader 使用 paths.writer_id — 这是 db_paths_ 注册的当前 Worker 的 writer_id。
场景 B 的完整读取路径：
1. 
第一轮：Worker A（writer_id=aaa）写 key_x → aaa.idx 有条目，data_aaa_000.dat 有数据
2. 
重跑：Worker A 再次写 key_x → aaa.idx 有两条条目，data_aaa_000.dat 有两份数据
3. 
读取时：DataReader(base_path, data_path, "aaa") → 加载 aaa.idx → find_entry("key_x") → 返回 front() = 第一次的数据
方案对比
方案
A. find_entry 返回 back()
B. add_entry 去重
C. 写入前检查 is_frozen 或 exists
推荐方案 A：find_entry() 返回 back()。理由：
1. 
语义正确：同一对象多次写入，最后一次为最新值
2. 
改动最小：仅改 LocalIndex::find_entry() 一行
3. 
无副作用：find_all_entries() 不受影响（large object 读全部 block 后排序拼接）
4. 
idx 文件不膨胀：add_entry 在 pending_adds_ 中追加，同一对象多次写入只是 idx 多几条记录，文件不大
5. 
data 文件有浪费但可接受：旧数据占空间，但不影响正确性，可留待 compaction 功能清理
问题 2：load_db 场景下新 Worker 如何加载 idx
load_db 的 idx 加载流程
Master load_db(path)
  → 扫描目录下所有 *.idx 文件 → 得到 [writer_id_1, writer_id_2, ...]
  → send_idx_load_commands(db_id, base_path, writer_ids)
      → 发给所有 Worker（广播）
  
Worker 收到 IdxLoadCommand
  → for each writer_id in msg.writer_ids:
      LocalIndex idx(base_path/writer_id.idx)
      idx.load()  → 加载该 writer_id 的全部 IndexEntry
      ds.restore_entries(db_id, all_entries)
          → local_idx_[object_name].entries.push_back(each entry)
          → 标记 COMPLETE + flushed
场景 B 在 load_db 下的完整情况
假设 Worker A（writer_id=aaa）执行 task 写了 key_x 两次（第一轮 + 重跑），Worker B（writer_id=bbb）正常写了一些数据。
DB 目录：
db_path/
├── _DB_META
├── aaa.idx        # Worker A 的索引，key_x 有两条记录（offset_1, offset_2）
├── data_aaa_000.dat  # Worker A 的数据，key_x 两份数据
├── bbb.idx        # Worker B 的索引
├── data_bbb_000.dat  # Worker B 的数据
load_db 后新 Worker 的 DataService 状态：
restore_entries 不做去重，纯追加：
// data_service.cpp:448
info->entries.push_back(std::move(e));  // 直接追加
所以 local_idx_["db_id:key_x"] 的 entries 向量会有两条 IndexEntry（都指向 aaa.idx 对应的数据位置）。
新 Worker 执行 task 读取 key_x 时：
// try_read_local_or_wait:
DataReader reader(paths.base_path, paths.data_path, paths.writer_id);
// paths.writer_id 是新 Worker 自己的 writer_id（如 "ccc"）
return reader.read_from_entries(info->entries);
但这里有个关键问题：DataReader 只加载 ccc.idx，而 info->entries 中的条目指向 data_aaa_000.dat。
read_from_entries 的流程：
1. 
排序 entries 按 file_name + offset
2. 
find_file_path(entry.file_name) → 在 base_path 下查找 data_aaa_000.dat
3. 
read_from_file(data_aaa_000.dat, offset, size) → 能读到数据
所以读取路径是：entries 指向 aaa 的数据文件 → find_file_path 在 base_path 下找到 → 能读到。
但 read_from_entries 处理多条 entry 的方式是把它们当 large object 的 block 拼接：
// data_reader.cpp:191-196
for (size_t i = 0; i < sorted.size(); ++i) {
    // 每条 entry 都会读取并 decompress
    result.insert(result.end(), decompressed.begin(), decompressed.end());
}
这导致读到的是两次数据的拼接（v1 + v1），而不是最新的一次！
这是 read_from_entries 的语义问题：它假设同一个 object_name 的多条 entry 是 large object 的不同 block，而实际上这里是同一对象写入两次产生的重复。
修复建议
两处修改配合：
修改 1：LocalIndex::find_entry() → back() — 解决本地 Worker 读取问题
修改 2：DataService::restore_entries() → 去重，同一 object_name 只保留最后一条 entry — 解决 load_db 场景
// restore_entries 去重逻辑
for (auto& [object_name, obj_entries] : grouped) {
    auto& info = local_idx_[object_name];
    if (!info) {
        info = CMMakeShared<LocalObjectInfo>();
    }
    info->db_id = db_id;
    // 同一 object_name 只保留最后一条（最新写入）
    if (!obj_entries.empty()) {
        info->entries = {obj_entries.back()};
    }
    info->completion_state = CompletionState::COMPLETE;
    info->flushed = true;
}
这样 load_db 后 local_idx_["db_id:key_x"].entries 只有一条 entry（指向第二次写入的位置），读取时 read_from_entries 只读一条 → 正确。