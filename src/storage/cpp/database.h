#pragma once

#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/write_back_queue.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <storage/cpp/fly_stream.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/object_cache.h>
#include <serialization/cpp/object_header.h>
#include <core/cpp/config.h>
#include <common/cpp/worker_context.h>
#include <common/cpp/common_types.h>
#include <log/cpp/logger.h>
#include <memory>
#include <stdexcept>
#include <atomic>
#include <cstring>

class Database {
public:
    // existing_db_path 参数已废弃（db_path_ 现在是 db_path 的别名），
    // 保留签名仅为过渡期调用方兼容，值被忽略。
    Database(const CMString& db_path, const CMString& data_path = "", uint64_t writer_id = 0, const CMString& host = "", const CMString& existing_db_path = "");
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // write_pickle_bytes / compress_pickle_bytes / commit_stream 已删除
    // （T2b/T2c 2026-08-31 用户裁定：调用仅存在于测试的 API 已过期——
    // 造数/压缩验证统一走 open_write_stream → finish_and_commit 恒流式）。

    // ── L1 大对象流式写（chunked-transfer-design §9.1）──
    // open_write_stream 返回 sink FlyStream（pickle.dump 流入 → 压缩块直写
    // DataWriter 增量 record，无内存整累积）→ stream.finish_and_commit()
    // 完成编排（trailer + entry + register——时序对齐 commit_write）。
    // 未 commit 析构 = 放弃（残块无 trailer 不可读 + 段事务兜底）。
    // 盘写在任务线程同步进行（write 进 page cache 即返回——与 WBQ 后台
    // execute 的延迟特征一致；WBQ 逐块后台化留作后续优化，决策记录于
    // chunked-transfer-design.md §9 落地修订）。
    // 返回裸指针（export 层 take_ownership 接管）。
    // temp=true（T2d 2026-08-31 temp 写流式化）：pickle.dump 直入
    // temp_writer_ 增量直写（内存 R+常数而非旧 write_temp_pickle 的
    // R+2C 整对象缓冲）；frozen（无 temp writer）返回 nullptr。
    FlyStream* open_write_stream(const CMString& object_name,
                                 const CMString& py_name, bool temp = false);

    // temp 流式写实现（T2d）：temp_writer_ 上的正式路径镜像，commit 回调走
    // on_temp_write_started/on_temp_write 语义（见 database.cpp）。
    FlyStream* open_temp_write_stream(const CMString& object_name,
                                      const CMString& py_name);

    // compress_pickle_bytes 已删除（T2c 2026-08-31，仅测试调用的过期 API）。
    // write_temp_pickle 已删除（T2d 2026-08-31 temp 写流式化，被
    // open_write_stream(temp=true) 取代）。

    // bypass_cache=true skips the low-tier cache lookup (cache="none" mode).
    std::pair<FlyBufferPtr, CMString> read_object_compressed(const CMString& object_name, bool backup = false, bool bypass_cache = false);

    template<typename T>
    fly::WriteErrorType write_object(const CMString& object_name, const T& obj,
                          const CMString& py_name, bool backup = false,
                          bool populate_cache = true);

