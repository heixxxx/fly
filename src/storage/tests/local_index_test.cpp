#include <gtest/gtest.h>
#include <storage/cpp/local_index.h>
#include <common/cpp/common_types.h>
#include <filesystem>
#include <fstream>

namespace {

class LocalIndexTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_local_index_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    CMString make_idx_path(const CMString& name) const {
        return test_dir_ + "/" + name + ".idx";
    }
};

TEST_F(LocalIndexTest, AddAndFindEntry) {
    LocalIndex index(make_idx_path("add_find"));

    IndexEntry entry{"test/object", "data_001.dat", 100, 200, false, 0};
    index.add_entry(entry);

    auto found = index.find_entry("test/object");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->object_name_, "test/object");
    EXPECT_EQ(found->file_name_, "data_001.dat");
    EXPECT_EQ(found->offset_, 100);
    EXPECT_EQ(found->size_, 200);
    EXPECT_FALSE(found->is_large_);
}

TEST_F(LocalIndexTest, FindNonExistentEntry) {
    LocalIndex index(make_idx_path("find_none"));

    auto found = index.find_entry("nonexistent");
    EXPECT_FALSE(found.has_value());
}

TEST_F(LocalIndexTest, RemoveEntry) {
    LocalIndex index(make_idx_path("remove"));

    IndexEntry entry{"obj/remove", "data.dat", 50, 100, false, 0};
    index.add_entry(entry);

    EXPECT_TRUE(index.remove_entry("obj/remove"));
    EXPECT_EQ(index.find_entry("obj/remove"), std::nullopt);
    EXPECT_EQ(index.entry_count(), 0);
}

TEST_F(LocalIndexTest, RemoveNonExistentEntry) {
    LocalIndex index(make_idx_path("remove_none"));
    EXPECT_FALSE(index.remove_entry("nonexistent"));
}

TEST_F(LocalIndexTest, EntryCount) {
    LocalIndex index(make_idx_path("count"));

    EXPECT_EQ(index.entry_count(), 0);

    index.add_entry({"obj1", "data.dat", 0, 10, false, 0});
    EXPECT_EQ(index.entry_count(), 1);

    index.add_entry({"obj2", "data.dat", 10, 20, false, 0});
    EXPECT_EQ(index.entry_count(), 2);

    index.remove_entry("obj1");
    EXPECT_EQ(index.entry_count(), 1);
}

TEST_F(LocalIndexTest, SaveAndLoad) {
    CMString idx_path = make_idx_path("save_load");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/a", "data_001.dat", 0, 100, false, 0});
        index.add_entry({"obj/b", "data_001.dat", 100, 200, false, 0});
        index.add_entry({"obj/c", "data_002.dat", 0, 300, true, 5});
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();

    EXPECT_EQ(loaded.entry_count(), 3);

    auto entry_a = loaded.find_entry("obj/a");
    ASSERT_TRUE(entry_a.has_value());
    EXPECT_EQ(entry_a->file_name_, "data_001.dat");
    EXPECT_EQ(entry_a->offset_, 0);
    EXPECT_EQ(entry_a->size_, 100);

    auto entry_c = loaded.find_entry("obj/c");
    ASSERT_TRUE(entry_c.has_value());
    EXPECT_TRUE(entry_c->is_large_);
    EXPECT_EQ(entry_c->block_count_, 5);
}

TEST_F(LocalIndexTest, LoadFromNonExistentFile) {
    CMString idx_path = test_dir_ + "/nonexistent.idx";
    LocalIndex index(idx_path);
    index.load();

    EXPECT_EQ(index.entry_count(), 0);
}

