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
    EXPECT_EQ(found->object_name, "test/object");
    EXPECT_EQ(found->file_name, "data_001.dat");
    EXPECT_EQ(found->offset, 100);
    EXPECT_EQ(found->size, 200);
    EXPECT_FALSE(found->is_large);
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
    EXPECT_EQ(entry_a->file_name, "data_001.dat");
    EXPECT_EQ(entry_a->offset, 0);
    EXPECT_EQ(entry_a->size, 100);

    auto entry_c = loaded.find_entry("obj/c");
    ASSERT_TRUE(entry_c.has_value());
    EXPECT_TRUE(entry_c->is_large);
    EXPECT_EQ(entry_c->block_count, 5);
}

TEST_F(LocalIndexTest, LoadFromNonExistentFile) {
    CMString idx_path = test_dir_ + "/nonexistent.idx";
    LocalIndex index(idx_path);
    index.load();

    EXPECT_EQ(index.entry_count(), 0);
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
    EXPECT_EQ(found->file_name, "data_001.dat");
    EXPECT_EQ(found->offset, 0);

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
    EXPECT_EQ(entry->offset, 5000);
    EXPECT_EQ(entry->size, 50);
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
    EXPECT_EQ(a->file_name, "data_001.dat");

    auto b = loaded.find_entry("obj/b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->file_name, "data_002.dat");
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
    EXPECT_EQ(b->size, 200);
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
    EXPECT_EQ(a->size, 100);

    auto b = loaded.find_entry("obj/b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->size, 200);
}

TEST_F(LocalIndexTest, ConcurrentAddRemoveSave) {
    CMString idx_path = make_idx_path("concurrent");

    LocalIndex index(idx_path);

    // Pre-populate with 50 entries
    for (int i = 0; i < 50; i++) {
        index.add_entry({"obj_" + std::to_string(i), "data.dat", i * 10, 10, false, 0});
    }
    index.save();

    std::atomic<bool> done{false};

    // Thread A: add entries 50-99 and save
    std::thread adder([&] {
        for (int i = 50; i < 100 && !done.load(); i++) {
            index.add_entry({"obj_" + std::to_string(i), "data.dat", i * 10, 10, false, 0});
        }
        index.save();
    });

    // Thread B: remove entries 0-25 and save
    std::thread remover([&] {
        for (int i = 0; i < 25 && !done.load(); i++) {
            index.remove_entry("obj_" + std::to_string(i));
        }
        index.save();
    });

    // Thread C: read entry count repeatedly
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

    // Verify final state is consistent: 50 pre + up to 50 added - up to 25 removed
    int64_t count = index.entry_count();
    EXPECT_GE(count, 25);  // At least 50-25=25 original + 0 added (worst case timing)
    EXPECT_LE(count, 100); // At most all entries
}

}