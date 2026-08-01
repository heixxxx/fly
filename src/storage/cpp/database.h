#pragma once

#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/temp_store.h>
#include <storage/cpp/write_back_queue.h>
#include <storage/cpp/db_meta.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_buffer_stream.h>
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

    fly::WriteErrorType write_pickle_bytes(const CMString& object_name,
                                const char* data, int64_t data_size,
                                const CMString& py_name, bool backup = false);

    fly::WriteErrorType commit_stream(const CMString& object_name,
                                      FlyBufferPtr record,
                                      const CMString& py_name, bool backup = false);

    CMString compress_pickle_bytes(const char* data, int64_t data_size,
                                   const CMString& py_name);

    // Compress + register + store as temp (no disk write, no cache).
    // Avoids C++→Python→C++ roundtrip of compress_pickle_bytes + put_temp_data.
    void write_temp_pickle(const CMString& object_name,
                           const char* data, int64_t data_size,
                           const CMString& py_name);

    // bypass_cache=true skips the low-tier cache lookup (cache="none" mode).
    std::pair<FlyBufferPtr, CMString> read_object_compressed(const CMString& object_name, bool backup = false, bool bypass_cache = false);

    template<typename T>
    fly::WriteErrorType write_object(const CMString& object_name, const T& obj,
                          const CMString& py_name, bool backup = false);

    // Cache tiers: "low" (default) and "high" populate/query the high-tier
    // cache (省反序列化); "none" bypasses all cache tiers and reads from source.
    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name, const CMString& cache = "low");

    // Returns the py_name (type name) stored in the object header, without
    // reading/deserializing the payload. Goes through the low-tier cache, so a
    // hit is cheap. Used by Python to dispatch read_object to the right tier.
    CMString read_object_py_name(const CMString& object_name);

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

    void mark_temp(const CMString& object_name);
    void put_temp_data(const CMString& object_name, FlyBufferPtr compressed_data);

    DbMeta load_meta() const;
    // 静态读 _DB_META，不构造 Database 实例（不触发 DataService register_database，
    // 避免 merge_db 在已 open_db 的进程内重复注册同 db_path）。
    static DbMeta load_meta_from_path(const CMString& db_path);

    CMString get_db_path() const;
    CMString get_data_path() const;
    // Merge-only：跨机数据集中后产物落在新路径，master 用它更新既有 Database 的路径，
    // 同步 re-register 进 DataService非 merge 场景禁止调用。
    void set_paths(const CMString& db_path, const CMString& data_path);
    CMString get_full_name(const CMString& name) const;
    CMString get_writer_id() const;

    void reset();

    void write_db_meta_header();
    void append_worker_info_to_meta(const WorkerInfo& info);

    // 读取已登记的 worker 数量（用于验证 record_worker_info 是否生效）。
    size_t worker_info_count() const;

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
    // Shared by write_pickle_bytes and write_object<T>. On registration failure,
    // rolls back cache and local_idx, does not enqueue disk write.
    fly::WriteErrorType commit_write(const CMString& object_name,
                                     const CMString& full,
                                     FlyBufferPtr record,
                                     int64_t original_size,
                                     int32_t chunk_count,
                                     bool backup);

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
    CMUniquePtr<DataReader> reader_;
    CMUnorderedSet<CMString> removed_objects_;
    CMUnorderedSet<CMString> temp_objects_;

    // Var store: var_name → VarEntry. Mutex guards all access (set/get/remove
    // may come from task-execution threads; freeze snapshots under the lock).
    CMUnorderedMap<CMString, VarEntry> var_store_;
    mutable std::mutex var_mutex_;
};

template<typename T>
fly::WriteErrorType Database::write_object(const CMString& object_name, const T& obj,
                                const CMString& py_name, bool backup) {
    CMString full = full_name(object_name);
    if (check_frozen()) {
        fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB);
        return fly::WriteErrorType::FROZEN_DB;
    }

    // Serialize + compress via streaming pipeline.
    auto record = CMMakeShared<FlyBuffer>();
    FlyBufferStreamBuf fly_buf(*record);
    CountingStreamBuf counting_buf(fly_buf);
    std::ostream counting_stream(&counting_buf);

    ObjectHeader header;
    header.total_size_ = 0;
    header.chunk_count_ = 0;
    header.compression_type_ = static_cast<uint8_t>(compression_type_);
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    CMString header_bytes = header.serialize();
    counting_stream.write(header_bytes.data(), static_cast<std::streamsize>(header_bytes.size()));

    int64_t total_uncompressed = 0;
    int32_t chunk_count = 0;
    {
        auto compressor = compression_type_ != CompressionType::NONE
            ? CompressorFactory::create(compression_type_) : nullptr;
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
    }
    counting_stream.flush();

    header.total_size_ = static_cast<uint64_t>(total_uncompressed);
    header.chunk_count_ = static_cast<uint32_t>(chunk_count);
    CMString real_header = header.serialize();
    std::memcpy(record->data(), real_header.data(), real_header.size());

    // Commit: cache → register → enqueue disk write (shared logic).
    return commit_write(object_name, full, record, total_uncompressed, chunk_count, backup);
}

template<typename T>
CMSharedPtr<T> Database::read_object(const CMString& object_name, const CMString& cache) {
    CMString full = full_name(object_name);
    auto& cache_instance = fly::ObjectCache::instance();

    // cache="none": bypass all cache tiers, read directly from source.
    if (cache == "none") {
        auto [comp_data, py_name] = read_object_compressed(object_name, false, true);
        if (!comp_data || comp_data->empty()) {
            ERR("read_object<T>: no data for '{}'", full);
            return nullptr;
        }
        auto obj = CMMakeShared<T>();
        DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
        std::istream is(&dsbuf);
        obj->fly_deserialize(is);
        return obj;
    }

    // cache="low" (default) or cache="high": query high tier first.
    // Hit → return cached instance, skip IO + deserialize.
    if (auto cached = cache_instance.get_high<T>(full)) {
        return cached;
    }

    // Miss → read compressed data (low-tier cache transparent in read_object_compressed).
    auto [comp_data, py_name] = read_object_compressed(object_name, false, false);
    if (!comp_data || comp_data->empty()) {
        ERR("read_object<T>: no data for '{}'", full);
        return nullptr;
    }

    auto obj = CMMakeShared<T>();
    DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
    std::istream is(&dsbuf);
    obj->fly_deserialize(is);

    // Populate high tier so subsequent reads skip deserialization.
    size_t accounted = comp_data->size();
    try {
        int64_t off = 0;
        auto hdr = ObjectHeader::deserialize({comp_data->data(), comp_data->size()}, off);
        if (hdr.total_size_ > 0) accounted = static_cast<size_t>(hdr.total_size_);
    } catch (...) {}
    cache_instance.put_high<T>(full, obj, accounted);

    return obj;
}