// 契约：LocalIndex 是 per-(db,writer) 的，idx 文件天然属于同一个 db，
// 因此 entry 的 object_name_ 只存 short_name（不冗余存 db_id 前缀）。
// save→load 循环后 entry.object_name_ 必须仍是 short_name，不含 ":" 前缀。
TEST_F(LocalIndexTest, EntryObjectNameIsShortNameNoDbIdPrefix) {
    CMString idx_path = make_idx_path("short_name_contract");

    {
        LocalIndex index(idx_path);
        // 模拟 Database::commit_write 传入的 short_name（无 db_id 前缀）
        index.add_entry({"matrix", "data_001.dat", 0, 100, false, 0});
        index.add_entry({"result/obj_1", "data_001.dat", 100, 200, false, 0});
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();

    auto all = loaded.get_all_entries();
    ASSERT_EQ(all.size(), 2u);
    // 每个 entry.object_name_ 必须是 short_name，不含 ":" 分隔符前缀
    for (const auto& e : all) {
        EXPECT_NE(e.object_name_, CMString{});
        // short_name 不应含 ":" —— db_id 前缀形式（如 "abc123:obj"）不应出现
        EXPECT_EQ(e.object_name_.find(':'), CMString::npos)
            << "entry.object_name_ should be short_name without ':' but got: " << e.object_name_;
    }

    // find_entry 用 short_name 查询（与 add_entry 的 key 一致）
    EXPECT_TRUE(loaded.find_entry("matrix").has_value());
    EXPECT_TRUE(loaded.find_entry("result/obj_1").has_value());
}

TEST_F(LocalIndexTest, GetAllEntries) {
    LocalIndex index(make_idx_path("get_all"));

    index.add_entry({"obj/1", "data.dat", 0, 10, false, 0});
    index.add_entry({"obj/2", "data.dat", 10, 20, false, 0});
    index.add_entry({"obj/3", "data.dat", 30, 30, false, 0});

    CMVector<IndexEntry> entries = index.get_all_entries();
    EXPECT_EQ(entries.size(), 3u);
}

TEST_F(LocalIndexTest, MultipleEntriesPerKey) {
    LocalIndex index(make_idx_path("multi_per_key"));

    index.add_entry({"obj/same", "data_001.dat", 0, 100, false, 0});
    index.add_entry({"obj/same", "data_002.dat", 200, 50, true, 2});

    EXPECT_EQ(index.entry_count(), 2);

    auto found = index.find_entry("obj/same");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->file_name_, "data_001.dat");
    EXPECT_EQ(found->offset_, 0);

    auto all = index.find_all_entries("obj/same");
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 2u);
}

TEST_F(LocalIndexTest, MultipleEntriesSaveLoad) {
    CMString idx_path = make_idx_path("multi_save_load");

    {
        LocalIndex index(idx_path);
        for (int i = 0; i < 100; i++) {
            CMString name = "obj_" + std::to_string(i);
            index.add_entry({name, "data.dat", i * 100, 50, false, 0});
        }
        EXPECT_EQ(index.entry_count(), 100);
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 100);

    auto entry = loaded.find_entry("obj_50");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->offset_, 5000);
    EXPECT_EQ(entry->size_, 50);
}

TEST_F(LocalIndexTest, IncrementalAddRecords) {
    CMString idx_path = make_idx_path("incr_add");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/a", "data_001.dat", 0, 100, false, 0});
        index.save();
        index.add_entry({"obj/b", "data_002.dat", 0, 200, false, 0});
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 2);

    auto a = loaded.find_entry("obj/a");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->file_name_, "data_001.dat");

    auto b = loaded.find_entry("obj/b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->file_name_, "data_002.dat");
}

TEST_F(LocalIndexTest, IncrementalRemoveRecord) {
    CMString idx_path = make_idx_path("incr_remove");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/a", "data.dat", 0, 100, false, 0});
        index.add_entry({"obj/b", "data.dat", 100, 200, false, 0});
        index.save();
        index.remove_entry("obj/a");
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 1);

    EXPECT_EQ(loaded.find_entry("obj/a"), std::nullopt);

    auto b = loaded.find_entry("obj/b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->size_, 200);
}

TEST_F(LocalIndexTest, IncrementalMixedOps) {
    CMString idx_path = make_idx_path("incr_mixed");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/1", "data.dat", 0, 10, false, 0});
        index.add_entry({"obj/2", "data.dat", 10, 20, false, 0});
        index.save();
        index.remove_entry("obj/1");
        index.add_entry({"obj/3", "data.dat", 30, 30, false, 0});
        index.save();
        index.remove_entry("obj/2");
        index.add_entry({"obj/4", "data.dat", 40, 40, false, 0});
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 2);

    EXPECT_EQ(loaded.find_entry("obj/1"), std::nullopt);
    EXPECT_EQ(loaded.find_entry("obj/2"), std::nullopt);
    ASSERT_TRUE(loaded.find_entry("obj/3").has_value());
    ASSERT_TRUE(loaded.find_entry("obj/4").has_value());
}