    // Cache tiers: "low" (default) and "high" populate/query the high-tier
    // cache (省反序列化); "none" bypasses all cache tiers and reads from source.
    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name, const CMString& cache = "low");

    // Returns the py_name (type name) stored in the object header, without
    void backup_object(const CMString& object_name);

    void freeze();
    bool is_frozen() const;

    void remove_object(const CMString& object_name);
    void remove_index_entry(const CMString& object_name);

    // 写入段标记（委托 writer_）。task 写入第一个对象时打 BEGIN，成功时打 END。
    // master 直接 write_object 不打标记（隐式事务，ADD 落盘即生效）。
    void mark_write_begin();
    void mark_write_end();

    // task 异常撤销：清空 WBQ 未落盘的脏写 + idx 打 ABORT + data 文件 truncate 回滚 +
    // 清运行时内存（DataService.local_idx_ / ObjectCache）中本 task 的脏对象。
    // dirty_full_names 是本 task 已写出的对象全名列表（db_path:short_name）。
    void abort_task_writes(const CMVector<CMString>& dirty_full_names);

    void put_temp_data(const CMString& object_name, FlyBufferPtr compressed_data);

    // 删除 db 目录下全部 temp 落盘产物（temp_data_*.dat + *.temp.idx）。
    // freeze 确认后调用（temp=中间态，阶段完成即作废）。幂等（文件不存在 no-op）。
    void cleanup_temp_files();

    // _DB_META（JSON）读写已上移 Python 编排层（storage/py/db_meta.py 的
    // DbMetaFile）：初写在 open_db → _init_chain，读在 load_db/merge_db/
    // worker deserialize_args，WorkerInfo 追加经 master 回调。C++ 不再读写。

    CMString get_db_path() const;
    CMString get_data_path() const;
    // Merge-only：跨机数据集中后产物落在新路径，master 用它更新既有 Database 的路径，
    // 同步 re-register 进 DataService非 merge 场景禁止调用。
    void set_paths(const CMString& db_path, const CMString& data_path);
    CMString get_full_name(const CMString& name) const;
    CMString get_writer_id() const;

    void reset();

    // ---- Var service: lightweight small-object KV managed by this Database ----
    // value is an already-serialized FlyBufferPtr (pickle for Python objects,
    // FLY_ENCODE_TO_BUFFER for C++ exported objects). Zero-copy in-process: the
    // shared FlyBufferPtr is shared between local cache, WorkerAgentContext,
    // and TaskAssignMessage construction.
    //
    // set_var/get_var are synchronous (await master response); remove_var is
    // asynchronous (fire-and-forget). On the master process these go directly
    // to the local authoritative store (master_*); on worker processes they
    // traverse WorkerAgentContext to the master over the network.

    // Sync set. Returns false if db frozen or var already exists (immutable).
    bool set_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name);
    // Sync get. Returns (success, value, type_name). success=false on miss.
    std::tuple<bool, FlyBufferPtr, CMString> get_var(const CMString& var_name);
    // Async remove (local cache cleared immediately; master notified async).
    void remove_var(const CMString& var_name);

    // Worker-side: inject a var into the local cache (from TaskAssignMessage
    // var_payloads). No network. Called before task execution so db.get_var
    // hits the local cache without a round-trip.
    void inject_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name);

    // Master authoritative operations (no network, direct local store access).
    bool master_set_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name);
    std::tuple<bool, FlyBufferPtr, CMString> master_get_var(const CMString& var_name);
    void master_remove_var(const CMString& var_name);
    bool master_has_var(const CMString& var_name);

    // Drop a local cached var (called on immutable-reject broadcast from master).
    void drop_local_var(const CMString& var_name);

private:
    bool check_frozen();
    void create_frozen_marker();
    void ensure_directory_exists(const CMString& path);
    CMString full_name(const CMString& short_name) const;

    struct CompressResult {
        int64_t original_size_;
        int32_t chunk_count_;
    };
    CompressResult compress_buffered_data(const char* data, int64_t data_size,
                                           const CMString& py_name, FlyBuffer& target);

    // Shared backup write logic: takes ownership of compressed_data and writes it
    // as a backup record for the given object.
    void do_backup_write(const CMString& full, const CMString& object_name, CMString compressed_data, const CMString& source_hash = {});

    // Commit a compressed record: cache → register with master → enqueue disk write.
    // Shared by the恒流式 write path and write_object<T>. On registration failure,
    // rolls back cache and local_idx, does not enqueue disk write.
    // populate_cache=false（保存等级"none"）跳过 low-tier 缓存填充，仅落盘。
    fly::WriteErrorType commit_write(const CMString& object_name,
                                     const CMString& full,
                                     FlyBufferPtr record,
                                     int64_t original_size,
                                     int32_t chunk_count,
                                     bool backup,
                                     bool populate_cache = true);

    // Var persistence: flush non-deleted vars to {db_path}/_VARS at freeze time;
    // load them back at construction if the file exists.
    void flush_vars_to_disk();
    void load_vars_from_disk();

    // A single var entry: zero-copy shared value buffer + Python type name.
    struct VarEntry {
        FlyBufferPtr value_;
        CMString type_name_;
    };

    CMString db_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString host_;
    std::atomic<bool> is_frozen_{false};

    CompressionType compression_type_ = CompressionType::NONE;
    int compression_level_ = 0;
    int64_t serialize_chunk_size_ = 4194304;
    // Payloads at or below this size skip compression (raw passthrough). Read
    // from config "compression_threshold" (default 4096).
    int64_t compression_threshold_ = 4096;

    CMUniquePtr<DataWriter> writer_;
    // temp 专用 writer（temp_data_{wid}_{NNN}.dat + {wid}.temp.idx，恒落
    // db_path）。与 writer_ 同生命周期创建：事务段（mark_write_begin/end/abort）
    // 必须在首写之前打标，惰性创建会漏段。空段 abort 是 no-op 安全。
    CMUniquePtr<DataWriter> temp_writer_;
    CMUnorderedSet<CMString> removed_objects_;

    // 自保护锁：保护 db_path_/data_path_/writer_id_/writer_ 操作序列/
    // removed_objects_/freeze 的 check-and-set。master/worker
    // 的容器锁（db_instances_/databases_）外经 shared_ptr 调用本对象方法的
    // 前提（见 DEVELOPMENT_GUIDELINES §13 与锁内 IO 拆除前置）。
    mutable std::mutex state_mutex_;

    // Var store: var_name → VarEntry. Mutex guards all access (set/get/remove
    // may come from task-execution threads; freeze snapshots under the lock).
    CMUnorderedMap<CMString, VarEntry> var_store_;
    mutable std::mutex var_mutex_;
};

