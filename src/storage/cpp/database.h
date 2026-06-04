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
    Database(const CMString& base_path, const CMString& data_path = "", uint64_t writer_id = 0, const CMString& host = "", const CMString& existing_db_id = "");
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    CMString write_pickle_bytes(const CMString& object_name,
                                const char* data, int64_t data_size,
                                const CMString& py_name, bool backup = false);

    CMString compress_pickle_bytes(const char* data, int64_t data_size,
                                   const CMString& py_name);

    std::pair<CMString, CMString> read_object_compressed(const CMString& object_name, bool backup = false);

    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                          const CMString& py_name, bool backup = false);

    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name);

    void backup_object(const CMString& object_name);

    void freeze();
    bool is_frozen() const;

    void remove_object(const CMString& object_name);
    void remove_index_entry(const CMString& object_name);

    void put_temp(const CMString& object_name, const CMString& compressed_data);
    std::pair<bool, CMString> get_temp(const CMString& object_name);
    bool has_temp(const CMString& object_name);
    void remove_temp(const CMString& object_name);
    void mark_temp(const CMString& object_name);

    DbMeta load_meta() const;

    CMString get_db_id() const;
    void set_db_id(const CMString& db_id);
    CMString get_base_path() const;
    CMString get_data_path() const;
    CMString get_obj_name(const CMString& name) const;
    CMString get_writer_id() const;

    void reset();

    void write_db_meta_header();
    void append_worker_info_to_meta(const WorkerInfo& info);

private:
    bool check_frozen();
    void create_frozen_marker();
    CMString generate_db_id();
    void ensure_directory_exists(const CMString& path);
    CMString full_name(const CMString& short_name) const;

    struct CompressResult {
        int64_t original_size;
        int32_t chunk_count;
    };
    CompressResult compress_buffered_data(const char* data, int64_t data_size,
                                           const CMString& py_name, FlyBuffer& target);

    // Shared backup write logic: takes ownership of compressed_data and writes it
    // as a backup record for the given object.
    void do_backup_write(const CMString& full, const CMString& object_name, CMString compressed_data, const CMString& source_hash = {});

    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString db_id_;
    CMString host_;
    std::atomic<bool> is_frozen_{false};

    CompressionType compression_type_ = CompressionType::NONE;
    int compression_level_ = 0;
    int64_t serialize_chunk_size_ = 4194304;

    CMUniquePtr<DataWriter> writer_;
    CMUniquePtr<DataReader> reader_;
    CMUnorderedSet<CMString> removed_objects_;
    CMUnorderedSet<CMString> temp_objects_;
    CMUniquePtr<fly::TempStore> temp_store_;
};

template<typename T>
CMString Database::write_object(const CMString& object_name, const T& obj,
                                const CMString& py_name, bool backup) {
    CMString full = full_name(object_name);
    if (check_frozen()) {
        fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB);
        return {};
    }

    fly::DataService::instance().on_write_started(db_id_, full);
    auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_id_, object_name);
    if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
        fly::DataService::instance().on_write_failed(db_id_, full, reg_error);
        ERR("Write registration rejected: {} (type={})", reg_error, static_cast<int>(reg_error_type));
        return {};
    }

    auto record = CMMakeShared<FlyBuffer>();
    FlyBufferStreamBuf fly_buf(*record);
    CountingStreamBuf counting_buf(fly_buf);
    std::ostream counting_stream(&counting_buf);

    ObjectHeader header;
    header.total_size = 0;
    header.chunk_count = 0;
    header.compression_type = static_cast<uint8_t>(compression_type_);
    header.py_name = py_name;
    header.py_name_len = static_cast<uint16_t>(py_name.size());
    CMString header_bytes = header.serialize();
    counting_stream.write(header_bytes.data(), static_cast<std::streamsize>(header_bytes.size()));

    int64_t total_uncompressed = 0;
    int32_t chunk_count = 0;
    {
        auto compressor = compression_type_ != CompressionType::NONE
            ? CompressorFactory::create(compression_type_) : nullptr;
        CompressingStreamBuf csbuf(counting_stream, std::move(compressor),
                                    serialize_chunk_size_);
        std::ostream os(&csbuf);
        obj.fly_serialize(os);
        os.flush();
        total_uncompressed = csbuf.total_uncompressed();
        chunk_count = csbuf.chunk_count();
    }
    counting_stream.flush();

    header.total_size = static_cast<uint64_t>(total_uncompressed);
    header.chunk_count = static_cast<uint32_t>(chunk_count);
    CMString real_header = header.serialize();
    std::memcpy(record->data(), real_header.data(), real_header.size());

    DataWriter* w = writer_.get();
    auto caller_record_func = fly::WorkerAgentContext::current_record_func();
    auto caller_backup_func = backup ? fly::WorkerAgentContext::current_backup_func() : std::function<void(const fly::CMString&, const fly::CMString&)>{};
    CMString write_hash = fly::WorkerAgentContext::get_current_write_hash();

    auto execute = [w, name = full, total_uncompressed, chunk_count, record, write_hash]() {
        w->write_record(name, total_uncompressed, chunk_count, *record, write_hash);
        w->flush();
    };

    auto complete = [full, db_id = this->db_id_, object_name,
                     caller_record_func,
                     caller_backup_func, w, backup]() {
        auto& ds = fly::DataService::instance();
        auto entries = w->get_all_entries(full);
        if (entries.has_value()) {
            ds.on_write_completed(db_id, full, entries.value());
        }
        ds.on_object_flushed(full);
        if (caller_record_func) {
            caller_record_func(db_id, object_name);
        }
        if (backup && caller_backup_func) {
            caller_backup_func(db_id, object_name);
        }
    };

    fly::WriteRequest req;
    req.execute = std::move(execute);
    req.on_complete = std::move(complete);
    fly::DataService::instance().enqueue_write_back(std::move(req));

    return "";
}

template<typename T>
CMSharedPtr<T> Database::read_object(const CMString& object_name) {
    auto [comp_data, py_name] = read_object_compressed(object_name, false);
    if (comp_data.empty()) {
        ERR("read_object<T>: no data for '{}'", full_name(object_name));
        return nullptr;
    }

    auto obj = CMMakeShared<T>();
    DecompressingStreamBuf dsbuf(comp_data.data(), comp_data.size());
    std::istream is(&dsbuf);
    obj->fly_deserialize(is);
    return obj;
}