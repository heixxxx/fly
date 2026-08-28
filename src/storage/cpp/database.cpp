#include <storage/cpp/database.h>
#include <storage/cpp/compressor.h>
#include <serialization/cpp/object_header.h>
#include <serialization/cpp/serialization_macros.h>
#include <storage/cpp/object_cache.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <common/cpp/writer_id.h>
#include <common/cpp/worker_context.h>
#include <common/cpp/write_context_hash.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <random>
#include <istream>
#include <cassert>

namespace fs = std::filesystem;

Database::Database(const CMString& db_path, const CMString& data_path, uint64_t worker_id, const CMString& host, const CMString& existing_db_path)
    : db_path_(db_path)
    , data_path_(data_path)
    , writer_id_(generate_writer_id())
    , host_(host) {
    (void)worker_id;  // worker_id kept for API compat; writer_id_ is used for file naming
    (void)existing_db_path;  // 废弃参数（值被忽略）

    // 校验 db_path 不含 ':' —— full_name = "db_path:short" 用 ':' 分隔，
    // db_path 含 ':' 会导致 split 歧义。源头拒绝，双保险。
    if (db_path_.find(':') != CMString::npos) {
        ERR("Database db_path must not contain ':' (would break full_name split): '{}'", db_path_);
        db_path_.clear();
        return;
    }

    // 迁移跟随：_MIGRATED_TO 机制已废弃（db chain 取代）。
    // resolve_migrated_path 现在对新 db 总是返回原始 path（_MIGRATED_TO 不再写入）。
    // 旧 db 如有 _MIGRATED_TO 残留仍可跟随（兼容），但新 merge 不再产生此文件。
    CMString resolved = fly::DataService::instance()->resolve_migrated_path(db_path_);
    if (resolved != db_path_) {
        CMString target_data = fly::DataService::instance()->read_migrated_data_path(db_path_);
        INFO("Database migrated: db_path '{}' -> '{}', data_path '{}' -> '{}'",
             db_path_, resolved, data_path_, target_data);
        db_path_ = resolved;
        if (!target_data.empty()) {
            data_path_ = target_data;
        }
    }

    fly::DataService::instance()->register_database(db_path_, data_path_, writer_id_);

    ensure_directory_exists(db_path_);
    if (!data_path_.empty()) {
        ensure_directory_exists(data_path_);
    }

    // _DB_META（JSON）的初写由 Python 编排层负责（open_db → Database.
    // _init_chain → DbMetaFile.write_new）；C++ 不写元信息文件。

    CMString frozen_marker = db_path_ + "/_FROZEN";
    if (fs::exists(frozen_marker)) {
        is_frozen_ = true;
    }

    // Restore vars from a prior freeze (_VARS file) if present. Applies to both
    // fresh opens and load_db scenarios — absence of the file is a no-op.
    load_vars_from_disk();

    auto config = Config::instance();
    CMString comp_type_str = config->get_str("compression_type");
    compression_type_ = CompressorFactory::type_from_name(comp_type_str);
    compression_level_ = static_cast<int>(config->get_int("compression_level"));
    serialize_chunk_size_ = config->get_int("serialize_chunk_size");
    compression_threshold_ = config->get_int("compression_threshold");

    writer_ = CMMakeUnique<DataWriter>(
        db_path_, data_path_, writer_id_,
        config->get_int("aggregation_threshold"),
        host_
    );
    // temp writer：与正式 writer 同 writer_id（temp idx/数据文件按后缀区分：
    // {wid}.temp.idx / temp_data_{wid}_{NNN}.dat）。同生命周期创建——事务段
    // 必须在首写之前打标（mark_write_begin 双打），惰性创建会漏段导致
    // task 失败回滚不覆盖 temp 写。frozen db 跳过：写入已被 check_frozen
    // 拦截（事务段永不开），建了只会留下空 temp 文件残留（load_db 场景）。
    if (!is_frozen_) {
        temp_writer_ = CMMakeUnique<DataWriter>(
            db_path_, data_path_, writer_id_,
            config->get_int("aggregation_threshold"),
            host_,
            /*temp_mode=*/true
        );
    }
}

Database::~Database() {
    fly::DataService::instance()->drain_write_back();
    fly::DataService::instance()->cleanup_temp_entries(db_path_);
    fly::DataService::instance()->unregister_database(db_path_);
}

Database::CompressResult Database::compress_buffered_data(
    const char* data, int64_t data_size,
    const CMString& py_name, FlyBuffer& target) {

    // trailer 格式（§4.4）：块流纯追加，完成后追加 trailer——占位+memcpy
    // 回填消失（total_size/chunk_count 写完末块自然已知）。
    FlyBufferStreamBuf fly_buf(target);
    CountingStreamBuf counting_buf(fly_buf);
    std::ostream counting_stream(&counting_buf);

    int64_t total_uncompressed = 0;
    int32_t chunk_count = 0;
    uint8_t effective_comp = static_cast<uint8_t>(compression_type_);
    {
        auto compressor = compression_type_ != CompressionType::NONE
            ? CompressorFactory::create(compression_type_, compression_level_) : nullptr;
        CompressingStreamBuf csbuf(counting_stream, std::move(compressor),
                                    serialize_chunk_size_, compression_threshold_);
        std::ostream os(&csbuf);
        os.write(data, static_cast<std::streamsize>(data_size));
        os.flush();
        total_uncompressed = csbuf.total_uncompressed();
        chunk_count = csbuf.chunk_count();
        // Small payloads skip compression internally; record the actual format
        // so the read-side picks the matching (de)compressor path.
        effective_comp = static_cast<uint8_t>(csbuf.effective_compression_type());
    }
    counting_stream.flush();

    ObjectHeader header;
    header.compression_type_ = effective_comp;
    header.total_size_ = static_cast<uint64_t>(total_uncompressed);
    header.chunk_count_ = static_cast<uint32_t>(chunk_count);
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    CMString trailer = header.serialize_trailer();
    target.write(trailer.data(), trailer.size());

    return {total_uncompressed, chunk_count};
}

