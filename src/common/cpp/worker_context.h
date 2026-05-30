#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <stdexcept>
#include <utility>
#include <functional>

namespace fly {

class WorkerAgentContext {
public:
    static void set_record_write_func(std::function<void(const CMString& db_id, const CMString& name)> func) {
        record_write_func_ = std::move(func);
    }

    static void set_notify_removed_func(std::function<void(const CMString& db_id, const CMString& name)> func) {
        notify_removed_func_ = std::move(func);
    }

    static void set_freeze_func(std::function<void(const CMString& db_id)> func) {
        freeze_func_ = std::move(func);
    }

    static void set_remove_request_func(std::function<void(const CMString& db_id, const CMString& object_name)> func) {
        remove_request_func_ = std::move(func);
    }

    static void clear() {
        record_write_func_ = nullptr;
        register_func_ = nullptr;
        notify_removed_func_ = nullptr;
        freeze_func_ = nullptr;
        remove_request_func_ = nullptr;
        backup_request_func_ = nullptr;
        last_error_type_ = TaskErrorType::UNKNOWN;
    }

    static void record_write(const CMString& db_id, const CMString& object_name) {
        if (record_write_func_) {
            record_write_func_(db_id, object_name);
        }
    }

    static void set_register_func(std::function<std::pair<CMString, TaskErrorType>(const CMString&, const CMString&)> func) {
        register_func_ = std::move(func);
    }

    static std::pair<CMString, TaskErrorType> register_write(const CMString& db_id, const CMString& object_name) {
        if (register_func_) {
            return register_func_(db_id, object_name);
        }
        return {"", TaskErrorType::UNKNOWN};
    }

    static bool is_active() {
        return record_write_func_ != nullptr;
    }

    static void notify_object_removed(const CMString& db_id, const CMString& object_name) {
        if (notify_removed_func_) {
            notify_removed_func_(db_id, object_name);
        }
    }

    static void notify_freeze(const CMString& db_id) {
        if (freeze_func_) {
            freeze_func_(db_id);
        }
    }

    static void request_remove(const CMString& db_id, const CMString& object_name) {
        if (remove_request_func_) {
            remove_request_func_(db_id, object_name);
        }
    }

    static void set_backup_request_func(std::function<void(const CMString& db_id, const CMString& object_name)> func) {
        backup_request_func_ = std::move(func);
    }

    static void request_backup(const CMString& db_id, const CMString& object_name) {
        if (backup_request_func_) {
            backup_request_func_(db_id, object_name);
        }
    }

    static std::function<void(const CMString&, const CMString&)>& current_backup_func() { return backup_request_func_; }

    static void set_last_error_type(TaskErrorType type) {
        last_error_type_ = type;
    }

    static TaskErrorType get_last_error_type() {
        return last_error_type_;
    }

    static std::function<void(const CMString&, const CMString&)>& current_record_func() { return record_write_func_; }

private:
    static inline thread_local std::function<void(const CMString&, const CMString&)> record_write_func_;
    static inline thread_local std::function<std::pair<CMString, TaskErrorType>(const CMString&, const CMString&)> register_func_;
    static inline thread_local std::function<void(const CMString&, const CMString&)> notify_removed_func_;
    static inline thread_local std::function<void(const CMString&)> freeze_func_;
    static inline thread_local std::function<void(const CMString&, const CMString&)> remove_request_func_;
    static inline thread_local std::function<void(const CMString&, const CMString&)> backup_request_func_;
    static inline thread_local TaskErrorType last_error_type_ = TaskErrorType::UNKNOWN;
};

}  // namespace fly
