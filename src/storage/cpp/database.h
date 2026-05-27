#pragma once

#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/write_back_queue.h>
#include <storage/cpp/db_meta.h>
#include <common/cpp/worker_context.h>
#include <common/cpp/common_types.h>
#include <log/cpp/logger.h>
#include <memory>
#include <stdexcept>

#include <set>

class Database {
public:
    Database(const CMString& base_path, const CMString& data_path = "", uint64_t writer_id = 0, const CMString& host = "", const CMString& existing_db_id = "");
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                           const CMString& py_name = "") {
        CMString full = full_name(object_name);
        if (check_frozen()) {
            fly::WorkerAgentContext::set_last_error_type(fly::TaskErrorType::WRITE_TO_FROZEN_DB);
            return {};
        }

        FlySerBuf serialized;
        FLY_ENCODE_TO_BYTES(obj, serialized);

        fly::DataService::instance().on_write_started(db_id_, full);

        auto [reg_error, reg_error_type] = fly::WorkerAgentContext::register_write(db_id_, object_name);
        if (reg_error_type != fly::TaskErrorType::UNKNOWN) {
            fly::DataService::instance().on_write_failed(db_id_, full, reg_error);
            ERR("Write registration rejected: {} (type={})", reg_error, static_cast<int>(reg_error_type));
            return {};
        }

        auto record = CMMakeShared<FlyBuffer>();
        auto compress_result = writer_->compress_to_buffer(
            static_cast<uint64_t>(serialized.size()), py_name,
            serialized.data(), static_cast<int64_t>(serialized.size()),
            *record);

        DataWriter* w = writer_.get();
        auto caller_record_func = fly::WorkerAgentContext::current_record_func();
        auto caller_record_ctx = fly::WorkerAgentContext::current_record_ctx();

        auto execute = [w, name = full, compress_result, record]() {
            w->write_record(name, compress_result.original_size,
                            compress_result.chunk_count, *record);
            w->flush();
        };

        auto complete = [full, db_id = this->db_id_, object_name,
                         caller_record_func, caller_record_ctx, w]() {
            auto& ds = fly::DataService::instance();
            auto* entries = w->get_all_entries(full);
            if (entries) {
                ds.on_write_completed(db_id, full, *entries);
            }
            ds.on_object_flushed(full);
            if (caller_record_func) {
                caller_record_func(caller_record_ctx, db_id, object_name);
            }
        };

        fly::WriteRequest req;
        req.execute = std::move(execute);
        req.on_complete = std::move(complete);
        fly::DataService::instance().enqueue_write_back(std::move(req));

        return "";
    }

    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name) {
        CMString full = full_name(object_name);
        auto result = fly::DataService::instance().read_raw(full);
        if (result.data_buffer.empty()) {
            ERR("read_object<T>: empty data for '{}'", full);
            return nullptr;
        }
        auto obj = CMMakeShared<T>();
        FLY_DECODE_FROM_BYTES(result.data_buffer, T, *obj);
        return obj;
    }

    CMString write_object(const CMString& object_name, const CMString& data, bool backup = false);

    CMString read_object(const CMString& object_name);

    CMString write_object_typed(const CMString& object_name, const CMString& data,
                                 const CMString& py_name);

    CMString write_object_buffer(const CMString& object_name,
                                 CMSharedPtr<FlyBuffer> buffer,
                                 const CMString& py_name);

    CMString write_object_raw_ptr(const CMString& object_name,
                                  const char* data, int64_t data_size,
                                  const CMString& py_name);

    ReadResult read_object_typed(const CMString& object_name);

    void freeze();
    bool is_frozen() const;

    void remove_object(const CMString& object_name);
    void remove_index_entry(const CMString& object_name);

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

    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString db_id_;
    CMString host_;
    bool is_frozen_ = false;

    CMUniquePtr<DataWriter> writer_;
    CMUniquePtr<DataReader> reader_;
    CMSet<CMString> removed_objects_;
};