// Shared commit logic: cache → register → enqueue disk write.
fly::WriteErrorType Database::commit_write(const CMString& object_name,
                                           const CMString& full,
                                           FlyBufferPtr record,
                                           int64_t original_size,
                                           int32_t chunk_count,
                                           bool backup,
                                           bool populate_cache) {
    // 裸写入（非 @as_task，无 task context）的 current_write_hash 为空。用时间戳填充，
    // 使 provenance 校验对裸写入也生效（消灭 do_write_register 的空 hash 旁路），
    // 并保证 register_write（内部 get_current_write_hash）与 write_record 落盘同一个值。
    CMString write_hash = fly::WorkerAgentContext::get_current_write_hash();
    bool self_set_hash = write_hash.empty();
    if (self_set_hash) {
        write_hash = make_timestamp_hash();
        fly::WorkerAgentContext::set_current_write_hash(write_hash);
    }
    // RAII：仅当本函数 set 了 hash 时，任意 return 路径退出前 clear（恢复空状态）。
    struct WriteHashGuard {
        bool self;
        ~WriteHashGuard() { if (self) fly::WorkerAgentContext::clear_current_write_hash(); }
    } hash_guard{self_set_hash};

    // 1. Populate low-tier cache immediately — remote reads can serve from
    //    cache without waiting for the background disk write.
    //    populate_cache=false（保存等级"none"，仅落盘）：跳过缓存填充——
    //    数据搬运/merge 等场景不希望中间对象挤占缓存，读直接走索引+磁盘。
    if (populate_cache) {
        size_t sz = original_size > 0 ? static_cast<size_t>(original_size) : record->size();
        fly::ObjectCache::instance().put_low(full, record, sz);
    }

    // 2. Register write with master. Only NOW does the master mark data ready
    //    and schedule dependent tasks — by which point the cache is populated
    //    and remote reads can be served immediately.
    fly::DataService::instance()->on_write_started(db_path_, full);
    int64_t compressed_size = static_cast<int64_t>(record->size());
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_path_, object_name, compressed_size);

    if (reg_error_type == fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, "duplicate skipped");
        fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED);
        return fly::WriteErrorType::DUPLICATE_SKIPPED;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_PROVENANCE_MISMATCH) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        ERR("Write registration failed: {} (type={})", reg_error, static_cast<int>(reg_error_type));
        return fly::WriteErrorType::REGISTRATION_FAILED;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_REGISTRATION_FAILED) {
        // 注册被拒的通用兜底（空 hash 到达 master / worker 终止未确认 /
        // master 未运行）——与 mismatch 同款撤缓存 + on_write_failed 处理。
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        ERR("Write registration rejected: {} (type={})", reg_error, static_cast<int>(reg_error_type));
        return fly::WriteErrorType::REGISTRATION_FAILED;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_REGISTRATION_TIMEOUT) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        ERR("Write registration timeout: {}", reg_error);
        return fly::WriteErrorType::REGISTRATION_TIMEOUT;
    }
    if (reg_error_type == fly::TaskErrorType::WRITE_TO_FROZEN_DB) {
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        ERR("Write rejected (db frozen): {} (type={})", reg_error, static_cast<int>(reg_error_type));
        return fly::WriteErrorType::FROZEN_DB;
    }

    // 3. Registration succeeded — enqueue background disk write.
    DataWriter* w = writer_.get();
    auto caller_backup_func = backup ? fly::WorkerAgentContext::current_backup_func() : std::function<void(const fly::CMString&, const fly::CMString&)>{};
    // write_hash 复用入口已取的值（task context hash 或裸写入时间戳），保证 register 与 record 一致。

    // 同步 record_write：register 成功后立即记录写出对象，不放在异步 on_complete_
    // 回调里。否则 task 在 write_object 返回后立即异常时（on_complete_ 尚未执行），
    // current_writes_ 为空 → dirty_objects_ 为空 → master 不清理脏对象的
    // provenance/remote_idx → 重写同 key 时 provenance mismatch。
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    if (caller_record_func) {
        caller_record_func(db_path_, object_name, compressed_size);
    }

    // 仅 worker task 上下文打 BEGIN（transaction_mode 激活时）。master 直接
    // write_object 时 transaction_mode 未激活，其 ADD 为段外隐式事务，load 时
    // 直接生效。mark_begin 记录 data 偏移回滚点，后续 mark_end 提交 /
    // abort_segment 回滚。WBQ 单线程串行，segment_active 判断与设置无竞态。
    bool in_task_context = fly::WorkerAgentContext::is_transaction_mode();
    // LocalIndex 是 per-(db,writer) 的，idx 文件天然属于本 db，entry 只需存 short_name
    // （前缀在同文件内 100% 冗余）。write_record/get_all_entries 传 short_name；
    // DataService 层（on_write_completed/on_object_flushed）仍用 full_name 作 key。
    auto execute = [w, short_name = object_name, original_size, chunk_count, record, write_hash, in_task_context]() -> bool {
        if (in_task_context && !w->segment_active()) {
            w->mark_begin();
        }
        bool ok = w->write_record_checked(short_name, original_size, chunk_count, *record, write_hash);
        ok = ok && w->flush_checked();
        return ok;
    };

    auto complete = [full, db_path = this->db_path_, object_name,
                     caller_backup_func, w, backup, compressed_size]() {
        auto ds = fly::DataService::instance();
        auto entries = w->get_all_entries(object_name);
        if (entries.has_value()) {
            ds->on_write_completed(db_path, full, entries.value());
        }
        ds->on_object_flushed(full);
        if (backup && caller_backup_func) {
            caller_backup_func(db_path, object_name);
        }
    };

    auto on_error = [full, db_path = this->db_path_, object_name]() {
        // 落盘失败：撤销 ObjectCache + local_idx，通知 master 对象不可用。
        fly::ObjectCache::instance().remove(full);
        fly::DataService::instance()->on_write_failed(db_path, full,
            "persistent disk write failure");
        ERR("[WRITE-BACK] object {} persisted-failed: removed from cache, "
            "notified master as unavailable", full);
    };

    fly::WriteRequest req;
    req.execute_ = std::move(execute);
    req.on_complete_ = std::move(complete);
    req.on_error_ = std::move(on_error);
    fly::DataService::instance()->enqueue_write_back(std::move(req));

    return fly::WriteErrorType::OK;
}

