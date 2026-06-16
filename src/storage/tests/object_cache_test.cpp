#include <gtest/gtest.h>
#include <storage/cpp/object_cache.h>
#include <storage/cpp/database.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/serialization_macros.h>
#include <thread>
#include <chrono>
#include <filesystem>

namespace {

// Minimal serializable type for high-tier round-trip tests.
struct CacheItem {
    int32_t n = 0;
    CMString s;
    FLY_SERIALIZE(n, s)
};

}  // namespace

namespace fly {

// ---- high-tier: put/get with type restoration ----

TEST(ObjectCacheTest, HighTierPutGetRestoresType) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    c.put_high<int>("k1", CMMakeShared<int>(42), sizeof(int));
    auto got = c.get_high<int>("k1");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(*got, 42);
    c.clear();
}

TEST(ObjectCacheTest, HighTierMissReturnsNull) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    EXPECT_EQ(c.get_high<int>("nope"), nullptr);
    c.clear();
}

TEST(ObjectCacheTest, HighTierTypeMismatchReturnsNull) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    c.put_high<int>("k", CMMakeShared<int>(7), sizeof(int));
    // Stored as int, requested as double → type mismatch → nullptr.
    EXPECT_EQ(c.get_high<double>("k"), nullptr);
    c.clear();
}

TEST(ObjectCacheTest, HighTierHoldsSharedPtrLifetime) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    auto sp = CMMakeShared<std::string>("hello");
    std::weak_ptr<std::string> wp = sp;
    c.put_high<std::string>("k", std::move(sp), 16);
    sp.reset();
    // Cache still holds it.
    EXPECT_FALSE(wp.expired());

    {
        auto got = c.get_high<std::string>("k");
        ASSERT_NE(got, nullptr);
        EXPECT_EQ(*got, "hello");
        // got holds a strong ref; release before clear.
    }
    // Cache is the sole owner now.
    EXPECT_FALSE(wp.expired());
    c.clear();
    // After clear, the only strong ref is gone.
    EXPECT_TRUE(wp.expired());
}

// ---- low-tier: compressed bytes ----

TEST(ObjectCacheTest, LowTierPutGetReturnsBytes) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    c.put_low("k", "compressed_payload", 18);
    auto [hit, data] = c.get_low("k");
    EXPECT_TRUE(hit);
    EXPECT_EQ(data, "compressed_payload");
    c.clear();
}

TEST(ObjectCacheTest, LowTierMissReturnsFalse) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    auto [hit, data] = c.get_low("nope");
    EXPECT_FALSE(hit);
    EXPECT_TRUE(data.empty());
    c.clear();
}

// ---- remove / clear ----

TEST(ObjectCacheTest, RemoveClearsBothTiers) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    c.put_low("k", "low_data", 8);
    c.put_high<int>("k", CMMakeShared<int>(1), 4);
    EXPECT_EQ(c.low_size(), 1u);
    EXPECT_EQ(c.high_size(), 1u);

    c.remove("k");
    EXPECT_EQ(c.low_size(), 0u);
    EXPECT_EQ(c.high_size(), 0u);
    EXPECT_EQ(c.low_bytes(), 0u);
    EXPECT_EQ(c.high_bytes(), 0u);
}

TEST(ObjectCacheTest, RemoveUnknownKeyIsNoop) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    EXPECT_NO_THROW(c.remove("absent"));
}

// ---- eviction ----

TEST(ObjectCacheTest, EvictionTriggersOverMaxBytes) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(100);  // tiny limit
    // Each entry 40 bytes; 3 entries = 120 > 100 → must evict.
    c.put_low("a", CMString(40, 'a'), 40);
    // Force entries past the 30s protection window by sleeping is slow; instead
    // test the hard-limit path: keep adding until > 1.5x (=150) forces eviction
    // even within the protection window.
    c.put_low("b", CMString(40, 'b'), 40);
    c.put_low("c", CMString(40, 'c'), 40);
    c.put_low("d", CMString(40, 'd'), 40);  // total 160 > 150 hard limit
    // After eviction, total must be <= 100.
    EXPECT_LE(c.low_bytes(), 100u);
    c.clear();
}