TEST_F(LocalIndexTest, CompactReducesFileSize) {
    CMString idx_path = make_idx_path("compact");

    {
        LocalIndex index(idx_path);
        for (int i = 0; i < 50; i++) {
            index.add_entry({"obj_" + std::to_string(i), "data.dat", i * 10, 10, false, 0});
        }
        index.save();
        for (int i = 0; i < 25; i++) {
            index.remove_entry("obj_" + std::to_string(i));
        }
        index.save();
    }

    auto size_before = std::filesystem::file_size(idx_path);

    LocalIndex index(idx_path);
    index.load();
    index.compact();

    auto size_after = std::filesystem::file_size(idx_path);
    EXPECT_LT(size_after, size_before);
    EXPECT_EQ(index.entry_count(), 25);

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 25);
    EXPECT_EQ(loaded.find_entry("obj_0"), std::nullopt);
    ASSERT_TRUE(loaded.find_entry("obj_25").has_value());
}

TEST_F(LocalIndexTest, LoadLegacyFormat) {
    CMString idx_path = make_idx_path("legacy_compat");

    {
        LocalIndex old_idx(idx_path);
        old_idx.add_entry({"obj/a", "data.dat", 0, 100, false, 0});
        old_idx.add_entry({"obj/b", "data.dat", 100, 200, false, 0});
        old_idx.save_legacy();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 2);

    auto a = loaded.find_entry("obj/a");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->size_, 100);

    auto b = loaded.find_entry("obj/b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->size_, 200);
}

TEST_F(LocalIndexTest, ConcurrentAddRemoveSave) {
    CMString idx_path = make_idx_path("concurrent");

    LocalIndex index(idx_path);

    for (int i = 0; i < 50; i++) {
        index.add_entry({"obj_" + std::to_string(i), "data.dat", i * 10, 10, false, 0});
    }
    index.save();

    std::atomic<bool> done{false};

    std::thread adder([&] {
        for (int i = 50; i < 100 && !done.load(); i++) {
            index.add_entry({"obj_" + std::to_string(i), "data.dat", i * 10, 10, false, 0});
        }
        index.save();
    });

    std::thread remover([&] {
        for (int i = 0; i < 25 && !done.load(); i++) {
            index.remove_entry("obj_" + std::to_string(i));
        }
        index.save();
    });

    std::thread reader([&] {
        for (int i = 0; i < 100 && !done.load(); i++) {
            index.entry_count();
            index.find_entry("obj_0");
        }
    });

    adder.join();
    remover.join();
    done = true;
    reader.join();

    int64_t count = index.entry_count();
    EXPECT_GE(count, 25);
    EXPECT_LE(count, 100);
}

TEST_F(LocalIndexTest, FindAllEntriesNonExistent) {
    LocalIndex index(make_idx_path("find_all_none"));
    auto result = index.find_all_entries("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(LocalIndexTest, SaveWithoutChangesIsNoop) {
    CMString idx_path = make_idx_path("save_noop");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/a", "data.dat", 0, 100, false, 0});
        index.save();
    }

    auto size_before = std::filesystem::file_size(idx_path);

    {
        LocalIndex index(idx_path);
        index.load();
        index.save();
    }

    auto size_after = std::filesystem::file_size(idx_path);
    EXPECT_EQ(size_before, size_after);
}

TEST_F(LocalIndexTest, GetAllEntriesEmpty) {
    LocalIndex index(make_idx_path("get_all_empty"));
    auto entries = index.get_all_entries();
    EXPECT_TRUE(entries.empty());
}

TEST_F(LocalIndexTest, AddEntryWithHostAndContextHash) {
    LocalIndex index(make_idx_path("host_ctx"));

    IndexEntry entry;
    entry.object_name_ = "obj/with_host";
    entry.file_name_ = "data.dat";
    entry.offset_ = 0;
    entry.size_ = 50;
    entry.is_large_ = false;
    entry.block_count_ = 0;
    entry.host_ = "192.168.1.1";
    entry.write_context_hash_ = "abc123";

    index.add_entry(entry);
    index.save();

    LocalIndex loaded(make_idx_path("host_ctx"));
    loaded.load();

    auto found = loaded.find_entry("obj/with_host");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->host_, "192.168.1.1");
    EXPECT_EQ(found->write_context_hash_, "abc123");
}

TEST_F(LocalIndexTest, RemoveOneEntryFromMultiplePerKey) {
    LocalIndex index(make_idx_path("remove_one_multi"));

    index.add_entry({"obj/same", "data_001.dat", 0, 100, false, 0});
    index.add_entry({"obj/same", "data_002.dat", 200, 50, true, 2});

    EXPECT_EQ(index.entry_count(), 2);
    EXPECT_TRUE(index.remove_entry("obj/same"));
    EXPECT_EQ(index.entry_count(), 0);
    EXPECT_FALSE(index.find_entry("obj/same").has_value());
}

