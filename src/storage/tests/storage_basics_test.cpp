#include <gtest/gtest.h>
#include <storage/cpp/object.h>
#include <storage/cpp/index_entry.h>
#include <storage/cpp/db_meta.h>
#include <serialization/cpp/serialization_macros.h>
#include <common/cpp/common_types.h>

// --- IndexEntry tests ---

TEST(IndexEntryTest, DefaultValues) {
    IndexEntry entry;
    EXPECT_EQ(entry.offset_, 0);
    EXPECT_EQ(entry.size_, 0);
    EXPECT_FALSE(entry.is_large_);
    EXPECT_EQ(entry.block_count_, 0);
    EXPECT_TRUE(entry.object_name_.empty());
    EXPECT_TRUE(entry.file_name_.empty());
}

TEST(IndexEntryTest, AggregateInit) {
    IndexEntry entry{"obj/test", "data_001.dat", 100, 200, false, 0};
    
    EXPECT_EQ(entry.object_name_, "obj/test");
    EXPECT_EQ(entry.file_name_, "data_001.dat");
    EXPECT_EQ(entry.offset_, 100);
    EXPECT_EQ(entry.size_, 200);
    EXPECT_FALSE(entry.is_large_);
    EXPECT_EQ(entry.block_count_, 0);
}

TEST(IndexEntryTest, LargeFileEntry) {
    IndexEntry entry{"large/file", "data.dat", 0, 1000000, true, 10};
    
    EXPECT_TRUE(entry.is_large_);
    EXPECT_EQ(entry.block_count_, 10);
}

TEST(IndexEntryTest, SerializeDeserialize) {
    IndexEntry entry{"obj/serialized", "data_002.dat", 500, 750, false, 0};
    
    CMString bytes;
    FLY_ENCODE(entry, bytes);
    EXPECT_GT(bytes.size(), 0);
    
    IndexEntry decoded;
    FLY_DECODE(bytes, IndexEntry, decoded);
    
    EXPECT_EQ(decoded.object_name_, "obj/serialized");
    EXPECT_EQ(decoded.file_name_, "data_002.dat");
    EXPECT_EQ(decoded.offset_, 500);
    EXPECT_EQ(decoded.size_, 750);
    EXPECT_FALSE(decoded.is_large_);
    EXPECT_EQ(decoded.block_count_, 0);
}

TEST(IndexEntryTest, SerializeDeserializeLargeFile) {
    IndexEntry entry{"big/file", "big.dat", 0, 999999, true, 42};
    
    CMString bytes;
    FLY_ENCODE(entry, bytes);
    
    IndexEntry decoded;
    FLY_DECODE(bytes, IndexEntry, decoded);
    
    EXPECT_EQ(decoded.object_name_, "big/file");
    EXPECT_TRUE(decoded.is_large_);
    EXPECT_EQ(decoded.block_count_, 42);
}

// --- WorkerInfo tests ---

TEST(WorkerInfoTest, DefaultValues) {
    WorkerInfo info;
    EXPECT_EQ(info.worker_id_, 0);
    EXPECT_TRUE(info.hostname_.empty());
    EXPECT_TRUE(info.ip_address_.empty());
    EXPECT_TRUE(info.launch_command_.empty());
}

TEST(WorkerInfoTest, AggregateInit) {
    WorkerInfo info{2, "w0000002", "gpu-node-1", "10.0.1.5", "ssh gpu-node-1 fly --worker ..."};
    
    EXPECT_EQ(info.worker_id_, 2);
    EXPECT_EQ(info.writer_id_, "w0000002");
    EXPECT_EQ(info.hostname_, "gpu-node-1");
    EXPECT_EQ(info.ip_address_, "10.0.1.5");
    EXPECT_EQ(info.launch_command_, "ssh gpu-node-1 fly --worker ...");
}

TEST(WorkerInfoTest, SerializeDeserialize) {
    WorkerInfo info{5, "w0000005", "node-3", "192.168.1.10", "bsub -n 1 fly --worker ..."};
    
    CMString bytes;
    FLY_ENCODE(info, bytes);
    EXPECT_GT(bytes.size(), 0);
    
    WorkerInfo decoded;
    FLY_DECODE(bytes, WorkerInfo, decoded);
    
    EXPECT_EQ(decoded.worker_id_, 5);
    EXPECT_EQ(decoded.writer_id_, "w0000005");
    EXPECT_EQ(decoded.hostname_, "node-3");
    EXPECT_EQ(decoded.ip_address_, "192.168.1.10");
    EXPECT_EQ(decoded.launch_command_, "bsub -n 1 fly --worker ...");
}