void Database::write_temp_pickle(const CMString& object_name,
                                 const char* data, int64_t data_size,
                                 const CMString& py_name) {
    // Compress directly into FlyBufferPtr — zero-copy path, no Python roundtrip.
    auto buf = CMMakeShared<FlyBuffer>();
    compress_buffered_data(data, data_size, py_name, *buf);

    // Register + store as temp (same logic as put_temp_data).
    put_temp_data(object_name, buf);
}

fly::WriteErrorType Database::write_pickle_bytes(const CMString& object_name,
                                         const char* data, int64_t data_size,
                                         const CMString& py_name, bool backup,
                                         bool populate_cache) {
    CMString full = full_name(object_name);
    if (check_frozen()) { fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB); return fly::WriteErrorType::FROZEN_DB; }

    auto record = CMMakeShared<FlyBuffer>();
    auto cr = compress_buffered_data(data, data_size, py_name, *record);

    return commit_write(object_name, full, record, cr.original_size_, cr.chunk_count_,
                        backup, populate_cache);
}

fly::WriteErrorType Database::commit_stream(const CMString& object_name,
                                     FlyBufferPtr record,
                                     const CMString& py_name, bool backup,
                                     bool populate_cache) {
    CMString full = full_name(object_name);
    if (check_frozen()) { fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB); return fly::WriteErrorType::FROZEN_DB; }

    // The incoming FlyBufferPtr may carry a nanobind py_deleter (if it crossed
    // the Python↔C++ boundary). commit_write stores it in ObjectCache, which is
    // destructed from non-Python threads (e.g. reactor thread's remove_local_index).
    // A py_deleter would try to acquire the GIL on those threads → deadlock if
    // the task executor thread holds the GIL. Create a pure-C++ copy to strip
    // the nanobind deleter.
    auto pure_record = CMMakeShared<FlyBuffer>();
    pure_record->write(record->data(), record->size());

    int64_t original_size = 0;
    int32_t chunk_count = 0;
    {
        ObjectHeader hdr;
        size_t trailer_len = 0;
        if (ObjectHeader::deserialize_trailer({pure_record->data(), pure_record->size()},
                                              hdr, trailer_len)) {
            original_size = static_cast<int64_t>(hdr.total_size_);
            chunk_count = static_cast<int32_t>(hdr.chunk_count_);
        }
    }

    return commit_write(object_name, full, pure_record, original_size, chunk_count,
                        backup, populate_cache);
}

CMString Database::compress_pickle_bytes(const char* data, int64_t data_size,
                                          const CMString& py_name) {
    FlyBuffer buf;
    compress_buffered_data(data, data_size, py_name, buf);
    return CMString(buf.data(), buf.size());
}

