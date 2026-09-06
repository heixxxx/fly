#include <gtest/gtest.h>
#include <storage/cpp/index_entry.h>
#include <storage/cpp/local_index.h>
#include <common/runtime/cpp/error_types.h>
#include <common/runtime/cpp/write_context_hash.h>
#include <common/testing/cpp/test_helpers.h>
#include <filesystem>
#include <set>
#include <string>

namespace {

// === compute_write_context_hash tests ===

TEST(WriteContextHashTest, DeterministicForSameInput) {
    CMString task_name = "my_task";
    CMString task_module = "my_module";
    CMVector<CMString> args = {"arg1", "arg2"};
    CMVector<CMString> inputs = {"db1:input/a.csv", "db1:input/b.csv"};

    CMString hash1 = compute_write_context_hash(task_name, task_module, args, inputs);
    CMString hash2 = compute_write_context_hash(task_name, task_module, args, inputs);

    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash1.size(), 32u);
}

TEST(WriteContextHashTest, DifferentForDifferentTaskName) {
    CMVector<CMString> args = {"arg1"};
    CMVector<CMString> inputs = {"db:input"};

    CMString hash_a = compute_write_context_hash("task_a", "mod", args, inputs);
    CMString hash_b = compute_write_context_hash("task_b", "mod", args, inputs);

    EXPECT_NE(hash_a, hash_b);
}

TEST(WriteContextHashTest, DifferentForDifferentModule) {
    CMVector<CMString> args = {"arg1"};
    CMVector<CMString> inputs = {"db:input"};

    CMString hash_a = compute_write_context_hash("task", "mod_a", args, inputs);
    CMString hash_b = compute_write_context_hash("task", "mod_b", args, inputs);

    EXPECT_NE(hash_a, hash_b);
}

TEST(WriteContextHashTest, DifferentForDifferentArgs) {
    CMVector<CMString> inputs = {"db:input"};

    CMString hash1 = compute_write_context_hash("task", "mod", {"arg1"}, inputs);
    CMString hash2 = compute_write_context_hash("task", "mod", {"arg2"}, inputs);

    EXPECT_NE(hash1, hash2);
}

TEST(WriteContextHashTest, DifferentForDifferentInputs) {
    CMVector<CMString> args = {"arg1"};

    CMString hash1 = compute_write_context_hash("task", "mod", args, {"db:input_a"});
    CMString hash2 = compute_write_context_hash("task", "mod", args, {"db:input_b"});

    EXPECT_NE(hash1, hash2);
}

TEST(WriteContextHashTest, EmptyArgsAndInputs) {
    CMString hash = compute_write_context_hash("task", "mod", {}, {});
    EXPECT_EQ(hash.size(), 32u);
    EXPECT_FALSE(hash.empty());
}

TEST(WriteContextHashTest, HashIsHexString) {
    CMString hash = compute_write_context_hash("task", "mod", {"arg"}, {"db:input"});
    for (char c : hash) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Hash contains non-hex character: " << c;
    }
}

// === IndexEntry serialization tests ===

TEST(IndexEntryV2Test, SerializeDeserializeWithWriteContextHash) {
    IndexEntry entry;
    entry.object_name_ = "db_path:test_obj";
    entry.file_name_ = "data_abc_001.dat";
    entry.offset_ = 1024;
    entry.size_ = 512;
    entry.is_large_ = false;
    entry.block_count_ = 3;
    entry.host_ = "worker-1";
    entry.write_context_hash_ = "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6";

    CMString serialized;
    FLY_ENCODE(entry, serialized);
    EXPECT_FALSE(serialized.empty());

    IndexEntry loaded;
    FLY_DECODE(serialized, IndexEntry, loaded);

    EXPECT_EQ(loaded.object_name_, entry.object_name_);
    EXPECT_EQ(loaded.file_name_, entry.file_name_);
    EXPECT_EQ(loaded.offset_, entry.offset_);
    EXPECT_EQ(loaded.size_, entry.size_);
    EXPECT_EQ(loaded.is_large_, entry.is_large_);
    EXPECT_EQ(loaded.block_count_, entry.block_count_);
    EXPECT_EQ(loaded.host_, entry.host_);
    EXPECT_EQ(loaded.write_context_hash_, entry.write_context_hash_);
}

