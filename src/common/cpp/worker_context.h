#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <stdexcept>
#include <utility>
#include <functional>

namespace fly {

using RecordWriteFunc = void(*)(void* ctx, const CMString& db_id, const CMString& name);
using NotifyRemovedFunc = void(*)(void* ctx, const CMString& db_id, const CMString& name);
using FreezeFunc = void(*)(void* ctx, const CMString& db_id);
using RemoveRequestFunc = void(*)(void* ctx, const CMString& db_id, const CMString& object_name);
using BackupRequestFunc = void(*)(void* ctx, const CMString& db_id, const CMString& object_name);
using NotifyBackupCompleteFunc = void(*)(void* ctx, const CMString& db_id, const CMString& object_name);

class WorkerAgentContext {
public:
    static void set(RecordWriteFunc func, void* ctx) {
        func_ = func;
        ctx_ = ctx;
    }

    static void set_notify_removed_func(NotifyRemovedFunc func, void* ctx) {
        notify_removed_func_ = func;
        notify_removed_ctx_ = ctx;
    }

    static void set_freeze_func(FreezeFunc func, void* ctx) {
        freeze_func_ = func;
        freeze_ctx_ = ctx;
    }

    static void set_remove_request_func(RemoveRequestFunc func, void* ctx) {
        remove_request_func_ = func;
        remove_request_ctx_ = ctx;
    }

    static void clear() {
        func_ = nullptr;
        ctx_ = nullptr;
        register_func_ = nullptr;
        notify_removed_func_ = nullptr;
        notify_removed_ctx_ = nullptr;
        freeze_func_ = nullptr;
        freeze_ctx_ = nullptr;
        remove_request_func_ = nullptr;
        remove_request_ctx_ = nullptr;
        backup_request_func_ = nullptr;
        backup_request_ctx_ = nullptr;
        notify_backup_complete_func_ = nullptr;
        notify_backup_complete_ctx_ = nullptr;
        last_error_type_ = TaskErrorType::UNKNOWN;
    }

    static void record_write(const CMString& db_id, const CMString& object_name) {
        if (func_) {
            func_(ctx_, db_id, object_name);
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
        return func_ != nullptr;
    }

    static void notify_object_removed(const CMString& db_id, const CMString& object_name) {
        if (notify_removed_func_) {
            notify_removed_func_(notify_removed_ctx_, db_id, object_name);
        }
    }

    static void notify_freeze(const CMString& db_id) {
        if (freeze_func_) {
            freeze_func_(freeze_ctx_, db_id);
        }
    }

    static void request_remove(const CMString& db_id, const CMString& object_name) {
        if (remove_request_func_) {
            remove_request_func_(remove_request_ctx_, db_id, object_name);
        }
    }

    static void set_backup_request_func(BackupRequestFunc func, void* ctx) {
        backup_request_func_ = func;
        backup_request_ctx_ = ctx;
    }

    static void request_backup(const CMString& db_id, const CMString& object_name) {
        if (backup_request_func_) {
            backup_request_func_(backup_request_ctx_, db_id, object_name);
        }
    }

    static BackupRequestFunc current_backup_func() { return backup_request_func_; }
    static void* current_backup_ctx() { return backup_request_ctx_; }

    static void set_notify_backup_complete_func(NotifyBackupCompleteFunc func, void* ctx) {
        notify_backup_complete_func_ = func;
        notify_backup_complete_ctx_ = ctx;
    }

    static void notify_backup_complete(const CMString& db_id, const CMString& object_name) {
        if (notify_backup_complete_func_) {
            notify_backup_complete_func_(notify_backup_complete_ctx_, db_id, object_name);
        }
    }

    static void set_last_error_type(TaskErrorType type) {
        last_error_type_ = type;
    }

    static TaskErrorType get_last_error_type() {
        return last_error_type_;
    }

    static RecordWriteFunc current_record_func() { return func_; }
    static void* current_record_ctx() { return ctx_; }

private:
    static inline thread_local RecordWriteFunc func_ = nullptr;
    static inline thread_local void* ctx_ = nullptr;
    static inline thread_local std::function<std::pair<CMString, TaskErrorType>(const CMString&, const CMString&)> register_func_;
    static inline thread_local NotifyRemovedFunc notify_removed_func_ = nullptr;
    static inline thread_local void* notify_removed_ctx_ = nullptr;
    static inline thread_local FreezeFunc freeze_func_ = nullptr;
    static inline thread_local void* freeze_ctx_ = nullptr;
    static inline thread_local RemoveRequestFunc remove_request_func_ = nullptr;
    static inline thread_local void* remove_request_ctx_ = nullptr;
    static inline thread_local BackupRequestFunc backup_request_func_ = nullptr;
    static inline thread_local void* backup_request_ctx_ = nullptr;
    static inline thread_local NotifyBackupCompleteFunc notify_backup_complete_func_ = nullptr;
    static inline thread_local void* notify_backup_complete_ctx_ = nullptr;
    static inline thread_local TaskErrorType last_error_type_ = TaskErrorType::UNKNOWN;
};

}  // namespace fly
