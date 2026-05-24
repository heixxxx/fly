#include <gtest/gtest.h>
#include <storage/cpp/database.h>
#include <common/cpp/worker_context.h>
#include <filesystem>
#include <fstream>

namespace {

class DatabaseTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_db_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DatabaseTest, WriteAndReadObject) {
    CMString base_path = test_dir_ + "/write_read";
    Database db(base_path);

    db.write_object("test/obj", "hello world", false);
    fly::DataService::instance().drain_write_back();
    CMString result = db.read_object("test/obj");

    EXPECT_EQ(result, "hello world");
}

TEST_F(DatabaseTest, FreezePreventsWrite) {
    CMString base_path = test_dir_ + "/freeze_prevent";
    Database db(base_path);

    db.write_object("test/obj", "data", false);
    db.freeze();

    EXPECT_TRUE(db.is_frozen());
    EXPECT_THROW(db.write_object("test/obj2", "data2", false), std::runtime_error);
}

TEST_F(DatabaseTest, FrozenMarkerCreated) {
    CMString base_path = test_dir_ + "/frozen_marker";
    Database db(base_path);

    db.freeze();

    std::ifstream ifs(base_path + "/_FROZEN");
    EXPECT_TRUE(ifs.good());
}

TEST_F(DatabaseTest, DoublePathReadPriority) {
    CMString base_path = test_dir_ + "/shared_base";
    CMString data_path = test_dir_ + "/local_data";
    Database db(base_path, data_path);

    db.write_object("priority/test", "local_data", false);
    fly::DataService::instance().drain_write_back();
    CMString result = db.read_object("priority/test");

    EXPECT_EQ(result, "local_data");
}

TEST_F(DatabaseTest, LoadMetaFromFrozenDatabase) {
    CMString base_path = test_dir_ + "/meta_db";
    Database db(base_path);

    db.write_object("test/obj", "data", false);
    db.freeze();

    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_id, db.get_db_id());
    EXPECT_GT(meta.created_at, 0);
}

TEST_F(DatabaseTest, GetDbIdIsHashed) {
    CMString base_path = test_dir_ + "/id_check";
    Database db(base_path);

    EXPECT_NE(db.get_db_id(), base_path);
    EXPECT_FALSE(db.get_db_id().empty());
}

TEST_F(DatabaseTest, GetBasePath) {
    CMString base_path = test_dir_ + "/path_check";
    Database db(base_path);

    EXPECT_EQ(db.get_base_path(), base_path);
}

TEST_F(DatabaseTest, GetDataPath) {
    CMString base_path = test_dir_ + "/data_path_base";
    CMString data_path = test_dir_ + "/data_path_local";
    Database db(base_path, data_path);

    EXPECT_EQ(db.get_data_path(), data_path);
}

TEST_F(DatabaseTest, ResetClearsFrozenState) {
    CMString base_path = test_dir_ + "/reset_db";
    Database db(base_path);

    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    db.reset();
    EXPECT_FALSE(db.is_frozen());

    std::filesystem::path frozen_marker(base_path + "/_FROZEN");
    EXPECT_FALSE(std::filesystem::exists(frozen_marker));
}

TEST_F(DatabaseTest, WriteMultipleObjects) {
    CMString base_path = test_dir_ + "/multi_write";
    Database db(base_path);

    db.write_object("obj1", "data1", false);
    db.write_object("obj2", "data2", false);
    db.write_object("obj3", "data3", false);
    fly::DataService::instance().drain_write_back();

    EXPECT_EQ(db.read_object("obj1"), "data1");
    EXPECT_EQ(db.read_object("obj2"), "data2");
    EXPECT_EQ(db.read_object("obj3"), "data3");
}

TEST_F(DatabaseTest, ReadNonexistentObjectThrows) {
    CMString base_path = test_dir_ + "/nonexist";
    Database db(base_path);

    EXPECT_THROW(db.read_object("no/such/object"), std::runtime_error);
}

// ─── Typed write/read tests ───

