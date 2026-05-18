#pragma once

#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/db_meta.h>
#include <agent/cpp/worker_context.h>
#include <common/cpp/common_types.h>
#include <memory>
#include <stdexcept>

class Database {
public:
    Database(const CMString& base_path, const CMString& data_path = "", uint64_t writer_id = 0);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                           const CMString& py_name = "") {
        CMString full = full_name(object_name);
        check_frozen();
        fly::DataService::instance().on_write_started(db_id_, full);
        try {
            fly::WorkerAgentContext::register_write(db_id_, object_name);
        } catch (const std::exception& e) {
            fly::DataService::instance().on_write_failed(db_id_, full, e.what());
            throw;
        }
        CMString result = writer_->write_object(full, obj, py_name);
        auto* all = writer_->get_all_entries(full);
        if (all) {
            fly::DataService::instance().on_write_completed(db_id_, full, *all);
        }
        writer_->flush();
        fly::DataService::instance().on_flush(db_id_);
        fly::WorkerAgentContext::record_write(db_id_, object_name);
        return result;
    }

    template<typename T>
    std::shared_ptr<T> read_object(const CMString& object_name) {
        CMString full = full_name(object_name);
        auto result = fly::DataService::instance().read_raw(full);
        auto obj = std::make_shared<T>();
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
    bool is_frozen_ = false;

    std::unique_ptr<DataWriter> writer_;
    std::unique_ptr<DataReader> reader_;
};