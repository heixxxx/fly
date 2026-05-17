#pragma once

#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
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
        check_frozen();
        CMString result = writer_->write_object(object_name, obj, py_name);
        fly::WorkerAgentContext::record_write(db_id_, object_name);
        return result;
    }

    template<typename T>
    std::shared_ptr<T> read_object(const CMString& object_name) {
        if (!is_frozen_) {
            writer_->flush();
            reader_ = std::make_unique<DataReader>(base_path_, data_path_, writer_id_);
        }
        return reader_->read_object<T>(object_name);
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
    CMString get_base_path() const;
    CMString get_data_path() const;
    CMString get_obj_name(const CMString& name) const;

    void reset();

private:
    void check_frozen();
    void create_frozen_marker();
    CMString generate_db_id();
    void ensure_directory_exists(const CMString& path);
    ReadResult find_and_read_typed(const CMString& object_name);
    CMString find_and_read(const CMString& object_name);

    CMString base_path_;
    CMString data_path_;
    CMString db_id_;
    uint64_t writer_id_;
    bool is_frozen_ = false;

    std::unique_ptr<DataWriter> writer_;
    std::unique_ptr<DataReader> reader_;
};