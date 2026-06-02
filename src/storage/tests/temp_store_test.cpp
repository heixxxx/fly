#include <gtest/gtest.h>
#include <storage/cpp/temp_store.h>
#include <common/cpp/common_types.h>
#include <filesystem>

namespace {

TEST(TempStoreTest, PutAndGetMemory) {
    fly::TempStore ts(1024 * 1024);
    ts.put("k1", "data_1");
    auto [found, result] = ts.get("k1");
    ASSERT_TRUE(found);
    EXPECT_EQ(result, "data_1");
}

TEST(TempStoreTest, GetNonexistent) {
    fly::TempStore ts(1024 * 1024);
    auto [found, result] = ts.get("no:such:key");
    EXPECT_FALSE(found);
}

TEST(TempStoreTest, Has) {
    fly::TempStore ts(1024 * 1024);
    EXPECT_FALSE(ts.has("k1"));
    ts.put("k1", "d");
    EXPECT_TRUE(ts.has("k1"));
    ts.remove("k1");
    EXPECT_FALSE(ts.has("k1"));
}

TEST(TempStoreTest, Remove) {
    fly::TempStore ts(1024 * 1024);
    ts.put("k1", "d");
    ts.remove("k1");
    EXPECT_FALSE(ts.has("k1"));
    auto [found, _] = ts.get("k1");
    EXPECT_FALSE(found);
}

TEST(TempStoreTest, Overwrite) {
    fly::TempStore ts(1024 * 1024);
    ts.put("k1", "old");
    ts.put("k1", "new");
    auto [found, result] = ts.get("k1");
    ASSERT_TRUE(found);
    EXPECT_EQ(result, "new");
}

TEST(TempStoreTest, MemoryTracking) {
    fly::TempStore ts(1024 * 1024);
    EXPECT_EQ(ts.mem_bytes(), 0);
    ts.put("k1", CMString(100, 'a'));
    EXPECT_EQ(ts.mem_bytes(), 100);
    ts.put("k2", CMString(200, 'b'));
    EXPECT_EQ(ts.mem_bytes(), 300);
    ts.remove("k1");
    EXPECT_EQ(ts.mem_bytes(), 200);
}

TEST(TempStoreTest, EvictToDisk) {
    fly::TempStore ts(200);
    ts.put("small", CMString(50, 'x'));
    EXPECT_EQ(ts.mem_bytes(), 50);
    ts.put("big", CMString(300, 'y'));
    auto [fs, ds] = ts.get("small");
    ASSERT_TRUE(fs);
    EXPECT_EQ(ds.size(), 50u);
    auto [fb, db] = ts.get("big");
    ASSERT_TRUE(fb);
    EXPECT_EQ(db.size(), 300u);
}

TEST(TempStoreTest, MultipleKeys) {
    fly::TempStore ts(1024 * 1024);
    for (int i = 0; i < 10; i++) {
        ts.put("k" + std::to_string(i), "d" + std::to_string(i));
    }
    for (int i = 0; i < 10; i++) {
        auto [found, result] = ts.get("k" + std::to_string(i));
        ASSERT_TRUE(found);
        EXPECT_EQ(result, "d" + std::to_string(i));
    }
}

TEST(TempStoreTest, CleanupAll) {
    fly::TempStore ts(1024 * 1024);
    ts.put("k1", "d1");
    ts.put("k2", "d2");
    ts.cleanup_all();
    EXPECT_FALSE(ts.has("k1"));
    EXPECT_FALSE(ts.has("k2"));
    EXPECT_EQ(ts.mem_bytes(), 0);
}

TEST(TempStoreTest, RemoveNonexistent) {
    fly::TempStore ts(1024 * 1024);
    EXPECT_NO_THROW(ts.remove("no:such:key"));
}

TEST(TempStoreTest, DestructorCleansUp) {
    {
        fly::TempStore ts(1024 * 1024);
        ts.put("k1", "d1");
        EXPECT_TRUE(ts.has("k1"));
    }
}

TEST(TempStoreTest, PutExactlyAtMaxBytesFitsInMemory) {
    fly::TempStore ts(100);
    CMString data(100, 'x');
    ts.put("exact", data);
    auto [found, result] = ts.get("exact");
    ASSERT_TRUE(found);
    EXPECT_EQ(result.size(), 100u);
    EXPECT_EQ(ts.mem_bytes(), 100);
}

TEST(TempStoreTest, PutOverMaxBytesSpillsToDisk) {
    fly::TempStore ts(100);
    CMString data(101, 'y');
    ts.put("over", data);
    auto [found, result] = ts.get("over");
    ASSERT_TRUE(found);
    EXPECT_EQ(result.size(), 101u);
    EXPECT_EQ(ts.mem_bytes(), 0);
}

TEST(TempStoreTest, OverwriteMemoryToMemory) {
    fly::TempStore ts(1024);
    ts.put("k1", CMString(10, 'a'));
    EXPECT_EQ(ts.mem_bytes(), 10);
    ts.put("k1", CMString(20, 'b'));
    EXPECT_EQ(ts.mem_bytes(), 20);
    auto [found, result] = ts.get("k1");
    ASSERT_TRUE(found);
    EXPECT_EQ(result, CMString(20, 'b'));
}

TEST(TempStoreTest, OverwriteDiskToMemory) {
    fly::TempStore ts(50);
    ts.put("k1", CMString(100, 'a'));
    EXPECT_EQ(ts.mem_bytes(), 0);

    ts.put("k1", CMString(10, 'b'));
    EXPECT_EQ(ts.mem_bytes(), 10);
    auto [found, result] = ts.get("k1");
    ASSERT_TRUE(found);
    EXPECT_EQ(result.size(), 10u);
}

TEST(TempStoreTest, OverwriteMemoryToDisk) {
    fly::TempStore ts(50);
    ts.put("k1", CMString(10, 'a'));
    EXPECT_EQ(ts.mem_bytes(), 10);

    ts.put("k1", CMString(100, 'b'));
    EXPECT_EQ(ts.mem_bytes(), 0);
    auto [found, result] = ts.get("k1");
    ASSERT_TRUE(found);
    EXPECT_EQ(result.size(), 100u);
}

TEST(TempStoreTest, CleanupAllWithDiskEntries) {
    fly::TempStore ts(50);
    ts.put("mem", CMString(10, 'm'));
    ts.put("disk", CMString(100, 'd'));
    EXPECT_TRUE(ts.has("mem"));
    EXPECT_TRUE(ts.has("disk"));

    ts.cleanup_all();
    EXPECT_FALSE(ts.has("mem"));
    EXPECT_FALSE(ts.has("disk"));
    EXPECT_EQ(ts.mem_bytes(), 0);
}

TEST(TempStoreTest, GetNonexistentDiskEntry) {
    fly::TempStore ts(50);
    auto [found, result] = ts.get("never_put_here");
    EXPECT_FALSE(found);
}

TEST(TempStoreTest, HasReturnsFalseForNonexistent) {
    fly::TempStore ts(1024);
    EXPECT_FALSE(ts.has("no_such_key"));
}

TEST(TempStoreTest, MaxBytesAccessor) {
    fly::TempStore ts(2048);
    EXPECT_EQ(ts.max_bytes(), 2048);
}

TEST(TempStoreTest, PutEmptyString) {
    fly::TempStore ts(1024);
    ts.put("empty", "");
    auto [found, result] = ts.get("empty");
    ASSERT_TRUE(found);
    EXPECT_EQ(result.size(), 0u);
    EXPECT_EQ(ts.mem_bytes(), 0);
}

TEST(TempStoreTest, MultipleOverwritesTrackMemory) {
    fly::TempStore ts(1024);
    ts.put("k1", CMString(100, 'a'));
    EXPECT_EQ(ts.mem_bytes(), 100);
    ts.put("k1", CMString(200, 'b'));
    EXPECT_EQ(ts.mem_bytes(), 200);
    ts.put("k1", CMString(50, 'c'));
    EXPECT_EQ(ts.mem_bytes(), 50);
}

TEST(TempStoreTest, RemoveDiskEntry) {
    fly::TempStore ts(50);
    ts.put("disk_key", CMString(100, 'z'));
    EXPECT_TRUE(ts.has("disk_key"));

    ts.remove("disk_key");
    EXPECT_FALSE(ts.has("disk_key"));
    EXPECT_EQ(ts.mem_bytes(), 0);
}

} // namespace