TEST(ObjectCacheTest, ProtectionWindowPreventsEvictionUnderHardLimit) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(100);
    // 2 entries = 80 bytes. Over max (100? no, 80<100). Add one more = 120.
    // 120 < hard_limit (150) and within protection → no eviction.
    c.put_low("a", CMString(40, 'a'), 40);
    c.put_low("b", CMString(40, 'b'), 40);
    c.put_low("c", CMString(40, 'c'), 40);  // 120 > max but < hard_limit
    // All entries newly created (< 30s) and under hard limit → retained.
    EXPECT_EQ(c.low_size(), 3u);
    EXPECT_EQ(c.low_bytes(), 120u);
    c.clear();
}

TEST(ObjectCacheTest, EvictionRespectsScoreOrder) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(100);
    // This test relies on hard-limit eviction. Insert several, access one
    // repeatedly to boost its score, exceed hard limit, verify the low-score
    // entries are evicted preferentially.
    c.put_low("hot", CMString(40, 'h'), 40);
    c.put_low("cold1", CMString(40, 'c'), 40);
    // Boost "hot" score.
    for (int i = 0; i < 10; ++i) {
        c.get_low("hot");
    }
    // Now exceed hard limit (150): add more.
    c.put_low("cold2", CMString(40, 'c'), 40);  // 120
    c.put_low("cold3", CMString(40, 'c'), 40);  // 160 > 150
    // "hot" should survive (highest score); at least one cold evicted.
    auto [hot_hit, _] = c.get_low("hot");
    EXPECT_TRUE(hot_hit) << "high-score entry should survive eviction";
    EXPECT_LE(c.low_bytes(), 100u);
    c.clear();
}

// ---- hit statistics ----

TEST(ObjectCacheTest, LowTierHitMissPutCounters) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    const auto& s = c.stats();
    EXPECT_EQ(s.low_hits.load(), 0u);
    EXPECT_EQ(s.low_misses.load(), 0u);
    EXPECT_EQ(s.low_puts.load(), 0u);

    // Miss on empty cache.
    (void)c.get_low("absent");
    EXPECT_EQ(s.low_misses.load(), 1u);

    // Put then hit.
    c.put_low("k", "data", 4);
    EXPECT_EQ(s.low_puts.load(), 1u);
    auto [hit, _] = c.get_low("k");
    EXPECT_TRUE(hit);
    EXPECT_EQ(s.low_hits.load(), 1u);
    EXPECT_EQ(s.low_misses.load(), 1u);  // unchanged

    c.clear();
}

TEST(ObjectCacheTest, HighTierHitMissPutCounters) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    const auto& s = c.stats();

    // Miss on empty cache.
    EXPECT_EQ(c.get_high<int>("absent"), nullptr);
    EXPECT_EQ(s.high_misses.load(), 1u);

    // Put then hit.
    c.put_high<int>("k", CMMakeShared<int>(5), 4);
    EXPECT_EQ(s.high_puts.load(), 1u);
    EXPECT_NE(c.get_high<int>("k"), nullptr);
    EXPECT_EQ(s.high_hits.load(), 1u);

    // Type mismatch counts as miss.
    EXPECT_EQ(c.get_high<double>("k"), nullptr);
    EXPECT_EQ(s.high_misses.load(), 2u);

    c.clear();
}

TEST(ObjectCacheTest, EvictionCounter) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(100);  // tiny limit
    const auto& s = c.stats();

    // Each entry 40 bytes; exceed hard limit (150) to force eviction.
    c.put_low("a", CMString(40, 'a'), 40);
    c.put_low("b", CMString(40, 'b'), 40);
    c.put_low("c", CMString(40, 'c'), 40);
    c.put_low("d", CMString(40, 'd'), 40);  // 160 > 150 → evict
    EXPECT_GE(s.low_evictions.load(), 1u);
    c.clear();
}

TEST(ObjectCacheTest, ResetStatsZeroesCounters) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    c.put_low("k", "data", 4);
    (void)c.get_low("k");
    EXPECT_GT(c.stats().low_hits.load(), 0u);

    c.reset_stats();
    EXPECT_EQ(c.stats().low_hits.load(), 0u);
    EXPECT_EQ(c.stats().low_puts.load(), 0u);
    c.clear();
}

// ---- thread safety smoke test ----