std::pair<FlyBufferPtr, CMString> Database::read_object_compressed(const CMString& object_name, bool backup, bool bypass_cache) {
    CMString full = full_name(object_name);
    auto& cache = fly::ObjectCache::instance();

    // Low-tier hit: skip disk/remote IO. Re-parse py_name from the cached trailer.
    // Skipped when bypass_cache=true (cache="none" mode).
    if (!bypass_cache) {
        if (auto [hit, cached] = cache.get_low(full); hit) {
            CMString py_name;
            ObjectHeader hdr;
            size_t trailer_len = 0;
            if (ObjectHeader::deserialize_trailer({cached->data(), cached->size()},
                                                  hdr, trailer_len)) {
                py_name = hdr.py_name_;
            }
            // Malformed cached entry（py_name 空）— fall through to re-read from source.
            if (!py_name.empty()) {
                // backup=True means the caller wants a persisted local replica.
                // The low-tier cache is memory-only and does NOT imply a local
                // on-disk copy, so a cache hit must still honour the backup
                // request when the object is not yet persisted locally.
                // (Without this, a prior _get_py_name() pre-read would populate
                // the cache and silently skip the backup the caller asked for.)
                if (backup) {
                    auto ds = fly::DataService::instance();
                    if (!ds->has_local_object(full)) {
                        do_backup_write(full, object_name,
                                        CMString(cached->data(), cached->size()), {});
                    }
                }
                return {cached, std::move(py_name)};
            }
            // If header parse failed, evict the stale entry and re-read.
            cache.remove(full);
        }
    }

    auto ds = fly::DataService::instance();
    auto [comp_found, comp_data, comp_py_name, comp_hash, comp_can_still_produce] = ds->read_raw_compressed(full);
    if (!comp_found || !comp_data || comp_data->empty()) {
        ERR("read_object_compressed: no data for '{}'", full);
        return {nullptr, {}};
    }

    // ── 零容忍 trailer 校验（chunked-transfer-design §5）──
    // 取回的 record 必须解析出合法 trailer（块 CRC 的验证发生在解压出口）。
    // 失败 = 缓存/本地盘/传输损坏 → 失效缓存 + 一次 bypass 重取（远程副本
    // 优先；无副本即败）→ 仍败 → DataCorruptionError（FATAL，上层转 task 失败）。
    {
        ObjectHeader hdr;
        size_t trailer_len = 0;
        if (!ObjectHeader::deserialize_trailer({comp_data->data(), comp_data->size()},
                                                hdr, trailer_len)) {
            ERR("[FATAL-DATA-CORRUPTION] trailer verify failed, invalidating cache + one bypass re-fetch: obj={}",
                full);
            fly::ObjectCache::instance().remove(full);
            auto [f2, d2, py2, h2, c2] = ds->read_raw_compressed(full, /*bypass_local=*/true);
            if (f2 && d2 && !d2->empty() &&
                ObjectHeader::deserialize_trailer({d2->data(), d2->size()}, hdr, trailer_len)) {
                comp_data = d2;
                comp_py_name = py2;
                comp_hash = h2;
            } else {
                throw fly::DataCorruptionError(
                    "[FATAL-DATA-CORRUPTION] object '" + full +
                    "': trailer verify failed after one re-fetch");
            }
        }
    }

    if (backup && !ds->has_local_object(full)) {
        do_backup_write(full, object_name, CMString(comp_data->data(), comp_data->size()), comp_hash);
    }

    // Populate low tier: account by uncompressed size from the object trailer
    // (fall back to compressed size if the trailer cannot be parsed).
    size_t accounted = comp_data->size();
    {
        ObjectHeader hdr;
        size_t trailer_len = 0;
        if (ObjectHeader::deserialize_trailer({comp_data->data(), comp_data->size()},
                                              hdr, trailer_len) &&
            hdr.total_size_ > 0) {
            accounted = static_cast<size_t>(hdr.total_size_);
        }
        // Keep compressed-size accounting when the trailer cannot be parsed.
    }
    cache.put_low(full, comp_data, accounted);

    return {comp_data, std::move(comp_py_name)};
}

CMString Database::read_object_py_name(const CMString& object_name) {
    // read_object_compressed already goes through the low-tier cache, so this
    // is a cheap header parse on cache hit (no payload read).
    auto [comp_data, py_name] = read_object_compressed(object_name, false);
    return py_name;
}

void Database::do_backup_write(const CMString& full, const CMString& object_name, CMString compressed_data, const CMString& source_hash) {
    auto ds = fly::DataService::instance();
    ds->on_write_started(db_path_, full);

    auto saved_hash = fly::WorkerAgentContext::get_current_write_hash();
    fly::WorkerAgentContext::clear_current_write_hash();

    int64_t backup_compressed_size = static_cast<int64_t>(compressed_data.size());
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_path_, object_name, backup_compressed_size);
    if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
        ds->on_write_failed(db_path_, full, reg_error);
        fly::WorkerAgentContext::set_current_write_hash(saved_hash);
        ERR("do_backup_write: register_write failed for '{}': {}", object_name, reg_error);
        return;
    }

    int64_t h_off = 0;
    ObjectHeader header;
    size_t backup_trailer_len = 0;
    if (!ObjectHeader::deserialize_trailer(compressed_data, header, backup_trailer_len)) {
        // 源数据损坏（trailer 解析失败）：与 register_write 失败同款处理——
        // 撤写登记 + 恢复 write context + 放弃 backup，不落盘坏数据。
        ds->on_write_failed(db_path_, full, "do_backup_write: corrupted source object trailer");
        fly::WorkerAgentContext::set_current_write_hash(saved_hash);
        ERR("do_backup_write: corrupted object trailer for '{}'", object_name);
        return;
    }

    DataWriter* w = writer_.get();
    // 同步 record_write（同 commit_write 的修复理由：避免异步 on_complete_ 竞态）。
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    if (caller_record_func) {
        caller_record_func(db_path_, object_name, backup_compressed_size);
    }
    CMString backup_hash = source_hash;
    auto record = CMMakeShared<FlyBuffer>();
    record->take(std::move(compressed_data));

    auto execute = [w, short_name = object_name, header, record, backup_hash]() -> bool {
        bool ok = w->write_record_checked(short_name, header.total_size_, header.chunk_count_, *record, backup_hash);
        ok = ok && w->flush_checked();
        return ok;
    };

    auto complete = [full, db_path = db_path_, object_name,
                     w, saved_hash]() {
        fly::WorkerAgentContext::set_current_write_hash(saved_hash);
        auto dsvc = fly::DataService::instance();
        auto entries = w->get_all_entries(object_name);
        if (entries.has_value()) {
            dsvc->on_write_completed(db_path, full, entries.value());
        }
        dsvc->on_object_flushed(full);
    };

    auto on_error = [full, db_path = db_path_]() {
        fly::DataService::instance()->on_write_failed(db_path, full,
            "backup persistent disk write failure");
    };

    fly::WriteRequest req;
    req.execute_ = std::move(execute);
    req.on_complete_ = std::move(complete);
    req.on_error_ = std::move(on_error);
    ds->enqueue_write_back(std::move(req));
    ds->drain_write_back();
}