TEST_F(DatabaseTest, WriteAndReadTypedObject) {
    CMString base_path = test_dir_ + "/typed";
    Database db(base_path);

    CMString data = "typed_data_content";
    db.write_object_typed("typed/obj", data, "TestType");
    fly::DataService::instance().drain_write_back();

    ReadResult read_result = db.read_object_typed("typed/obj");
    CMString read_data(read_result.data_buffer.begin(), read_result.data_buffer.end());
    EXPECT_EQ(read_data, data);
    EXPECT_EQ(read_result.py_name, "TestType");
}

TEST_F(DatabaseTest, TypedObjectPersistenceAcrossFlush) {
    CMString base_path = test_dir_ + "/typed_flush";
    Database db(base_path);

    CMString data = "persistent_data";
    db.write_object_typed("persist/obj", data, "PersistType");
    fly::DataService::instance().drain_write_back();

    ReadResult result = db.read_object_typed("persist/obj");
    CMString read_data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(read_data, data);
    EXPECT_EQ(result.py_name, "PersistType");
}

TEST_F(DatabaseTest, TypedObjectWithPyNameDetection) {
    CMString base_path = test_dir_ + "/typed_pyname";
    Database db(base_path);

    db.write_object_typed("named/obj", "some_data", "MyCustomType");
    fly::DataService::instance().drain_write_back();

    ReadResult result = db.read_object_typed("named/obj");
    EXPECT_EQ(result.py_name, "MyCustomType");
    CMString read_data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(read_data, "some_data");
}

TEST_F(DatabaseTest, MultipleTypedObjects) {
    CMString base_path = test_dir_ + "/typed_multi";
    Database db(base_path);

    db.write_object_typed("type/a", "data_a", "TypeA");
    db.write_object_typed("type/b", "data_b", "TypeB");
    fly::DataService::instance().drain_write_back();

    ReadResult ra = db.read_object_typed("type/a");
    EXPECT_EQ(ra.py_name, "TypeA");
    CMString da(ra.data_buffer.begin(), ra.data_buffer.end());
    EXPECT_EQ(da, "data_a");

    ReadResult rb = db.read_object_typed("type/b");
    EXPECT_EQ(rb.py_name, "TypeB");
    CMString db2(rb.data_buffer.begin(), rb.data_buffer.end());
    EXPECT_EQ(db2, "data_b");
}

TEST_F(DatabaseTest, TypedNonexistentObjectThrows) {
    CMString base_path = test_dir_ + "/typed_nonexist";
    Database db(base_path);

    EXPECT_THROW(db.read_object_typed("no/such/typed/object"), std::runtime_error);
}

TEST_F(DatabaseTest, GetObjNameReturnsDbIdColonName) {
    CMString base_path = test_dir_ + "/obj_name_test";
    Database db(base_path);

    CMString obj_name = db.get_obj_name("output/result");
    CMString expected = db.get_db_id() + ":output/result";
    EXPECT_EQ(obj_name, expected);
}

TEST_F(DatabaseTest, GetObjNameDifferentDbDifferentResult) {
    CMString base_a = test_dir_ + "/db_a";
    CMString base_b = test_dir_ + "/db_b";
    Database db_a(base_a);
    Database db_b(base_b);

    // Same object name, different databases → different full names
    EXPECT_NE(db_a.get_obj_name("output/result"), db_b.get_obj_name("output/result"));
}