TEST(ObjectCacheTest, ConcurrentAccessIsSafe) {
    ObjectCache& c = ObjectCache::instance();
    c.reset_for_test(1 << 20);
    CMVector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&c, t]() {
            for (int i = 0; i < 100; ++i) {
                CMString key = "k" + std::to_string(t);
                c.put_low(key, "data", 4);
                c.get_low(key);
                c.put_high<int>(key, CMMakeShared<int>(t), 4);
                c.get_high<int>(key);
                if (i % 10 == 0) c.remove(key);
            }
        });
    }
    for (auto& th : threads) th.join();
    // No crash/hang = pass.
    c.clear();
}

// ---- Database integration: low-tier populated by read_object_compressed ----

class ObjectCacheDbTest : public ::testing::Test {
protected:
    CMString test_dir_;
    void SetUp() override {
        test_dir_ = "/tmp/fly_test_ocdb_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
        ObjectCache::instance().clear();
    }
    void TearDown() override {
        ObjectCache::instance().clear();
        std::filesystem::remove_all(test_dir_);
    }
};

// After read_object_compressed, the compressed bytes must be in the cache's
// low tier. A second read of the same object hits the cache (no disk IO).
TEST_F(ObjectCacheDbTest, ReadObjectCompressedPopulatesLowTier) {
    Database db(test_dir_ + "/low");
    db.write_pickle_bytes("obj", "payload", 7, "bytes", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_obj_name("obj");
    EXPECT_EQ(ObjectCache::instance().low_size(), 0u);

    // First read — populates low tier.
    auto [comp1, py1] = db.read_object_compressed("obj", false);
    ASSERT_FALSE(comp1.empty());
    EXPECT_EQ(ObjectCache::instance().low_size(), 1u)
        << "read_object_compressed should populate low tier";

    // Low-tier content equals what was returned.
    auto [hit, cached] = ObjectCache::instance().get_low(full);
    EXPECT_TRUE(hit);
    EXPECT_EQ(cached, comp1);
}

TEST_F(ObjectCacheDbTest, ReadObjectCompressedMissDoesNotPopulate) {
    Database db(test_dir_ + "/miss");
    // Reading a non-existent object must not insert anything into the cache.
    auto [comp, py] = db.read_object_compressed("absent", false);
    EXPECT_TRUE(comp.empty());
    EXPECT_EQ(ObjectCache::instance().low_size(), 0u);
}

// ---- Database integration: high-tier populated by read_object<T> ----

// After read_object<T>, the deserialized object must be in the cache's high
// tier. A second read hits the cache and returns the same instance.
TEST_F(ObjectCacheDbTest, ReadObjectPopulatesHighTier) {
    Database db(test_dir_ + "/high");
    CMString full = db.get_obj_name("obj");

    CacheItem src;
    src.n = 99;
    src.s = "payload";
    db.write_object("obj", src, "CacheItem", false);
    fly::DataService::instance()->drain_write_back();

    EXPECT_EQ(ObjectCache::instance().high_size(), 0u);

    // First read deserializes + populates high tier.
    auto obj1 = db.read_object<CacheItem>("obj");
    ASSERT_NE(obj1, nullptr);
    EXPECT_EQ(obj1->n, 99);
    EXPECT_EQ(ObjectCache::instance().high_size(), 1u)
        << "read_object<T> should populate high tier";

    // High-tier hit returns the cached shared_ptr (same instance).
    auto obj2 = db.read_object<CacheItem>("obj");
    ASSERT_NE(obj2, nullptr);
    EXPECT_EQ(obj2->n, 99);
    EXPECT_EQ(obj2.get(), obj1.get())
        << "second read should return the cached instance (high-tier hit)";
}

// A second read of the same object with the same type returns the cached
// high-tier instance (verifies the cached pointer is genuinely reused, and
// that the cache does not re-deserialize on a hit).
TEST_F(ObjectCacheDbTest, ReadObjectSecondReadReusesCachedInstance) {
    Database db(test_dir_ + "/reuse");
    CacheItem src;
    src.n = 7;
    src.s = "abc";
    db.write_object("obj", src, "CacheItem", false);
    fly::DataService::instance()->drain_write_back();

    auto a = db.read_object<CacheItem>("obj");
    ASSERT_NE(a, nullptr);
    auto b = db.read_object<CacheItem>("obj");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a.get(), b.get()) << "second read should return the cached instance";
}

}  // namespace fly