void Database::backup_object(const CMString& object_name) {
    CMString full = full_name(object_name);
    auto ds = fly::DataService::instance();

    // 零容忍（§5）：源数据校验预算耗尽 → 放弃 backup（不落坏数据），
    // ERR 大声暴露；backup 是尽力语义（无错误通道），不抛。
    try {
        auto [found, compressed_data, py_name, source_hash, can_still_produce] = ds->read_raw_compressed(full);
        if (!found || !compressed_data || compressed_data->empty()) {
            ERR("backup_object: no data for '{}'", full);
            return;
        }

        CMString hash_to_use = source_hash.empty() ? fly::WorkerAgentContext::get_current_write_hash() : source_hash;
        do_backup_write(full, object_name, CMString(compressed_data->data(), compressed_data->size()), hash_to_use);
    } catch (const fly::DataCorruptionError& e) {
        ERR("backup_object: {} — abandoning backup of '{}'", e.what(), full);
    }
}

void Database::freeze() {
    {
        // check-and-set 原子化：原实现"读 is_frozen_ → 置位"非原子，并发 freeze
        // 可同时通过检查重复执行副作用（drain/marker/广播双份）。
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (is_frozen_.load(std::memory_order_relaxed)) {
            return;
        }
        is_frozen_.store(true, std::memory_order_relaxed);
    }
    fly::DataService::instance()->drain_write_back();
    create_frozen_marker();
    CMString db_path_copy;
    size_t removed_count = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        db_path_copy = db_path_;
        removed_count = removed_objects_.size();
    }
    fly::DataService::instance()->on_flush(db_path_copy);
    fly::DataService::instance()->cleanup_temp_entries(db_path_copy);
    // temp 落盘产物删除：freeze = 阶段完成，中间态作废（用户裁定语义）。
    // 幂等（master/worker 侧 freeze 均调，文件不存在 no-op）；共享盘 db_path
    // 单点删除，跨主机 worker 的读已无需求（下游只依赖正式对象）。
    cleanup_temp_files();
    fly::WorkerAgentContext::notify_freeze(db_path_copy);

    // Persist non-deleted vars alongside the frozen db.
    flush_vars_to_disk();

    // TODO: freeze 后处理 — 从聚合文件中真正删除 removed_objects_ 的数据
    // 当前聚合文件可能包含多个对象，删除单个对象需要重写整个文件
    // 完整实现需要在数据压缩(compaction)功能中完成
    if (removed_count > 0) {
        INFO("freeze: {} objects marked for removal (disk cleanup pending compaction implementation)",
             removed_count);
    }
}

bool Database::is_frozen() const {
    return is_frozen_;
}

void Database::remove_object(const CMString& object_name) {
    if (check_frozen()) return;

    CMString full;
    CMString db_path_copy;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        full = db_path_ + ":" + object_name;
        db_path_copy = db_path_;
        removed_objects_.insert(full);
        // LocalIndex 只存 short_name（idx 文件天然属于本 db）。
        writer_->remove_entry(object_name);
    }

    fly::WorkerAgentContext::request_remove(db_path_copy, object_name);
    fly::DataService::instance()->remove_local_index(full);
    fly::ObjectCache::instance().remove(full);

    INFO("Object removed: {}", full);
}

void Database::remove_index_entry(const CMString& object_name) {
    CMString full;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        full = db_path_ + ":" + object_name;
        removed_objects_.insert(full);
        writer_->remove_entry(object_name);
    }
    fly::ObjectCache::instance().remove(full);
    INFO("Index entry removed: {}", full);
}

void Database::mark_write_begin() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    writer_->mark_begin();
    // temp writer 同步开段：task 内混合写正式/temp 对象时，两族写入共享同一
    // task 事务边界（END/ABORT 同步收尾）。空段 abort 是 no-op（回滚点=当前
    // 位置，truncate 等长 no-op），未写过 temp 的 task 无额外代价。
    // null = frozen db 不建 temp writer（写入被拦截，段永不开）。
    if (temp_writer_) temp_writer_->mark_begin();
}

void Database::mark_write_end() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    writer_->mark_end();
    if (temp_writer_) temp_writer_->mark_end();
}

