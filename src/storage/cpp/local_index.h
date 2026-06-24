#pragma once

#include <storage/cpp/index_entry.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>

// idx 文件的 op log 记录类型。
//
//   ADD/REMOVE 是数据记录；BEGIN/END/ABORT 是事务段边界标记。
//
// 事务模型（两种写入模式）：
//   - 显式事务（worker task 写入）：BEGIN 包裹的 ADD 进入 pending 区，
//     END 提交进 entries_，ABORT 丢弃 pending。崩溃(进程死亡)→无 END/ABORT
//     →load 时 pending 自然丢弃。
//   - 隐式事务（master 直接 write_object，无 task 状态）：ADD 不在任何段内，
//     立即生效进 entries_，load 时直接判定有效。
//
// 标记是纯粹的段边界，不含 task_id —— 只要"段是否闭合"即可判定该段 idx
// 是否有效。
enum class IdxOpType : uint8_t {
    ADD = 1,
    REMOVE = 2,
    BEGIN = 3,   // 写入段开始：其后 ADD 进入 pending 区，遇 END 才提交
    END = 4,     // 写入段成功提交：pending 区提交进 entries_
    ABORT = 5,   // 写入段撤销：pending 区丢弃（异常清理时打，整段作废）
};

class LocalIndex {
public:
    explicit LocalIndex(const CMString& idx_path);
    ~LocalIndex();

    LocalIndex(const LocalIndex&) = delete;
    LocalIndex& operator=(const LocalIndex&) = delete;

    void add_entry(const IndexEntry& entry);
    bool remove_entry(const CMString& object_name);
    std::optional<IndexEntry> find_entry(const CMString& object_name);
    std::optional<CMVector<IndexEntry>> find_all_entries(const CMString& object_name);

    void save();
    void save_legacy();
    void load();
    void compact();

    // 写入段标记。mark_begin 后的 ADD 进入 pending 区，mark_end 提交 pending，
    // mark_abort 丢弃 pending。崩溃(进程死亡) → 无 END/ABORT → load 时 pending
    // 自然丢弃。
    // 标记记录携带空 body（仅 8 字节 header），不占用 object_name 命名空间。
    void mark_begin();
    void mark_end();
    void mark_abort();

    int64_t entry_count() const;
    CMVector<IndexEntry> get_all_entries() const;

    // 诊断：load() 结束时是否检测到未闭合段(pending 非空)。用于 load_db 告警。
    bool had_unclosed_segment() const;

private:
    void append_add(const CMString& object_name, const CMVector<IndexEntry>& entries);
    void append_remove(const CMString& object_name);
    void append_marker(IdxOpType op);

    CMString idx_path_;
    CMUnorderedMap<CMString, CMVector<IndexEntry>> entries_;

    CMUnorderedMap<CMString, CMVector<IndexEntry>> pending_adds_;
    CMUnorderedSet<CMString> pending_removes_;
    bool modified_ = false;
    mutable std::mutex mutex_;

    // load() 诊断结果：加载结束时 pending 区是否非空(检测到崩溃遗留的未闭合段)。
    bool had_unclosed_segment_ = false;
};
