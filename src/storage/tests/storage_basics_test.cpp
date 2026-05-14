#include <gtest/gtest.h>
#include <storage/cpp/object.h>
#include <storage/cpp/index_entry.h>
#include <storage/cpp/db_meta.h>
#include <serialization/cpp/serialization_macros.h>
#include <common/cpp/common_types.h>

// --- IndexEntry tests ---

TEST(IndexEntryTest, DefaultValues) {
    IndexEntry entry;
    EXPECT_EQ(entry.offset, 0);
    EXPECT_EQ(entry.size, 0);
    EXPECT_FALSE(entry.is_large);
    EXPECT_EQ(entry.block_count, 0);
    EXPECT_TRUE(entry.object_name.empty());
    EXPECT_TRUE(entry.file_name.empty());
}

TEST(IndexEntryTest, AggregateInit) {
    IndexEntry entry{"obj/test", "data_001.dat", 100, 200, false, 0};
    
    EXPECT_EQ(entry.object_name, "obj/test");
    EXPECT_EQ(entry.file_name, "data_001.dat");
    EXPECT_EQ(entry.offset, 100);
    EXPECT_EQ(entry.size, 200);
    EXPECT_FALSE(entry.is_large);
    EXPECT_EQ(entry.block_count, 0);
}

TEST(IndexEntryTest, LargeFileEntry) {
    IndexEntry entry{"large/file", "data.dat", 0, 1000000, true, 10};
    
    EXPECT_TRUE(entry.is_large);
    EXPECT_EQ(entry.block_count, 10);
}

TEST(IndexEntryTest, SerializeDeserialize) {
    IndexEntry entry{"obj/serialized", "data_002.dat", 500, 750, false, 0};
    
    CMString bytes;
    FLY_ENCODE(entry, bytes);
    EXPECT_GT(bytes.size(), 0);
    
    IndexEntry decoded;
    FLY_DECODE(bytes, IndexEntry, decoded);
    
    EXPECT_EQ(decoded.object_name, "obj/serialized");
    EXPECT_EQ(decoded.file_name, "data_002.dat");
    EXPECT_EQ(decoded.offset, 500);
    EXPECT_EQ(decoded.size, 750);
    EXPECT_FALSE(decoded.is_large);
    EXPECT_EQ(decoded.block_count, 0);
}

TEST(IndexEntryTest, SerializeDeserializeLargeFile) {
    IndexEntry entry{"big/file", "big.dat", 0, 999999, true, 42};
    
    CMString bytes;
    FLY_ENCODE(entry, bytes);
    
    IndexEntry decoded;
    FLY_DECODE(bytes, IndexEntry, decoded);
    
    EXPECT_EQ(decoded.object_name, "big/file");
    EXPECT_TRUE(decoded.is_large);
    EXPECT_EQ(decoded.block_count, 42);
}

// --- WorkerInfo tests ---

TEST(WorkerInfoTest, DefaultValues) {
    WorkerInfo info;
    EXPECT_EQ(info.worker_id, 0);
    EXPECT_EQ(info.idx_entry_count, 0);
    EXPECT_TRUE(info.host.empty());
    EXPECT_TRUE(info.role.empty());
}

TEST(WorkerInfoTest, AggregateInit) {
    WorkerInfo info{2, "192.168.1.10", "storage_only", "/ssd", "w2.idx", 500, "ssh ..."};
    
    EXPECT_EQ(info.worker_id, 2);
    EXPECT_EQ(info.host, "192.168.1.10");
    EXPECT_EQ(info.role, "storage_only");
    EXPECT_EQ(info.data_path, "/ssd");
    EXPECT_EQ(info.idx_file, "w2.idx");
    EXPECT_EQ(info.idx_entry_count, 500);
    EXPECT_EQ(info.launch_command, "ssh ...");
}

TEST(WorkerInfoTest, SerializeDeserialize) {
    WorkerInfo info{5, "10.0.0.1", "hybrid", "/disk", "w5.idx", 300, "cmd_run"};
    
    CMString bytes;
    FLY_ENCODE(info, bytes);
    EXPECT_GT(bytes.size(), 0);
    
    WorkerInfo decoded;
    FLY_DECODE(bytes, WorkerInfo, decoded);
    
    EXPECT_EQ(decoded.worker_id, 5);
    EXPECT_EQ(decoded.host, "10.0.0.1");
    EXPECT_EQ(decoded.role, "hybrid");
    EXPECT_EQ(decoded.data_path, "/disk");
    EXPECT_EQ(decoded.idx_file, "w5.idx");
    EXPECT_EQ(decoded.idx_entry_count, 300);
    EXPECT_EQ(decoded.launch_command, "cmd_run");
}