TEST_F(LocalIndexTest, CompactThenAddAndSave) {
    CMString idx_path = make_idx_path("compact_add");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/a", "data.dat", 0, 10, false, 0});
        index.add_entry({"obj/b", "data.dat", 10, 20, false, 0});
        index.save();
        index.remove_entry("obj/a");
        index.compact();
        index.add_entry({"obj/c", "data.dat", 30, 30, false, 0});
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 2);
    EXPECT_FALSE(loaded.find_entry("obj/a").has_value());
    ASSERT_TRUE(loaded.find_entry("obj/b").has_value());
    ASSERT_TRUE(loaded.find_entry("obj/c").has_value());
}

TEST_F(LocalIndexTest, LoadEmptyFile) {
    CMString idx_path = make_idx_path("empty_file");

    std::ofstream ofs(idx_path, std::ios::binary);
    ofs.close();

    LocalIndex index(idx_path);
    index.load();
    EXPECT_EQ(index.entry_count(), 0);
}

TEST_F(LocalIndexTest, SaveAfterRemovePersistsRemoval) {
    CMString idx_path = make_idx_path("save_remove");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/a", "data.dat", 0, 100, false, 0});
        index.add_entry({"obj/b", "data.dat", 100, 200, false, 0});
        index.save();
        index.remove_entry("obj/a");
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 1);
    EXPECT_FALSE(loaded.find_entry("obj/a").has_value());
    EXPECT_TRUE(loaded.find_entry("obj/b").has_value());
}

TEST_F(LocalIndexTest, CompactEmptyIndexCreatesFile) {
    CMString idx_path = make_idx_path("compact_empty");

    LocalIndex index(idx_path);
    index.compact();

    EXPECT_TRUE(std::filesystem::exists(idx_path));
}

TEST_F(LocalIndexTest, CompactPreservesAllEntries) {
    CMString idx_path = make_idx_path("compact_preserve");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/1", "data.dat", 0, 10, false, 0});
        index.add_entry({"obj/2", "data.dat", 10, 20, false, 0});
        index.add_entry({"obj/3", "data.dat", 20, 30, false, 0});
        index.save();
    }

    LocalIndex index(idx_path);
    index.load();
    EXPECT_EQ(index.entry_count(), 3);
    index.compact();

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 3);
    EXPECT_TRUE(loaded.find_entry("obj/1").has_value());
    EXPECT_TRUE(loaded.find_entry("obj/2").has_value());
    EXPECT_TRUE(loaded.find_entry("obj/3").has_value());
}

TEST_F(LocalIndexTest, FindAllEntriesReturnsCorrectEntries) {
    CMString idx_path = make_idx_path("find_all_correct");
    LocalIndex index(idx_path);

    index.add_entry({"obj/multi", "d1.dat", 0, 10, false, 0});
    index.add_entry({"obj/multi", "d2.dat", 10, 20, true, 1});
    index.add_entry({"obj/multi", "d3.dat", 20, 30, false, 0});

    auto all = index.find_all_entries("obj/multi");
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 3u);
    EXPECT_EQ((*all)[0].file_name_, "d1.dat");
    EXPECT_EQ((*all)[1].file_name_, "d2.dat");
    EXPECT_EQ((*all)[2].file_name_, "d3.dat");
}

TEST_F(LocalIndexTest, FindAllEntriesAfterRemove) {
    LocalIndex index(make_idx_path("find_all_rm"));

    index.add_entry({"obj/1", "data.dat", 0, 10, false, 0});
    index.add_entry({"obj/2", "data.dat", 10, 20, false, 0});

    EXPECT_TRUE(index.find_all_entries("obj/1").has_value());
    EXPECT_EQ(index.find_all_entries("obj/1")->size(), 1u);

    index.remove_entry("obj/1");
    EXPECT_FALSE(index.find_all_entries("obj/1").has_value());
}

TEST_F(LocalIndexTest, SaveLoadWithLargeBlockCount) {
    CMString idx_path = make_idx_path("large_block");

    {
        LocalIndex index(idx_path);
        IndexEntry entry;
        entry.object_name_ = "obj/large";
        entry.file_name_ = "data.dat";
        entry.offset_ = 0;
        entry.size_ = 1000000;
        entry.is_large_ = true;
        entry.block_count_ = 1000;
        index.add_entry(entry);
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    auto found = loaded.find_entry("obj/large");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->is_large_);
    EXPECT_EQ(found->block_count_, 1000);
    EXPECT_EQ(found->size_, 1000000);
}