template<typename T>
fly::WriteErrorType Database::write_object(const CMString& object_name, const T& obj,
                                const CMString& py_name, bool backup,
                                bool populate_cache) {
    CMString full = full_name(object_name);
    if (check_frozen()) {
        fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB);
        return fly::WriteErrorType::FROZEN_DB;
    }

    // Serialize + compress via streaming pipeline.
    // trailer 格式（§4.4）：块流纯追加，完成后追加 trailer（无占位/回填）。
    auto record = CMMakeShared<FlyBuffer>();
    FlyBufferStreamBuf fly_buf(*record);
    CountingStreamBuf counting_buf(fly_buf);
    std::ostream counting_stream(&counting_buf);

    ObjectHeader header;
    header.compression_type_ = static_cast<uint8_t>(compression_type_);
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());

    int64_t total_uncompressed = 0;
    int32_t chunk_count = 0;
    {
        auto compressor = compression_type_ != CompressionType::NONE
            ? CompressorFactory::create(compression_type_, compression_level_) : nullptr;
        CompressingStreamBuf csbuf(counting_stream, std::move(compressor),
                                    serialize_chunk_size_, compression_threshold_);
        std::ostream os(&csbuf);
        obj.fly_serialize(os);
        os.flush();
        total_uncompressed = csbuf.total_uncompressed();
        chunk_count = csbuf.chunk_count();
        // Small payloads skip compression internally; record the actual format
        // so the read-side picks the matching (de)compressor path.
        header.compression_type_ = static_cast<uint8_t>(csbuf.effective_compression_type());
        header.block_comp_lens_ = csbuf.block_comp_lens();  // B' 块表
    }
    counting_stream.flush();

    header.total_size_ = static_cast<uint64_t>(total_uncompressed);
    header.chunk_count_ = static_cast<uint32_t>(chunk_count);
    CMString trailer = header.serialize_trailer();
    record->write(trailer.data(), trailer.size());

    // Commit: cache → register → enqueue disk write (shared logic).
    return commit_write(object_name, full, record, total_uncompressed, chunk_count,
                        backup, populate_cache);
}

template<typename T>
CMSharedPtr<T> Database::read_object(const CMString& object_name, const CMString& cache) {
    CMString full = full_name(object_name);
    auto& cache_instance = fly::ObjectCache::instance();

    // cache="none": bypass all cache tiers, read directly from source.
    if (cache == "none") {
        FlyBufferPtr comp_data;
        try {
            auto [cd, pn] = read_object_compressed(object_name, false, true);
            comp_data = cd;
        } catch (const fly::DataCorruptionError& e) {
            ERR("read_object<T>: {}", e.what());
            return nullptr;
        }
        if (!comp_data || comp_data->empty()) {
            ERR("read_object<T>: no data for '{}'", full);
            return nullptr;
        }
        auto obj = CMMakeShared<T>();
        DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
        std::istream is(&dsbuf);
        obj->fly_deserialize(is);
        if (dsbuf.checksum_failed()) {
            ERR("[FATAL-DATA-CORRUPTION] read_object<T>: '{}': chunk CRC/trailer verify failed", full);
            return nullptr;
        }
        return obj;
    }

    // cache="low" (default) or cache="high": query high tier first.
    // Hit → return cached instance, skip IO + deserialize.
    if (auto cached = cache_instance.get_high<T>(full)) {
        return cached;
    }

    // Miss → read compressed data (low-tier cache transparent in read_object_compressed).
    // 零容忍（§5）：DataCorruptionError（校验预算耗尽）→ C++ 路径不抛
    //（消费方是测试/内部对象），转 nullptr + ERR。
    FlyBufferPtr comp_data;
    try {
        auto [cd, pn] = read_object_compressed(object_name, false, false);
        comp_data = cd;
    } catch (const fly::DataCorruptionError& e) {
        ERR("read_object<T>: {}", e.what());
        return nullptr;
    }
    if (!comp_data || comp_data->empty()) {
        ERR("read_object<T>: no data for '{}'", full);
        return nullptr;
    }

    auto obj = CMMakeShared<T>();
    DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
    std::istream is(&dsbuf);
    obj->fly_deserialize(is);
    if (dsbuf.checksum_failed()) {
        // 零容忍（§5）：校验失败不产出（部分）反序列化对象。
        ERR("[FATAL-DATA-CORRUPTION] read_object<T>: '{}': chunk CRC/trailer verify failed", full);
        return nullptr;
    }

    // Populate high tier so subsequent reads skip deserialization.
    size_t accounted = comp_data->size();
    {
        ObjectHeader hdr;
        size_t trailer_len = 0;
        if (ObjectHeader::deserialize_trailer({comp_data->data(), comp_data->size()},
                                              hdr, trailer_len) &&
            hdr.total_size_ > 0) {
            accounted = static_cast<size_t>(hdr.total_size_);
        }
    }
    cache_instance.put_high<T>(full, obj, accounted);

    return obj;
}