TEST(IndexEntryV2Test, EmptyWriteContextHashRoundtrip) {
    IndexEntry entry;
    entry.object_name_ = "test_obj";
    entry.file_name_ = "data.dat";
    entry.offset_ = 0;
    entry.size_ = 100;
    entry.write_context_hash_ = "";

    CMString serialized;
    FLY_ENCODE(entry, serialized);

    IndexEntry loaded;
    FLY_DECODE(serialized, IndexEntry, loaded);
    EXPECT_EQ(loaded.write_context_hash_, "");
}

TEST(IndexEntryV2Test, LocalIndexSaveLoadWithHash) {
    CMString test_dir = fly::test::qa_tmp_dir("fly_test_v2_idx");
    std::filesystem::create_directories(test_dir);
    CMString idx_path = test_dir + "/v2.idx";

    {
        LocalIndex index(idx_path);
        IndexEntry e;
        e.object_name_ = "hashed_obj";
        e.file_name_ = "data.dat";
        e.offset_ = 0;
        e.size_ = 100;
        e.write_context_hash_ = "abcdef0123456789abcdef0123456789";
        index.add_entry(e);
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();

    auto found = loaded.find_entry("hashed_obj");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->write_context_hash_, "abcdef0123456789abcdef0123456789");

    std::filesystem::remove_all(test_dir);
}

TEST(IndexEntryV2Test, MultipleEntriesWithDifferentHashes) {
    CMString test_dir = fly::test::qa_tmp_dir("fly_test_multi_hash");
    std::filesystem::create_directories(test_dir);
    CMString idx_path = test_dir + "/multi.idx";

    {
        LocalIndex index(idx_path);
        IndexEntry e1;
        e1.object_name_ = "obj_a";
        e1.file_name_ = "data.dat";
        e1.offset_ = 0;
        e1.size_ = 50;
        e1.write_context_hash_ = "hash_aaa01234567890123456789012";

        IndexEntry e2;
        e2.object_name_ = "obj_b";
        e2.file_name_ = "data.dat";
        e2.offset_ = 50;
        e2.size_ = 60;
        e2.write_context_hash_ = "hash_bbb01234567890123456789012";

        index.add_entry(e1);
        index.add_entry(e2);
        index.save();
    }

    LocalIndex loaded(idx_path);
    loaded.load();
    EXPECT_EQ(loaded.entry_count(), 2);

    auto a = loaded.find_entry("obj_a");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->write_context_hash_, "hash_aaa01234567890123456789012");

    auto b = loaded.find_entry("obj_b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->write_context_hash_, "hash_bbb01234567890123456789012");

    std::filesystem::remove_all(test_dir);
}

// === TaskErrorType extension ===

TEST(WriteContextHashTest, NewErrorTypeExists) {
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::WRITE_PROVENANCE_MISMATCH), 5);
}

// === make_timestamp_hash tests ===

TEST(TimestampHashTest, IsNonEmptyAndHex) {
    CMString hash = make_timestamp_hash();
    EXPECT_EQ(hash.size(), 32u);
    EXPECT_FALSE(hash.empty());
    for (char c : hash) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Hash contains non-hex character: " << c;
    }
}

TEST(TimestampHashTest, TwoCallsDiffer) {
    CMString hash1 = make_timestamp_hash();
    CMString hash2 = make_timestamp_hash();
    EXPECT_NE(hash1, hash2);
}

TEST(TimestampHashTest, ManyCallsAllUnique) {
    // counter 兜底保证：即使连续调用落同纳秒也必唯一。
    std::set<CMString> seen;
    for (int i = 0; i < 1000; ++i) {
        seen.insert(make_timestamp_hash());
    }
    EXPECT_EQ(seen.size(), 1000u);
}

}