// --- DbMeta tests ---

TEST(DbMetaTest, DefaultValues) {
    DbMeta meta;
    EXPECT_EQ(meta.created_at_, 0);
    EXPECT_TRUE(meta.db_path_.empty());
    EXPECT_TRUE(meta.workers_.empty());
}

TEST(DbMetaTest, AggregateInit) {
    DbMeta meta{"/data/db1", 1715500000, {}};
    
    EXPECT_EQ(meta.db_path_, "/data/db1");
    EXPECT_EQ(meta.created_at_, 1715500000);
}

TEST(DbMetaTest, AddWorkerInfo) {
    DbMeta meta{"/data/db", 1715500000, {}};
    
    WorkerInfo worker{1, "w0000001", "localhost", "127.0.0.1", "cmd"};
    meta.workers_.push_back(worker);
    
    EXPECT_EQ(meta.workers_.size(), 1);
    EXPECT_EQ(meta.workers_[0].worker_id_, 1);
    EXPECT_EQ(meta.workers_[0].writer_id_, "w0000001");
    EXPECT_EQ(meta.workers_[0].hostname_, "localhost");
}

TEST(DbMetaTest, SerializeDeserialize) {
    DbMeta meta{"/db/test", 1715500000, {
        {1, "w0000001", "host1", "10.0.0.1", "cmd1"},
        {2, "w0000002", "host2", "10.0.0.2", "cmd2"}
    }};
    
    CMString bytes;
    FLY_ENCODE(meta, bytes);
    EXPECT_GT(bytes.size(), 0);
    
    DbMeta decoded;
    FLY_DECODE(bytes, DbMeta, decoded);
    
    EXPECT_EQ(decoded.db_path_, "/db/test");
    EXPECT_EQ(decoded.created_at_, 1715500000);
    EXPECT_EQ(decoded.workers_.size(), 2);
    EXPECT_EQ(decoded.workers_[0].worker_id_, 1);
    EXPECT_EQ(decoded.workers_[1].worker_id_, 2);
}

// --- DbMetaHeader tests ---

TEST(DbMetaHeaderTest, DefaultValues) {
    DbMetaHeader header;
    EXPECT_TRUE(header.db_path_.empty());
    EXPECT_EQ(header.created_at_, 0);
}

TEST(DbMetaHeaderTest, SerializeDeserialize) {
    DbMetaHeader header{"abc123", 1715500000};
    
    CMString bytes;
    FLY_ENCODE(header, bytes);
    EXPECT_GT(bytes.size(), 0);
    
    DbMetaHeader decoded;
    FLY_DECODE(bytes, DbMetaHeader, decoded);
    
    EXPECT_EQ(decoded.db_path_, "abc123");
    EXPECT_EQ(decoded.created_at_, 1715500000);
}

// --- Object interface tests ---

struct TestData {
    int32_t value = 0;
    CMString name;
    FLY_SERIALIZE(value, name)
};

class MockObject : public Object {
public:
    TestData data_;
    
    CMString type_name() const override { return "MockObject"; }
    
    CMString to_bytes() const override {
        CMString output;
        FLY_ENCODE(data_, output);
        return output;
    }
    
    void from_bytes(const CMString& bytes) override {
        TestData result;
        FLY_DECODE(bytes, TestData, result);
        data_ = std::move(result);
    }
};

TEST(ObjectTest, CreateObject) {
    auto obj = make_object<MockObject>();
    EXPECT_NE(obj, nullptr);
    EXPECT_EQ(obj->data_.value, 0);
    EXPECT_EQ(obj->data_.name, "");
}

TEST(ObjectTest, SerializeDeserialize) {
    auto obj = make_object<MockObject>();
    obj->data_.value = 42;
    obj->data_.name = "test";
    
    CMString bytes = obj->to_bytes();
    EXPECT_GT(bytes.size(), 0);
    
    auto obj2 = make_object<MockObject>();
    obj2->from_bytes(bytes);
    
    EXPECT_EQ(obj2->data_.value, 42);
    EXPECT_EQ(obj2->data_.name, "test");
}

TEST(ObjectTest, TypeName) {
    auto obj = make_object<MockObject>();
    EXPECT_EQ(obj->type_name(), "MockObject");
}

TEST(ObjectTest, Size) {
    auto obj = make_object<MockObject>();
    obj->data_.value = 100;
    obj->data_.name = "hello";
    int64_t sz = obj->size();
    EXPECT_GT(sz, 0);
}
