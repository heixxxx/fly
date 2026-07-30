#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <common/cpp/fly_buffer.h>
#include <stdexcept>
#include <utility>
#include <functional>

namespace fly {

class WorkerAgentContext {
public:
    static void set_record_write_func(std::function<void(const CMString& db_id, const CMString& name, int64_t size)> func) {
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
        set_var_func_ = nullptr;
        get_var_func_ = nullptr;
        remove_var_func_ = nullptr;
        push_message_func_ = nullptr;
        last_error_type_ = TaskErrorType::UNKNOWN;
        transaction_mode_ = false;
    }

    static void record_write(const CMString& db_id, const CMString& object_name, int64_t size) {
        if (record_write_func_) {
            record_write_func_(db_id, object_name, size);
        }
    }

    static void set_register_func(std::function<std::pair<CMString, TaskErrorType>(const CMString&, const CMString&, int64_t)> func) {
        register_func_ = std::move(func);
    }

    static std::pair<CMString, TaskErrorType> register_write(const CMString& db_id, const CMString& object_name, int64_t compressed_size) {
        if (register_func_) {
            return register_func_(db_id, object_name, compressed_size);
        }
        return {"", TaskErrorType::UNKNOWN};
    }

    static bool is_active() {
        return record_write_func_ != nullptr;
    }

    // 事务模式：仅 worker task 上下文激活（begin_task 设、end_task/clear 清）。
    // master 直接 write_object 时不激活——其 ADD 为段外隐式事务，立即生效。
    // 用于区分"worker task 写入需 BEGIN/END 包裹"与"master 写入直接落盘"。
    static void set_transaction_mode(bool enabled) {
        transaction_mode_ = enabled;
    }

    static bool is_transaction_mode() {
        return transaction_mode_;
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

    // ---- Var service (lightweight small-object KV) ----
    // All var names passed here are FULL names (db_id:short_name). The master
    // func splits off db_id to locate the Database; the worker func sends the
    // full name over the wire.
    // set_var: synchronous. Returns true on success (var stored on master).
    // value is an already-serialized FlyBufferPtr (pickle or FLY_ENCODE_TO_BUFFER).
    static void set_set_var_func(std::function<bool(const CMString& full_var_name,
                                                     FlyBufferPtr value, const CMString& type_name)> func) {
        set_var_func_ = std::move(func);
    }

    // get_var: synchronous. Returns (success, value, type_name). On miss,
    // success=false and value is nullptr.
    static void set_get_var_func(std::function<std::tuple<bool, FlyBufferPtr, CMString>(
        const CMString& full_var_name)> func) {
        get_var_func_ = std::move(func);
    }

    // remove_var: asynchronous (fire-and-forget to master).
    static void set_remove_var_func(std::function<void(const CMString& full_var_name)> func) {
        remove_var_func_ = std::move(func);
    }

    static bool set_var(const CMString& full_var_name,
                        FlyBufferPtr value, const CMString& type_name) {
        if (set_var_func_) {
            return set_var_func_(full_var_name, value, type_name);
        }
        return false;
    }

    static std::tuple<bool, FlyBufferPtr, CMString> get_var(const CMString& full_var_name) {
        if (get_var_func_) {
            return get_var_func_(full_var_name);
        }
        return {false, nullptr, ""};
    }

    static void remove_var(const CMString& full_var_name) {
        if (remove_var_func_) {
            remove_var_func_(full_var_name);
        }
    }

    // ---- Message 推送（高价值日志远程上报）----
    // level 用 uint8_t 传 LogLevel 的 underlying 值（避免 common 模块依赖 log 模块）。
    // source 为触发位置标识（仅打印标注，不参与配额）。
    // WorkerAgent::begin_task 绑定为发送 LogMessage 到 master；非 task 上下文为 no-op。
    // 注意：此处仅负责「推送」，白名单检查 / 配额 / 本地 debug log 已由 MSG 宏在上游完成。
    static void set_push_message_func(std::function<void(uint8_t level, const CMString& domain_id, int32_t source, const CMString& msg)> func) {
        push_message_func_ = std::move(func);
    }

    static void push_message(uint8_t level, const CMString& domain_id, int32_t source, const CMString& msg) {
        if (push_message_func_) {
            push_message_func_(level, domain_id, source, msg);
        }
    }

    static std::function<void(const CMString&, const CMString&)>& current_backup_func() { return backup_request_func_; }

    static void set_last_error_type(TaskErrorType type) {
        last_error_type_ = type;
    }

    static TaskErrorType get_last_error_type() {
        return last_error_type_;
    }

    static void set_current_write_hash(const CMString& hash) {
        current_write_hash_ = hash;
    }

    static CMString get_current_write_hash() {
        return current_write_hash_;
    }

    static void clear_current_write_hash() {
        current_write_hash_.clear();
    }

    static std::function<void(const CMString&, const CMString&, int64_t)>& current_record_func() { return record_write_func_; }

private:
    static inline thread_local std::function<void(const CMString&, const CMString&, int64_t)> record_write_func_;
    static inline thread_local std::function<std::pair<CMString, TaskErrorType>(const CMString&, const CMString&, int64_t)> register_func_;
    static inline thread_local std::function<void(const CMString&, const CMString&)> notify_removed_func_;
    static inline thread_local std::function<void(const CMString&)> freeze_func_;
    static inline thread_local std::function<void(const CMString&, const CMString&)> remove_request_func_;
    static inline thread_local std::function<void(const CMString&, const CMString&)> backup_request_func_;
    static inline thread_local std::function<bool(const CMString&, FlyBufferPtr, const CMString&)> set_var_func_;
    static inline thread_local std::function<std::tuple<bool, FlyBufferPtr, CMString>(const CMString&)> get_var_func_;
    static inline thread_local std::function<void(const CMString&)> remove_var_func_;
    static inline thread_local std::function<void(uint8_t, const CMString&, int32_t, const CMString&)> push_message_func_;
    static inline thread_local TaskErrorType last_error_type_ = TaskErrorType::UNKNOWN;
    static inline thread_local CMString current_write_hash_;
    static inline thread_local bool transaction_mode_ = false;
};

}  // namespace fly