TEST_F(LocalIndexTest, AddEntryClearsPendingRemoves) {
    LocalIndex index(make_idx_path("add_clears_rm"));

    index.add_entry({"obj/x", "data.dat", 0, 10, false, 0});
    index.remove_entry("obj/x");
    EXPECT_FALSE(index.find_entry("obj/x").has_value());

    index.add_entry({"obj/x", "data.dat", 10, 20, false, 0});
    EXPECT_TRUE(index.find_entry("obj/x").has_value());
}

TEST_F(LocalIndexTest, MultipleSaveCallsWithoutChanges) {
    CMString idx_path = make_idx_path("multi_save");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/a", "data.dat", 0, 10, false, 0});
        index.save();
    }

    auto size1 = std::filesystem::file_size(idx_path);

    {
        LocalIndex index(idx_path);
        index.load();
        index.save();
        index.save();
        index.save();
    }

    auto size2 = std::filesystem::file_size(idx_path);
    EXPECT_EQ(size1, size2);
}

TEST_F(LocalIndexTest, SaveLegacyThenLoadAndModify) {
    CMString idx_path = make_idx_path("legacy_modify");

    {
        LocalIndex index(idx_path);
        index.add_entry({"obj/legacy", "data.dat", 0, 100, false, 0});
        index.save_legacy();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 1);
    EXPECT_TRUE(loaded.find_entry("obj/legacy").has_value());
}

TEST_F(LocalIndexTest, GetAllEntriesAfterMultipleAddsAndRemoves) {
    LocalIndex index(make_idx_path("all_entries_mixed"));

    index.add_entry({"obj/1", "d1.dat", 0, 10, false, 0});
    index.add_entry({"obj/2", "d2.dat", 10, 20, false, 0});
    index.add_entry({"obj/3", "d3.dat", 20, 30, false, 0});
    index.remove_entry("obj/2");

    auto entries = index.get_all_entries();
    EXPECT_EQ(entries.size(), 2);

    bool found1 = false, found3 = false;
    for (const auto& e : entries) {
        if (e.object_name_ == "obj/1") found1 = true;
        if (e.object_name_ == "obj/3") found3 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found3);
}

TEST_F(LocalIndexTest, RemoveEntryReturnsFalseAfterAlreadyRemoved) {
    LocalIndex index(make_idx_path("double_rm"));

    index.add_entry({"obj/del", "data.dat", 0, 10, false, 0});
    EXPECT_TRUE(index.remove_entry("obj/del"));
    EXPECT_FALSE(index.remove_entry("obj/del"));
}

// =============================================================================
// 段标记（BEGIN/END/ABORT）测试 —— 事务化 pending 区语义
// =============================================================================

TEST_F(LocalIndexTest, BeginEndCommit) {
    // BEGIN → ADD → END：load 后 obj1 在 entries_（正常提交）
    {
        LocalIndex index(make_idx_path("begin_end_commit"));
        index.mark_begin();
        index.add_entry({"obj/1", "d1.dat", 0, 10, false, 0});
        index.save();
        index.mark_end();
    }
    LocalIndex index(make_idx_path("begin_end_commit"));
    index.load();
    EXPECT_EQ(index.entry_count(), 1);
    EXPECT_TRUE(index.find_entry("obj/1").has_value());
    EXPECT_FALSE(index.had_unclosed_segment());
}

TEST_F(LocalIndexTest, BeginAbortDropped) {
    // BEGIN → ADD → ABORT：load 后 entries_ 为空（整段撤销）
    {
        LocalIndex index(make_idx_path("begin_abort"));
        index.mark_begin();
        index.add_entry({"obj/1", "d1.dat", 0, 10, false, 0});
        index.save();
        index.mark_abort();
    }
    LocalIndex index(make_idx_path("begin_abort"));
    index.load();
    EXPECT_EQ(index.entry_count(), 0);
    EXPECT_FALSE(index.find_entry("obj/1").has_value());
    EXPECT_FALSE(index.had_unclosed_segment());   // ABORT 闭合了段
}