TEST_F(DatabaseTest, DbIdIsUUIDFormat) {
    CMString base_path = test_dir_ + "/uuid_test";
    Database db(base_path);
    CMString db_id = db.get_db_id();
    // UUID v4: 32 hex chars
    EXPECT_EQ(db_id.size(), 32u);
    for (char c : db_id) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST_F(DatabaseTest, DbIdIsNotBasePath) {
    CMString base_path = test_dir_ + "/not_path";
    Database db(base_path);
    EXPECT_NE(db.get_db_id(), base_path);
}

// ─── Write tracking tests ───

TEST_F(DatabaseTest, WriteObjectTracksWrite) {
    CMVector<CMString> recorded_writes;
    fly::WorkerAgentContext::set(
        [](void* ctx, const CMString& db_id, const CMString& name) {
            auto* writes = static_cast<CMVector<CMString>*>(ctx);
            writes->push_back(db_id + ":" + name);
        },
        &recorded_writes
    );

    CMString base_path = test_dir_ + "/write_track";
    Database db(base_path);
    db.write_object("test/obj", "data", false);
    fly::DataService::instance().drain_write_back();

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(recorded_writes.size(), 1u);
    EXPECT_EQ(recorded_writes[0], db.get_db_id() + ":test/obj");
}

TEST_F(DatabaseTest, WriteTypedObjectTracksWrite) {
    CMVector<CMString> recorded_writes;
    fly::WorkerAgentContext::set(
        [](void* ctx, const CMString& db_id, const CMString& name) {
            auto* writes = static_cast<CMVector<CMString>*>(ctx);
            writes->push_back(db_id + ":" + name);
        },
        &recorded_writes
    );

    CMString base_path = test_dir_ + "/typed_track";
    Database db(base_path);
    db.write_object_typed("typed/obj", "typed_data", "TestType");
    fly::DataService::instance().drain_write_back();

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(recorded_writes.size(), 1u);
    EXPECT_EQ(recorded_writes[0], db.get_db_id() + ":typed/obj");
}

TEST_F(DatabaseTest, NoTrackingWithoutContext) {
    CMString base_path = test_dir_ + "/no_track";
    Database db(base_path);
    db.write_object("safe/obj", "data", false);
}

TEST_F(DatabaseTest, RemoveObjectPreventsRead) {
    CMString base_path = test_dir_ + "/remove_obj";
    Database db(base_path);

    db.write_object("test/obj", "hello world", false);
    fly::DataService::instance().drain_write_back();

    CMString result = db.read_object("test/obj");
    EXPECT_EQ(result, "hello world");

    db.remove_object("test/obj");

    EXPECT_THROW(db.read_object("test/obj"), std::runtime_error);
}

TEST_F(DatabaseTest, RemoveObjectOnlyAffectsTarget) {
    CMString base_path = test_dir_ + "/remove_one";
    Database db(base_path);

    db.write_object("obj/a", "data_a", false);
    db.write_object("obj/b", "data_b", false);
    fly::DataService::instance().drain_write_back();

    db.remove_object("obj/a");

    EXPECT_THROW(db.read_object("obj/a"), std::runtime_error);
    EXPECT_EQ(db.read_object("obj/b"), "data_b");
}

TEST_F(DatabaseTest, RemoveObjectFailsWhenFrozen) {
    CMString base_path = test_dir_ + "/remove_frozen";
    Database db(base_path);

    db.write_object("test/obj", "data", false);
    db.freeze();

    EXPECT_THROW(db.remove_object("test/obj"), std::runtime_error);
}

TEST_F(DatabaseTest, RemoveObjectTrampolineRequestsRemove) {
    CMVector<CMString> remove_requests;
    fly::WorkerAgentContext::set_remove_request_func(
        [](void* ctx, const CMString& db_id, const CMString& name) {
            auto* requests = static_cast<CMVector<CMString>*>(ctx);
            requests->push_back(db_id + ":" + name);
        },
        &remove_requests
    );

    CMString base_path = test_dir_ + "/remove_trampoline";
    Database db(base_path);
    db.write_object("notify/obj", "data", false);
    fly::DataService::instance().drain_write_back();

    db.remove_object("notify/obj");

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(remove_requests.size(), 1u);
    EXPECT_EQ(remove_requests[0], db.get_db_id() + ":notify/obj");
}

// ─── _DB_META incremental format tests ───

TEST_F(DatabaseTest, DbMetaHeaderWrittenOnConstruction) {
    CMString base_path = test_dir_ + "/meta_header";
    Database db(base_path);

    // _DB_META file should exist after construction
    std::filesystem::path meta_path(base_path + "/_DB_META");
    EXPECT_TRUE(std::filesystem::exists(meta_path));

    // File should be non-empty
    auto file_size = std::filesystem::file_size(meta_path);
    EXPECT_GT(file_size, 0u);

    // Load meta and verify header fields
    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_id, db.get_db_id());
}