void Database::abort_task_writes(const CMVector<CMString>& dirty_full_names) {
    // 1. 先清空 WBQ 中尚未开始的脏写请求（比 drain 更高效，避免先落盘再删）。
    fly::DataService::instance()->clear_write_back();
    // 2. drain 正在执行的那个写（已 pop 出 queue，clear_pending 丢不掉它）：
    //    等它自然完成，保证 truncate 时文件状态确定。它落盘的脏字节会被
    //    下一步 truncate 回收，on_complete_ 更新的 local_idx_ 会在第 4 步清理。
    fly::DataService::instance()->drain_write_back();

    // 3. idx 打 ABORT + data 文件 truncate 回滚点（正式 + temp 双 writer，
    //    temp 写入已纳入 task 写追踪，脏 temp 同段撤销）。
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        writer_->abort_segment();
        if (temp_writer_) temp_writer_->abort_segment();
    }

    // 4. 清运行时内存中被本 task 污染的对象。
    //    （clear_pending 跳过了未开始请求的 on_complete_，drain 完成的那个
    //    请求的 on_complete_ 更新了 local_idx_；ABORT 标记只在 load 时丢弃
    //    pending，运行时内存需主动清。）
    for (const auto& full : dirty_full_names) {
        fly::DataService::instance()->remove_local_index(full);
        fly::ObjectCache::instance().remove(full);
    }

    INFO("Task writes aborted: {} dirty objects, db_path={}", dirty_full_names.size(),
         get_db_path());
}

CMString Database::get_db_path() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return db_path_;
}

CMString Database::get_data_path() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return data_path_;
}

void Database::set_paths(const CMString& db_path, const CMString& data_path) {
    // Merge 产物落到新路径：更新 db_path_/data_path_ + re-register 进 DataService。
    // db_path_ 是 Database 唯一路径标识，merge 后所有句柄（底层共享同一 C++ 对象）
    // 的 db_path_ 同时变为 target，full_name 自然产生 target path 前缀。
    // merge 的 wait_for_all_tasks 前提保证 merge 时无 pending task，不需要旧 path 锚点。
    // 成员修改在 state_mutex_ 内（与并发 get_db_path 等 reader 互斥）；
    // DataService re-register 在锁外（无反向调用本对象，安全）。
    CMString old_db_path;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        old_db_path = db_path_;
        db_path_ = db_path;
        data_path_ = data_path;
    }
    auto ds = fly::DataService::instance();
    // unregister 旧 db_path（merge 前的源 path），再注册新的。
    ds->unregister_database(old_db_path);
    ds->register_database(db_path, data_path, writer_id_);
}

CMString Database::get_writer_id() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return writer_id_;
}

CMString Database::get_full_name(const CMString& name) const {
    return full_name(name);
}

CMString Database::full_name(const CMString& short_name) const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return db_path_ + ":" + short_name;
}

void Database::reset() {
    is_frozen_ = false;
    CMString frozen_marker = db_path_ + "/_FROZEN";
    if (fs::exists(frozen_marker)) {
        fs::remove(frozen_marker);
    }
}

bool Database::check_frozen() {
    if (is_frozen_) {
        ERR("Database is frozen: {}", db_path_);
        return true;
    }
    return false;
}

void Database::create_frozen_marker() {
    CMString frozen_path = db_path_ + "/_FROZEN";
    std::ofstream ofs(frozen_path);
    ofs.close();
}

void Database::ensure_directory_exists(const CMString& path) {
    fs::create_directories(path);
}

void Database::put_temp_data(const CMString& object_name, FlyBufferPtr compressed_data) {
    CMString full = full_name(object_name);
    DBG("[TEMP-PUT] put_temp_data: obj={}, full={}, data_size={}", object_name, full, compressed_data ? compressed_data->size() : 0);

    // ── temp 落盘（task 级断点基建）──────────────────────────────────
    // temp 从纯内存（TempStore LRU）改为"内存 LRU（TIER1 读缓存）+ db 目录专用
    // 文件落盘"。COMPLETED task 的 temp 输出跨进程可恢复（load temp idx →
    // restore_temp_entries → 下游 task 输入 ready）；db freeze 后
    // cleanup_temp_files 删除全部 temp 文件。
    int64_t original_size = 0;
    int32_t chunk_count = 0;
    {
        ObjectHeader hdr;
        size_t trailer_len = 0;
        if (ObjectHeader::deserialize_trailer({compressed_data->data(), compressed_data->size()},
                                              hdr, trailer_len)) {
            original_size = static_cast<int64_t>(hdr.total_size_);
            chunk_count = static_cast<int32_t>(hdr.chunk_count_);
        }
    }
    bool disk_ok = false;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (!temp_writer_) {
            // frozen db 无 temp writer（构造跳过）——frozen 后的 temp 写本就
            // 是异常路径，显式失败而非静默内存降级。
            ERR("[TEMP-PUT] put_temp_data on frozen db (no temp writer): {}", object_name);
            fly::DataService::instance()->on_temp_write_started(db_path_, full);
            fly::DataService::instance()->on_write_failed(db_path_, full, "temp write on frozen db");
            return;
        }
        // write_context_hash 留空：temp 对象按对象名携带迭代步（如
        // __rasg__x_{i}_{step}），restore 保守加载（hash 空不判等价）语义安全。
        disk_ok = temp_writer_->write_record_checked(
            object_name, original_size, chunk_count, *compressed_data, "");
        // write-through：数据流 flush + idx ADD 落盘（LocalIndex::save）。
        // 必须 NOW——idx 的 ADD 是内存 pending、save 才写文件；不 flush 则
        // task END 标记（mark_write_end 即时 append）先于 ADD 落盘，恢复时
        // 段内无记录 → 已完成 task 的 temp 输出丢失（断点语义破坏）。
        if (disk_ok) {
            disk_ok = temp_writer_->flush_checked();
        }
    }
    if (!disk_ok) {
        // 落盘失败即写失败（断点语义要求 COMPLETE 的 temp 输出必在盘上，
        // 不做"仅内存"降级——那会让恢复方静默重算/悬空）。
        ERR("[TEMP-PUT] temp disk write failed for '{}' (db={}) — marking write failed",
            object_name, db_path_);
        fly::DataService::instance()->on_temp_write_started(db_path_, full);
        fly::DataService::instance()->on_write_failed(db_path_, full, "temp disk write failed");
        return;
    }

    // Step 1: Add local idx entry (INCOMPLETE, is_temp=true)
    fly::DataService::instance()->on_temp_write_started(db_path_, full);

    // Step 2: Store temp data and mark COMPLETE — must happen BEFORE register_write.
    // register_write is synchronous (blocks for ACK). Master dispatches dependent
    // tasks immediately on receiving WriteRegister. If data isn't stored yet,
    // other workers' reads will fail. entries（盘读 fallback 用）由 temp_writer
    // 的最新 entry 提供。
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        auto entry_opt = temp_writer_
            ? temp_writer_->get_last_entry(object_name) : std::nullopt;
        fly::DataService::instance()->on_temp_write(db_path_, full, compressed_data, entry_opt);
    }

    // Step 2.5: temp 写入纳入 task 写追踪（current_writes_）：task 失败的
    // ABORT 回滚覆盖 temp；TaskComplete.written_objects 上报完整。
    fly::WorkerAgentContext::record_write(db_path_, object_name,
                                          static_cast<int64_t>(compressed_data->size()));

    // Step 3: Register with master so other workers can discover this data.
    // By now the data is readable on this worker's DataServer.
    int64_t temp_compressed_size = static_cast<int64_t>(compressed_data->size());
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_path_, object_name, temp_compressed_size);
    if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
        ERR("[TEMP-PUT] register_write failed for '{}': {}", object_name, reg_error);
        fly::DataService::instance()->on_write_failed(db_path_, full, reg_error);
        return;
    }

    DBG("[TEMP-PUT] put_temp_data complete: obj={}", object_name);
}