TEST_F(LocalIndexTest, BeginNoEndDroppedOnLoad) {
    // BEGIN → ADD → [EOF 无 END/ABORT]：模拟崩溃，load 后 pending 丢弃
    {
        LocalIndex index(make_idx_path("begin_no_end"));
        index.mark_begin();
        index.add_entry({"obj/1", "d1.dat", 0, 10, false, 0});
        index.save();
        // 不打 END/ABORT，直接析构（模拟崩溃）
    }
    LocalIndex index(make_idx_path("begin_no_end"));
    index.load();
    EXPECT_EQ(index.entry_count(), 0);
    EXPECT_FALSE(index.find_entry("obj/1").has_value());
    EXPECT_TRUE(index.had_unclosed_segment());   // 检测到未闭合段
}

TEST_F(LocalIndexTest, MultipleSegmentsMixed) {
    // (BEGIN→ADD a→END)(BEGIN→ADD b→[EOF])：load 后只有 a
    {
        LocalIndex index(make_idx_path("multi_seg"));
        index.mark_begin();
        index.add_entry({"obj/a", "da.dat", 0, 10, false, 0});
        index.save();
        index.mark_end();
        index.mark_begin();
        index.add_entry({"obj/b", "db.dat", 10, 20, false, 0});
        index.save();
        // 第二段无 END，模拟崩溃
    }
    LocalIndex index(make_idx_path("multi_seg"));
    index.load();
    EXPECT_EQ(index.entry_count(), 1);
    EXPECT_TRUE(index.find_entry("obj/a").has_value());
    EXPECT_FALSE(index.find_entry("obj/b").has_value());
    EXPECT_TRUE(index.had_unclosed_segment());
}

TEST_F(LocalIndexTest, LegacyNoMarkersDirectEffect) {
    // 纯 ADD 无标记（旧格式 / master 隐式事务）：load 后直接生效
    {
        LocalIndex index(make_idx_path("legacy_no_markers"));
        index.add_entry({"obj/1", "d1.dat", 0, 10, false, 0});
        index.add_entry({"obj/2", "d2.dat", 10, 20, false, 0});
        index.save();
    }
    LocalIndex index(make_idx_path("legacy_no_markers"));
    index.load();
    EXPECT_EQ(index.entry_count(), 2);
    EXPECT_FALSE(index.had_unclosed_segment());
}

TEST_F(LocalIndexTest, BeginAsFirstRecordFormatDetection) {
    // BEGIN 是首字节：格式检测必须正确识别为新格式（回归）
    {
        LocalIndex index(make_idx_path("begin_first"));
        index.mark_begin();
        index.add_entry({"obj/1", "d1.dat", 0, 10, false, 0});
        index.save();
        index.mark_end();
    }
    LocalIndex index(make_idx_path("begin_first"));
    index.load();
    // 若格式检测错误（误判 legacy），entries_ 会为空
    EXPECT_EQ(index.entry_count(), 1);
    EXPECT_TRUE(index.find_entry("obj/1").has_value());
}

TEST_F(LocalIndexTest, ImplicitAndExplicitMixed) {
    // 段外 ADD（隐式）+ 段内 ADD（显式提交）：两者都应保留
    {
        LocalIndex index(make_idx_path("mixed_implicit_explicit"));
        index.add_entry({"obj/implicit", "d0.dat", 0, 10, false, 0});   // 段外，立即生效
        index.save();
        index.mark_begin();
        index.add_entry({"obj/explicit", "d1.dat", 10, 20, false, 0});
        index.save();
        index.mark_end();
    }
    LocalIndex index(make_idx_path("mixed_implicit_explicit"));
    index.load();
    EXPECT_EQ(index.entry_count(), 2);
    EXPECT_TRUE(index.find_entry("obj/implicit").has_value());
    EXPECT_TRUE(index.find_entry("obj/explicit").has_value());
}

TEST_F(LocalIndexTest, AbortDropsOnlyPendingSegment) {
    // 段外 ADD + (BEGIN→ADD→ABORT)：段外保留，段内丢弃
    {
        LocalIndex index(make_idx_path("abort_keeps_implicit"));
        index.add_entry({"obj/keep", "d0.dat", 0, 10, false, 0});   // 段外
        index.save();
        index.mark_begin();
        index.add_entry({"obj/drop", "d1.dat", 10, 20, false, 0});
        index.save();
        index.mark_abort();
    }
    LocalIndex index(make_idx_path("abort_keeps_implicit"));
    index.load();
    EXPECT_EQ(index.entry_count(), 1);
    EXPECT_TRUE(index.find_entry("obj/keep").has_value());
    EXPECT_FALSE(index.find_entry("obj/drop").has_value());
    EXPECT_FALSE(index.had_unclosed_segment());
}

}