TEST_F(DatabaseTest, AppendWorkerInfoToMeta) {
    CMString base_path = test_dir_ + "/meta_append";
    Database db(base_path);

    // Append first WorkerInfo
    WorkerInfo info1;
    info1.worker_id = 1;
    info1.hostname = "host1";
    info1.ip_address = "10.0.0.1";
    info1.launch_command = "python worker.py";
    db.append_worker_info_to_meta(info1);

    DbMeta meta = db.load_meta();
    ASSERT_EQ(meta.workers.size(), 1u);
    EXPECT_EQ(meta.workers[0].worker_id, 1u);
    EXPECT_EQ(meta.workers[0].hostname, "host1");
    EXPECT_EQ(meta.workers[0].ip_address, "10.0.0.1");
    EXPECT_EQ(meta.workers[0].launch_command, "python worker.py");

    // Append second WorkerInfo
    WorkerInfo info2;
    info2.worker_id = 2;
    info2.hostname = "host2";
    info2.ip_address = "10.0.0.2";
    info2.launch_command = "python worker2.py";
    db.append_worker_info_to_meta(info2);

    meta = db.load_meta();
    ASSERT_EQ(meta.workers.size(), 2u);
    EXPECT_EQ(meta.workers[0].worker_id, 1u);
    EXPECT_EQ(meta.workers[1].worker_id, 2u);
    EXPECT_EQ(meta.workers[1].hostname, "host2");
}

TEST_F(DatabaseTest, FreezeOnlyWritesFrozenMarker) {
    CMString base_path = test_dir_ + "/meta_freeze";
    Database db(base_path);

    db.write_object("test/obj", "data", false);
    fly::DataService::instance().drain_write_back();

    std::filesystem::path meta_path(base_path + "/_DB_META");
    auto meta_size_before = std::filesystem::file_size(meta_path);

    db.freeze();

    // _FROZEN should exist
    std::filesystem::path frozen_path(base_path + "/_FROZEN");
    EXPECT_TRUE(std::filesystem::exists(frozen_path));

    // _DB_META size should not change (no rewrite)
    auto meta_size_after = std::filesystem::file_size(meta_path);
    EXPECT_EQ(meta_size_before, meta_size_after);
}

TEST_F(DatabaseTest, LoadMetaReadsIncrementalFormat) {
    CMString base_path = test_dir_ + "/meta_incremental";
    Database db(base_path);

    db.write_object("data/obj1", "payload1", false);
    fly::DataService::instance().drain_write_back();

    // Append multiple WorkerInfo records
    WorkerInfo w1{1, "host_a", "192.168.1.1", "launch_a"};
    WorkerInfo w2{2, "host_b", "192.168.1.2", "launch_b"};
    WorkerInfo w3{3, "host_c", "192.168.1.3", "launch_c"};
    db.append_worker_info_to_meta(w1);
    db.append_worker_info_to_meta(w2);
    db.append_worker_info_to_meta(w3);

    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_id, db.get_db_id());
    EXPECT_GT(meta.created_at, 0);
    ASSERT_EQ(meta.workers.size(), 3u);
    EXPECT_EQ(meta.workers[0].worker_id, 1u);
    EXPECT_EQ(meta.workers[1].worker_id, 2u);
    EXPECT_EQ(meta.workers[2].worker_id, 3u);
}

TEST_F(DatabaseTest, LoadMetaNoWorkers) {
    CMString base_path = test_dir_ + "/meta_no_workers";
    Database db(base_path);

    // No WorkerInfo appended
    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_id, db.get_db_id());
    EXPECT_GT(meta.created_at, 0);
    EXPECT_TRUE(meta.workers.empty());
}

TEST_F(DatabaseTest, AppendWorkerInfoIdempotent) {
    CMString base_path = test_dir_ + "/meta_idempotent";
    Database db(base_path);

    // Append same WorkerInfo twice — append is additive, no dedup
    WorkerInfo info;
    info.worker_id = 42;
    info.hostname = "dup_host";
    info.ip_address = "10.0.0.42";
    info.launch_command = "python dup.py";
    db.append_worker_info_to_meta(info);
    db.append_worker_info_to_meta(info);

    DbMeta meta = db.load_meta();
    ASSERT_EQ(meta.workers.size(), 2u);
    // Both entries have same data
    EXPECT_EQ(meta.workers[0].worker_id, 42u);
    EXPECT_EQ(meta.workers[1].worker_id, 42u);
    EXPECT_EQ(meta.workers[0].hostname, "dup_host");
    EXPECT_EQ(meta.workers[1].hostname, "dup_host");
}

}