void Database::cleanup_temp_files() {
    // 删除 db 目录下全部 temp 落盘产物。文件名按前缀/后缀精确匹配：
    //   temp_data_{wid}_{NNN}.dat（数据）+ {wid}.temp.idx（索引）。
    // 幂等：不存在 no-op。freeze 确认后调用（master/worker 侧均可）。
    // 先收集再删：directory_iterator 边迭代边删会跳条目。
    CMVector<CMString> to_remove;
    try {
        for (const auto& f : fs::directory_iterator(db_path_)) {
            CMString name = f.path().filename().string();
            bool is_temp_data = name.rfind("temp_data_", 0) == 0 &&
                                name.size() > 4 &&
                                name.compare(name.size() - 4, 4, ".dat") == 0;
            bool is_temp_idx = name.size() > 9 &&
                               name.compare(name.size() - 9, 9, ".temp.idx") == 0;
            if (is_temp_data || is_temp_idx) {
                to_remove.push_back(f.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        WARN("cleanup_temp_files: directory iterate failed for {}: {}",
             db_path_, e.what());
        return;
    }
    std::error_code ec;
    int removed = 0;
    for (const auto& path : to_remove) {
        fs::remove(path, ec);
        if (!ec) ++removed;
    }
    if (removed > 0) {
        INFO("cleanup_temp_files: removed {} temp file(s) for db_path={}",
             removed, db_path_);
    }
}

// =============================================================================
// Var service implementation
// =============================================================================

// _VARS file magic header ("VARS" in little-endian int64).
static constexpr int64_t VARS_FILE_MAGIC = 0x53524156;  // 'V','A','R','S' LE

bool Database::set_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name) {
    // var is db data: freeze makes it immutable too (same gate as write_object).
    if (check_frozen()) {
        ERR("set_var rejected: db {} is frozen", db_path_);
        return false;
    }
    // Size warning: var is for small objects; >1K serialized is too large.
    if (value && value->size() > 1024) {
        WARN("var '{}' serialized size {} > 1K, too large for var (consider write_object)",
             var_name, value->size());
    }

    // var_store_ keys on the short name; the context receives the full name.
    CMString full = full_name(var_name);
    bool ok = fly::WorkerAgentContext::set_var(full, value, type_name);
    if (ok) {
        // Master accepted — cache locally (zero-copy shared FlyBufferPtr).
        std::lock_guard<std::mutex> lk(var_mutex_);
        var_store_[var_name] = VarEntry{value, type_name};
    }
    // On rejection (frozen / immutable) leave the local cache untouched: the
    // existing entry (if any) is the authoritative master value.
    return ok;
}

std::tuple<bool, FlyBufferPtr, CMString> Database::get_var(const CMString& var_name) {
    // Local cache first (zero-copy).
    {
        std::lock_guard<std::mutex> lk(var_mutex_);
        auto it = var_store_.find(var_name);
        if (it != var_store_.end()) {
            return {true, it->second.value_, it->second.type_name_};
        }
    }
    // Miss → ask master (synchronous) via the full name.
    CMString full = full_name(var_name);
    auto [success, value, type_name] = fly::WorkerAgentContext::get_var(full);
    if (success && value) {
        // Cache the fetched value locally (zero-copy shared).
        std::lock_guard<std::mutex> lk(var_mutex_);
        var_store_[var_name] = VarEntry{value, type_name};
    }
    return {success, value, type_name};
}

void Database::remove_var(const CMString& var_name) {
    // Clear local cache immediately; master notified asynchronously via full name.
    {
        std::lock_guard<std::mutex> lk(var_mutex_);
        var_store_.erase(var_name);
    }
    CMString full = full_name(var_name);
    fly::WorkerAgentContext::remove_var(full);
}

void Database::inject_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name) {
    // Worker-side local cache population from TaskAssignMessage var_payloads.
    // Overwrites any existing local entry (master authoritative).
    std::lock_guard<std::mutex> lk(var_mutex_);
    var_store_[var_name] = VarEntry{value, type_name};
}

void Database::drop_local_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    var_store_.erase(var_name);
}