// --- DbMeta tests ---

TEST(DbMetaTest, DefaultValues) {
    DbMeta meta;
    EXPECT_EQ(meta.created_at, 0);
    EXPECT_EQ(meta.frozen_at, 0);
    EXPECT_TRUE(meta.db_id.empty());
    EXPECT_TRUE(meta.base_path.empty());
    EXPECT_TRUE(meta.workers.empty());
}

TEST(DbMetaTest, AggregateInit) {
    DbMeta meta{"/data/db1", "/data", 1715500000, 0, {}};
    
    EXPECT_EQ(meta.db_id, "/data/db1");
    EXPECT_EQ(meta.base_path, "/data");
    EXPECT_EQ(meta.created_at, 1715500000);
    EXPECT_EQ(meta.frozen_at, 0);
}

TEST(DbMetaTest, AddWorkerInfo) {
    DbMeta meta{"/data/db", "/data", 1715500000, 1715600000, {}};
    
    WorkerInfo worker{1, "localhost", "hybrid", "/local", "w1.idx", 100, "cmd"};
    meta.workers.push_back(worker);
    
    EXPECT_EQ(meta.workers.size(), 1);
    EXPECT_EQ(meta.workers[0].worker_id, 1);
    EXPECT_EQ(meta.workers[0].host, "localhost");
}

TEST(DbMetaTest, SerializeDeserialize) {
    DbMeta meta{"/db/test", "/base", 1715500000, 1715600000, {
        {1, "host1", "hybrid", "/d1", "w1.idx", 100, "cmd1"},
        {2, "host2", "storage_only", "/d2", "w2.idx", 200, "cmd2"}
    }};
    
    CMString bytes;
    FLY_ENCODE(meta, bytes);
    EXPECT_GT(bytes.size(), 0);
    
    DbMeta decoded;
    FLY_DECODE(bytes, DbMeta, decoded);
    
    EXPECT_EQ(decoded.db_id, "/db/test");
    EXPECT_EQ(decoded.base_path, "/base");
    EXPECT_EQ(decoded.created_at, 1715500000);
    EXPECT_EQ(decoded.frozen_at, 1715600000);
    EXPECT_EQ(decoded.workers.size(), 2);
    EXPECT_EQ(decoded.workers[0].worker_id, 1);
    EXPECT_EQ(decoded.workers[1].worker_id, 2);
}

// --- Object interface tests ---

struct TestData {
    int32_t value = 0;
    CMString name;
    FLY_SERIALIZE(value, name)
};

class MockObject : public Object {
public:
    TestData data;
    
    CMString type_name() const override { return "MockObject"; }
    
    CMString to_bytes() const override {
        CMString output;
        FLY_ENCODE(data, output);
        return output;
    }
    
    void from_bytes(const CMString& bytes) override {
        TestData result;
        FLY_DECODE(bytes, TestData, result);
        data = std::move(result);
    }
};

TEST(ObjectTest, CreateObject) {
    auto obj = make_object<MockObject>();
    EXPECT_NE(obj, nullptr);
    EXPECT_EQ(obj->data.value, 0);
    EXPECT_EQ(obj->data.name, "");
}

TEST(ObjectTest, SerializeDeserialize) {
    auto obj = make_object<MockObject>();
    obj->data.value = 42;
    obj->data.name = "test";
    
    CMString bytes = obj->to_bytes();
    EXPECT_GT(bytes.size(), 0);
    
    auto obj2 = make_object<MockObject>();
    obj2->from_bytes(bytes);
    
    EXPECT_EQ(obj2->data.value, 42);
    EXPECT_EQ(obj2->data.name, "test");
}

TEST(ObjectTest, TypeName) {
    auto obj = make_object<MockObject>();
    EXPECT_EQ(obj->type_name(), "MockObject");
}

TEST(ObjectTest, Size) {
    auto obj = make_object<MockObject>();
    obj->data.value = 100;
    obj->data.name = "hello";
    int64_t sz = obj->size();
    EXPECT_GT(sz, 0);
}