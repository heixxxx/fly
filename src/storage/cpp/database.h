#pragma once

#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/write_back_queue.h>
#include <storage/cpp/db_meta.h>
#include <common/cpp/worker_context.h>
#include <common/cpp/common_types.h>
#include <memory>
#include <stdexcept>

class Database {
public:
    Database(const CMString& base_path, const CMString& data_path = "", uint64_t writer_id = 0, const CMString& host = "");
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                           const CMString& py_name = "") {
        CMString full = full_name(object_name);
        check_frozen();

        FlyBuffer buffer;
        FLY_ENCODE_TO_BYTES(obj, buffer);

        fly::DataService::instance().on_write_started(db_id_, full);

        try {
            fly::WorkerAgentContext::register_write(db_id_, object_name);
        } catch (const std::exception& e) {
            fly::DataService::instance().on_write_failed(db_id_, full, e.what());
            throw;
        }

        auto data_ptr = CMMakeShared<FlyBuffer>(std::move(buffer));
        auto original_size = static_cast<uint64_t>(data_ptr->size());

        DataWriter* w = writer_.get();
        auto caller_record_func = fly::WorkerAgentContext::current_record_func();
        auto caller_record_ctx = fly::WorkerAgentContext::current_record_ctx();

        auto execute = [w, name = full, original_size, py = py_name, data_ptr]() {
            w->write_typed_object(name, original_size, py,
                reinterpret_cast<const char*>(data_ptr->data()),
                static_cast<int64_t>(data_ptr->size()));
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
        auto obj = CMMakeShared<T>();
        FLY_DECODE_FROM_BYTES(result.data_buffer, T, *obj);
        return obj;
    }

    CMString write_object(const CMString& object_name, const CMString& data, bool backup = false);

    CMString read_object(const CMString& object_name);

    CMString write_object_typed(const CMString& object_name, const CMString& data,
                                 const CMString& py_name);
    ReadResult read_object_typed(const CMString& object_name);

    void freeze();
    bool is_frozen() const;

    DbMeta load_meta() const;

    CMString get_db_id() const;
    void set_db_id(const CMString& db_id);
    CMString get_base_path() const;
    CMString get_data_path() const;
    CMString get_obj_name(const CMString& name) const;

    void reset();

private:
    void check_frozen();
    void create_frozen_marker();
    CMString generate_db_id();
    void ensure_directory_exists(const CMString& path);
    CMString full_name(const CMString& short_name) const;

    CMString base_path_;
    CMString data_path_;
    uint64_t writer_id_ = 0;
    CMString db_id_;
    CMString host_;
    bool is_frozen_ = false;

    CMUniquePtr<DataWriter> writer_;
    CMUniquePtr<DataReader> reader_;
};