// ---- Master authoritative operations (no network) ----

bool Database::master_set_var(const CMString& var_name, FlyBufferPtr value, const CMString& type_name) {
    if (check_frozen()) {
        ERR("master_set_var rejected: db {} is frozen", db_path_);
        return false;
    }
    std::lock_guard<std::mutex> lk(var_mutex_);
    if (var_store_.count(var_name)) {
        // Immutable: a var may only be written once.
        ERR("master_set_var rejected: var '{}' already exists (immutable)", var_name);
        return false;
    }
    var_store_[var_name] = VarEntry{value, type_name};
    return true;
}

std::tuple<bool, FlyBufferPtr, CMString> Database::master_get_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    auto it = var_store_.find(var_name);
    if (it == var_store_.end()) {
        return {false, nullptr, ""};
    }
    return {true, it->second.value_, it->second.type_name_};
}

void Database::master_remove_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    if (var_name.empty()) {
        var_store_.clear();
    } else {
        var_store_.erase(var_name);
    }
}

bool Database::master_has_var(const CMString& var_name) {
    std::lock_guard<std::mutex> lk(var_mutex_);
    return var_store_.count(var_name) > 0;
}

// ---- Persistence (_VARS file) ----

void Database::flush_vars_to_disk() {
    // Snapshot shared FlyBufferPtrs under lock (zero-copy — shared_ptr refcount,
    // no byte copy), write outside lock to avoid blocking readers.
    CMVector<std::tuple<CMString, FlyBufferPtr, CMString>> snapshot;  // (name, buf, type_name)
    {
        std::lock_guard<std::mutex> lk(var_mutex_);
        snapshot.reserve(var_store_.size());
        for (const auto& [name, entry] : var_store_) {
            snapshot.emplace_back(name, entry.value_, entry.type_name_);
        }
    }

    CMString vars_path = db_path_ + "/_VARS";
    std::ofstream ofs(vars_path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        ERR("flush_vars_to_disk: cannot open {}", vars_path);
        return;
    }

    int64_t magic = VARS_FILE_MAGIC;
    int64_t count = static_cast<int64_t>(snapshot.size());
    ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [name, buf, type_name] : snapshot) {
        int64_t name_len = static_cast<int64_t>(name.size());
        ofs.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        ofs.write(name.data(), name_len);

        // Read directly from the shared FlyBuffer — no copy.
        int64_t val_len = buf ? static_cast<int64_t>(buf->size()) : 0;
        ofs.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        if (val_len > 0) {
            ofs.write(buf->data(), val_len);
        }

        int64_t tn_len = static_cast<int64_t>(type_name.size());
        ofs.write(reinterpret_cast<const char*>(&tn_len), sizeof(tn_len));
        ofs.write(type_name.data(), tn_len);
    }
    ofs.flush();
    DBG("flush_vars_to_disk: wrote {} vars to {}", count, vars_path);
}

void Database::load_vars_from_disk() {
    CMString vars_path = db_path_ + "/_VARS";
    if (!fs::exists(vars_path)) {
        return;  // No _VARS file — nothing to load.
    }

    std::ifstream ifs(vars_path, std::ios::binary);
    if (!ifs.is_open()) {
        ERR("load_vars_from_disk: cannot open {}", vars_path);
        return;
    }

    int64_t magic = 0;
    ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!ifs || magic != VARS_FILE_MAGIC) {
        ERR("load_vars_from_disk: bad magic in {}", vars_path);
        return;
    }

    int64_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!ifs || count < 0) {
        ERR("load_vars_from_disk: bad count in {}", vars_path);
        return;
    }

    std::lock_guard<std::mutex> lk(var_mutex_);
    for (int64_t i = 0; i < count; ++i) {
        int64_t name_len = 0;
        ifs.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
        if (!ifs || name_len < 0) break;
        CMString name(static_cast<size_t>(name_len), '\0');
        ifs.read(name.data(), name_len);

        int64_t val_len = 0;
        ifs.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        if (!ifs || val_len < 0) break;
        CMString value_bytes(static_cast<size_t>(val_len), '\0');
        ifs.read(value_bytes.data(), val_len);

        int64_t tn_len = 0;
        ifs.read(reinterpret_cast<char*>(&tn_len), sizeof(tn_len));
        if (!ifs || tn_len < 0) break;
        CMString type_name(static_cast<size_t>(tn_len), '\0');
        ifs.read(type_name.data(), tn_len);

        // Zero-copy into a fresh FlyBufferPtr (take moves the CMString).
        auto buf = CMMakeShared<FlyBuffer>();
        buf->take(std::move(value_bytes));
        var_store_[std::move(name)] = VarEntry{buf, std::move(type_name)};
    }
    DBG("load_vars_from_disk: loaded {} vars from {}", count, vars_path);
}


