#pragma once

#include <common/cpp/common_types.h>

namespace fly {

// Function pointer type for write recording.
// ctx: user context (typically WorkerAgent*), db_id: database identifier, name: object name
using RecordWriteFunc = void(*)(void* ctx, const CMString& db_id, const CMString& name);

// Thread-local context for write tracking during task execution.
// Database calls record_write() during write_object. WorkerAgent sets/clears the callback.
class WorkerAgentContext {
public:
    // Set the write recording callback (called by WorkerAgent::begin_task)
    static void set(RecordWriteFunc func, void* ctx) {
        func_ = func;
        ctx_ = ctx;
    }

    // Clear the callback (called by WorkerAgent::end_task)
    static void clear() {
        func_ = nullptr;
        ctx_ = nullptr;
    }

    // Record a write — calls the registered callback if one exists.
    // Called by Database.write_object.
    static void record_write(const CMString& db_id, const CMString& object_name) {
        if (func_) {
            func_(ctx_, db_id, object_name);
        }
    }

    // Check if a callback is currently registered
    static bool is_active() {
        return func_ != nullptr;
    }

private:
    static inline thread_local RecordWriteFunc func_ = nullptr;
    static inline thread_local void* ctx_ = nullptr;
};

}  // namespace fly