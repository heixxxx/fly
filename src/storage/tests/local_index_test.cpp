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

    IndexEntry* found = index.find_entry("test/object");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->object_name, "test/object");
    EXPECT_EQ(found->file_name, "data_001.dat");
    EXPECT_EQ(found->offset, 100);
    EXPECT_EQ(found->size, 200);
    EXPECT_FALSE(found->is_large);
}

TEST_F(LocalIndexTest, FindNonExistentEntry) {
    LocalIndex index(make_idx_path("find_none"));

    IndexEntry* found = index.find_entry("nonexistent");
    EXPECT_EQ(found, nullptr);
}

TEST_F(LocalIndexTest, RemoveEntry) {
    LocalIndex index(make_idx_path("remove"));

    IndexEntry entry{"obj/remove", "data.dat", 50, 100, false, 0};
    index.add_entry(entry);

    EXPECT_TRUE(index.remove_entry("obj/remove"));
    EXPECT_EQ(index.find_entry("obj/remove"), nullptr);
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

    IndexEntry* entry_a = loaded.find_entry("obj/a");
    ASSERT_NE(entry_a, nullptr);
    EXPECT_EQ(entry_a->file_name, "data_001.dat");
    EXPECT_EQ(entry_a->offset, 0);
    EXPECT_EQ(entry_a->size, 100);

    IndexEntry* entry_c = loaded.find_entry("obj/c");
    ASSERT_NE(entry_c, nullptr);
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

    IndexEntry* found = index.find_entry("obj/same");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->file_name, "data_001.dat");
    EXPECT_EQ(found->offset, 0);

    CMVector<IndexEntry>* all = index.find_all_entries("obj/same");
    ASSERT_NE(all, nullptr);
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

    IndexEntry* entry = loaded.find_entry("obj_50");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->offset, 5000);
    EXPECT_EQ(entry->size, 50);
}

}