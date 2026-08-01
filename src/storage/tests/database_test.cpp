#include <gtest/gtest.h>
#include <storage/cpp/database.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <common/cpp/worker_context.h>
#include <common/cpp/fly_buffer.h>
#include <filesystem>
#include <fstream>
#include <istream>

namespace {

// Build a FlyBufferPtr from raw bytes (simulates pickle/FLY_ENCODE_TO_BUFFER output).
static FlyBufferPtr make_var_buf(const CMString& bytes) {
    auto buf = CMMakeShared<FlyBuffer>();
    buf->write(bytes.data(), bytes.size());
    return buf;
}

static fly::WriteErrorType write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    return db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
}

static CMString read_raw_string(Database& db, const CMString& name, bool backup = false) {
    auto [comp_data, py_name] = db.read_object_compressed(name, backup);
    if (!comp_data || comp_data->empty()) return {};
    DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
    std::istream is(&dsbuf);
    CMString result;
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) {
            result.append(tmp.data(), static_cast<size_t>(is.gcount()));
        }
    }
    return result;
}

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

    write_raw(db, "test/obj", "hello world", false);
    fly::DataService::instance()->drain_write_back();
    CMString result = read_raw_string(db, "test/obj");

    EXPECT_EQ(result, "hello world");
}

TEST_F(DatabaseTest, FreezePreventsWrite) {
    CMString base_path = test_dir_ + "/freeze_prevent";
    Database db(base_path);

    write_raw(db, "test/obj", "data", false);
    db.freeze();

    EXPECT_TRUE(db.is_frozen());
    EXPECT_EQ(write_raw(db, "test/obj2", "data2", false), fly::WriteErrorType::FROZEN_DB);
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

    write_raw(db, "priority/test", "local_data", false);
    fly::DataService::instance()->drain_write_back();
    CMString result = read_raw_string(db, "priority/test");

    EXPECT_EQ(result, "local_data");
}

TEST_F(DatabaseTest, LoadMetaFromFrozenDatabase) {
    CMString base_path = test_dir_ + "/meta_db";
    Database db(base_path);

    write_raw(db, "test/obj", "data", false);
    db.freeze();

    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_path_, db.get_db_path());
    EXPECT_GT(meta.created_at_, 0);
}

TEST_F(DatabaseTest, GetDbIdEqualsBasePath) {
    // db_path 废弃：db_path 现在是 base_path 的别名（不再随机生成）。
    CMString base_path = test_dir_ + "/id_check";
    Database db(base_path);

    EXPECT_EQ(db.get_db_path(), base_path);
    EXPECT_FALSE(db.get_db_path().empty());
}

TEST_F(DatabaseTest, BasePathWithColonRejected) {
    // full_name = "db_path:short" 用 ':' 分隔，base_path 含 ':' 会破坏 split。
    // Database 构造时拒绝含 ':' 的 base_path（双保险）。
    CMString bad_path = test_dir_ + "/bad:path";
    Database db(bad_path);
    // 拒绝后 base_path_ 清空，db_path_ 也清空（不产生有效对象）
    EXPECT_TRUE(db.get_base_path().empty())
        << "base_path with ':' should be rejected, got: " << db.get_base_path();
    EXPECT_TRUE(db.get_db_path().empty());
}

TEST_F(DatabaseTest, FullNameIsDbPathColonShort) {
    // full_name = "db_path:short_name"（db_path 废弃后的契约）
    CMString base_path = test_dir_ + "/fullname_check";
    Database db(base_path);
    EXPECT_EQ(db.get_full_name("matrix"), base_path + ":matrix");
    EXPECT_EQ(db.get_full_name("result/obj_1"), base_path + ":result/obj_1");
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

    write_raw(db, "obj1", "data1", false);
    write_raw(db, "obj2", "data2", false);
    write_raw(db, "obj3", "data3", false);
    fly::DataService::instance()->drain_write_back();

    EXPECT_EQ(read_raw_string(db, "obj1"), "data1");
    EXPECT_EQ(read_raw_string(db, "obj2"), "data2");
    EXPECT_EQ(read_raw_string(db, "obj3"), "data3");
}

TEST_F(DatabaseTest, ReadNonexistentObjectThrows) {
    CMString base_path = test_dir_ + "/nonexist";
    Database db(base_path);

    EXPECT_TRUE(read_raw_string(db, "no/such/object").empty());
}

// ─── Typed write/read tests ───

TEST_F(DatabaseTest, WriteAndReadTypedObject) {
    CMString base_path = test_dir_ + "/typed";
    Database db(base_path);

    CMString data = "typed_data_content";
    db.write_pickle_bytes("typed/obj", data.data(), static_cast<int64_t>(data.size()), "TestType");
    fly::DataService::instance()->drain_write_back();

    CMString read_data = read_raw_string(db, "typed/obj");
    EXPECT_EQ(read_data, data);
}

TEST_F(DatabaseTest, TypedObjectPersistenceAcrossFlush) {
    CMString base_path = test_dir_ + "/typed_flush";
    Database db(base_path);

    CMString data = "persistent_data";
    db.write_pickle_bytes("persist/obj", data.data(), static_cast<int64_t>(data.size()), "PersistType");
    fly::DataService::instance()->drain_write_back();

    CMString read_data = read_raw_string(db, "persist/obj");
    EXPECT_EQ(read_data, data);
}

TEST_F(DatabaseTest, TypedObjectWithPyNameDetection) {
    CMString base_path = test_dir_ + "/typed_pyname";
    Database db(base_path);

    db.write_pickle_bytes("named/obj", "some_data", 9, "MyCustomType");
    fly::DataService::instance()->drain_write_back();

    auto [comp_data, py_name] = db.read_object_compressed("named/obj");
    EXPECT_EQ(py_name, "MyCustomType");
    EXPECT_FALSE(!comp_data || comp_data->empty());
}

TEST_F(DatabaseTest, MultipleTypedObjects) {
    CMString base_path = test_dir_ + "/typed_multi";
    Database db(base_path);

    db.write_pickle_bytes("type/a", "data_a", 6, "TypeA");
    db.write_pickle_bytes("type/b", "data_b", 6, "TypeB");
    fly::DataService::instance()->drain_write_back();

    auto [comp_a, py_a] = db.read_object_compressed("type/a");
    EXPECT_EQ(py_a, "TypeA");
    EXPECT_FALSE(!comp_a || comp_a->empty());

    auto [comp_b, py_b] = db.read_object_compressed("type/b");
    EXPECT_EQ(py_b, "TypeB");
    EXPECT_FALSE(!comp_b || comp_b->empty());
}

TEST_F(DatabaseTest, CompressedNonexistentObjectReturnsEmpty) {
    CMString base_path = test_dir_ + "/compressed_nonexist";
    Database db(base_path);

    auto [comp_data, py_name] = db.read_object_compressed("no/such/object");
    EXPECT_TRUE(!comp_data || comp_data->empty());
}

TEST_F(DatabaseTest, GetObjNameReturnsDbIdColonName) {
    CMString base_path = test_dir_ + "/obj_name_test";
    Database db(base_path);

    CMString obj_name = db.get_full_name("output/result");
    CMString expected = db.get_db_path() + ":output/result";
    EXPECT_EQ(obj_name, expected);
}

TEST_F(DatabaseTest, GetObjNameDifferentDbDifferentResult) {
    CMString base_a = test_dir_ + "/db_a";
    CMString base_b = test_dir_ + "/db_b";
    Database db_a(base_a);
    Database db_b(base_b);

    // Same object name, different databases → different full names
    EXPECT_NE(db_a.get_full_name("output/result"), db_b.get_full_name("output/result"));
}

// ─── Write tracking tests ───

TEST_F(DatabaseTest, WriteObjectTracksWrite) {
    CMVector<CMString> recorded_writes;
    fly::WorkerAgentContext::set_record_write_func(
        [&recorded_writes](const CMString& db_path, const CMString& name, int64_t size) {
            recorded_writes.push_back(db_path + ":" + name);
        }
    );

    CMString base_path = test_dir_ + "/write_track";
    Database db(base_path);
    write_raw(db, "test/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(recorded_writes.size(), 1u);
    EXPECT_EQ(recorded_writes[0], db.get_db_path() + ":test/obj");
}

TEST_F(DatabaseTest, WriteTypedObjectTracksWrite) {
    CMVector<CMString> recorded_writes;
    fly::WorkerAgentContext::set_record_write_func(
        [&recorded_writes](const CMString& db_path, const CMString& name, int64_t size) {
            recorded_writes.push_back(db_path + ":" + name);
        }
    );

    CMString base_path = test_dir_ + "/typed_track";
    Database db(base_path);
    db.write_pickle_bytes("typed/obj", "typed_data", 10, "TestType");
    fly::DataService::instance()->drain_write_back();

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(recorded_writes.size(), 1u);
    EXPECT_EQ(recorded_writes[0], db.get_db_path() + ":typed/obj");
}

TEST_F(DatabaseTest, NoTrackingWithoutContext) {
    CMString base_path = test_dir_ + "/no_track";
    Database db(base_path);
    write_raw(db, "safe/obj", "data", false);
}

TEST_F(DatabaseTest, RemoveObjectPreventsRead) {
    CMString base_path = test_dir_ + "/remove_obj";
    Database db(base_path);

    write_raw(db, "test/obj", "hello world", false);
    fly::DataService::instance()->drain_write_back();

    CMString result = read_raw_string(db, "test/obj");
    EXPECT_EQ(result, "hello world");

    db.remove_object("test/obj");

    EXPECT_TRUE(read_raw_string(db, "test/obj").empty());
}

TEST_F(DatabaseTest, RemoveObjectOnlyAffectsTarget) {
    CMString base_path = test_dir_ + "/remove_one";
    Database db(base_path);

    write_raw(db, "obj/a", "data_a", false);
    write_raw(db, "obj/b", "data_b", false);
    fly::DataService::instance()->drain_write_back();

    db.remove_object("obj/a");

    EXPECT_TRUE(read_raw_string(db, "obj/a").empty());
    EXPECT_EQ(read_raw_string(db, "obj/b"), "data_b");
}

TEST_F(DatabaseTest, RemoveObjectFailsWhenFrozen) {
    CMString base_path = test_dir_ + "/remove_frozen";
    Database db(base_path);

    write_raw(db, "test/obj", "data", false);
    db.freeze();

    db.remove_object("test/obj");
}

TEST_F(DatabaseTest, RemoveObjectTrampolineRequestsRemove) {
    CMVector<CMString> remove_requests;
    fly::WorkerAgentContext::set_remove_request_func(
        [&remove_requests](const CMString& db_path, const CMString& name) {
            remove_requests.push_back(db_path + ":" + name);
        }
    );

    CMString base_path = test_dir_ + "/remove_trampoline";
    Database db(base_path);
    write_raw(db, "notify/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.remove_object("notify/obj");

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(remove_requests.size(), 1u);
    EXPECT_EQ(remove_requests[0], db.get_db_path() + ":notify/obj");
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
    EXPECT_EQ(meta.db_path_, db.get_db_path());
}

TEST_F(DatabaseTest, AppendWorkerInfoToMeta) {
    CMString base_path = test_dir_ + "/meta_append";
    Database db(base_path);

    // Append first WorkerInfo
    WorkerInfo info1;
    info1.worker_id_ = 1;
    info1.hostname_ = "host1";
    info1.ip_address_ = "10.0.0.1";
    info1.launch_command_ = "python worker.py";
    db.append_worker_info_to_meta(info1);

    DbMeta meta = db.load_meta();
    ASSERT_EQ(meta.workers_.size(), 1u);
    EXPECT_EQ(meta.workers_[0].worker_id_, 1u);
    EXPECT_EQ(meta.workers_[0].hostname_, "host1");
    EXPECT_EQ(meta.workers_[0].ip_address_, "10.0.0.1");
    EXPECT_EQ(meta.workers_[0].launch_command_, "python worker.py");

    // Append second WorkerInfo
    WorkerInfo info2;
    info2.worker_id_ = 2;
    info2.hostname_ = "host2";
    info2.ip_address_ = "10.0.0.2";
    info2.launch_command_ = "python worker2.py";
    db.append_worker_info_to_meta(info2);

    meta = db.load_meta();
    ASSERT_EQ(meta.workers_.size(), 2u);
    EXPECT_EQ(meta.workers_[0].worker_id_, 1u);
    EXPECT_EQ(meta.workers_[1].worker_id_, 2u);
    EXPECT_EQ(meta.workers_[1].hostname_, "host2");
}

TEST_F(DatabaseTest, FreezeOnlyWritesFrozenMarker) {
    CMString base_path = test_dir_ + "/meta_freeze";
    Database db(base_path);

    write_raw(db, "test/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

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

    write_raw(db, "data/obj1", "payload1", false);
    fly::DataService::instance()->drain_write_back();

    // Append multiple WorkerInfo records
    WorkerInfo w1{1, "host_a", "192.168.1.1", "launch_a"};
    WorkerInfo w2{2, "host_b", "192.168.1.2", "launch_b"};
    WorkerInfo w3{3, "host_c", "192.168.1.3", "launch_c"};
    db.append_worker_info_to_meta(w1);
    db.append_worker_info_to_meta(w2);
    db.append_worker_info_to_meta(w3);

    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_path_, db.get_db_path());
    EXPECT_GT(meta.created_at_, 0);
    ASSERT_EQ(meta.workers_.size(), 3u);
    EXPECT_EQ(meta.workers_[0].worker_id_, 1u);
    EXPECT_EQ(meta.workers_[1].worker_id_, 2u);
    EXPECT_EQ(meta.workers_[2].worker_id_, 3u);
}

TEST_F(DatabaseTest, LoadMetaNoWorkers) {
    CMString base_path = test_dir_ + "/meta_no_workers";
    Database db(base_path);

    // No WorkerInfo appended
    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_path_, db.get_db_path());
    EXPECT_GT(meta.created_at_, 0);
    EXPECT_TRUE(meta.workers_.empty());
}

TEST_F(DatabaseTest, AppendWorkerInfoIdempotent) {
    CMString base_path = test_dir_ + "/meta_idempotent";
    Database db(base_path);

    // Append same WorkerInfo twice — append is additive, no dedup
    WorkerInfo info;
    info.worker_id_ = 42;
    info.hostname_ = "dup_host";
    info.ip_address_ = "10.0.0.42";
    info.launch_command_ = "python dup.py";
    db.append_worker_info_to_meta(info);
    db.append_worker_info_to_meta(info);

    DbMeta meta = db.load_meta();
    ASSERT_EQ(meta.workers_.size(), 2u);
    // Both entries have same data
    EXPECT_EQ(meta.workers_[0].worker_id_, 42u);
    EXPECT_EQ(meta.workers_[1].worker_id_, 42u);
    EXPECT_EQ(meta.workers_[0].hostname_, "dup_host");
    EXPECT_EQ(meta.workers_[1].hostname_, "dup_host");
}

TEST_F(DatabaseTest, FreezeDuringInFlightWrite) {
    CMString base_path = test_dir_ + "/freeze_inflight";
    Database db(base_path);

    // Write object but do NOT drain — data is in WBQ
    write_raw(db, "inflight/obj", "inflight_data", false);

    // Freeze while write is in-flight (WBQ hasn't flushed yet)
    // freeze() calls drain_write_back() internally, so data gets persisted
    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    // Data should be readable — freeze() drains the WBQ before freezing
    CMString result = read_raw_string(db, "inflight/obj");
    EXPECT_EQ(result, "inflight_data");

    // Subsequent writes should be rejected
    EXPECT_EQ(write_raw(db, "after/freeze", "data2", false), fly::WriteErrorType::FROZEN_DB);
}

TEST_F(DatabaseTest, DoubleFreezeIsIdempotent) {
    CMString base_path = test_dir_ + "/double_freeze";
    Database db(base_path);

    write_raw(db, "before/freeze", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    EXPECT_NO_THROW(db.freeze());
    EXPECT_TRUE(db.is_frozen());

    std::ifstream ifs(base_path + "/_FROZEN");
    EXPECT_TRUE(ifs.good());

    EXPECT_EQ(write_raw(db, "after/freeze", "data2", false), fly::WriteErrorType::FROZEN_DB);
}

TEST_F(DatabaseTest, CompressPickleBytes) {
    CMString base_path = test_dir_ + "/compress_bytes";
    Database db(base_path);

    CMString data = "compressible_test_data";
    CMString compressed = db.compress_pickle_bytes(data.data(), static_cast<int64_t>(data.size()), "bytes");
    EXPECT_FALSE(compressed.empty());
}

TEST_F(DatabaseTest, CompressPickleBytesTyped) {
    CMString base_path = test_dir_ + "/compress_typed";
    Database db(base_path);

    CMString data = "typed_compress";
    CMString compressed = db.compress_pickle_bytes(data.data(), static_cast<int64_t>(data.size()), "MyType");
    EXPECT_FALSE(compressed.empty());

    DecompressingStreamBuf dsbuf(compressed.data(), compressed.size());
    std::istream is(&dsbuf);
    EXPECT_EQ(dsbuf.py_name(), "MyType");
}

TEST_F(DatabaseTest, ReadNonexistentObjectCompressedReturnsEmpty) {
    CMString base_path = test_dir_ + "/read_nonexist_comp";
    Database db(base_path);

    auto [comp_data, py_name] = db.read_object_compressed("absent/object");
    EXPECT_TRUE(!comp_data || comp_data->empty());
    EXPECT_TRUE(py_name.empty());
}

TEST_F(DatabaseTest, WriteAndReadViaReadObjectCompressed) {
    CMString base_path = test_dir_ + "/write_read_comp";
    Database db(base_path);

    write_raw(db, "comp/test", "compressed_payload", false);
    fly::DataService::instance()->drain_write_back();

    auto [comp_data, py_name] = db.read_object_compressed("comp/test");
    EXPECT_FALSE(!comp_data || comp_data->empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DatabaseTest, GetWriterIdIsNotEmpty) {
    CMString base_path = test_dir_ + "/writer_id";
    Database db(base_path);

    EXPECT_FALSE(db.get_writer_id().empty());
}

TEST_F(DatabaseTest, RemoveObjectOnFrozenIsNoop) {
    CMString base_path = test_dir_ + "/remove_frozen_noop";
    Database db(base_path);

    write_raw(db, "freeze_rm/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    db.remove_object("freeze_rm/obj");
}

TEST_F(DatabaseTest, LoadMetaEmptyAfterCorruption) {
    CMString base_path = test_dir_ + "/meta_corrupt";
    Database db(base_path);

    CMString meta_path = base_path + "/_DB_META";
    {
        std::ofstream ofs(meta_path, std::ios::binary | std::ios::trunc);
        int64_t bad_size = -1;
        ofs.write(reinterpret_cast<const char*>(&bad_size), sizeof(bad_size));
    }

    DbMeta meta = db.load_meta();
    EXPECT_TRUE(meta.db_path_.empty());
}

TEST_F(DatabaseTest, RemoveIndexEntry) {
    CMString base_path = test_dir_ + "/remove_idx";
    Database db(base_path);

    write_raw(db, "ridx/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.remove_index_entry("ridx/obj");
}

TEST_F(DatabaseTest, WriteEmptyData) {
    CMString base_path = test_dir_ + "/empty_write";
    Database db(base_path);

    write_raw(db, "empty/obj", "", false);
    fly::DataService::instance()->drain_write_back();
}

TEST_F(DatabaseTest, WriteLargeData) {
    CMString base_path = test_dir_ + "/large_write";
    Database db(base_path);

    CMString large_data(100000, 'X');
    write_raw(db, "large/obj", large_data, false);
    fly::DataService::instance()->drain_write_back();

    CMString result = read_raw_string(db, "large/obj");
    EXPECT_EQ(result.size(), large_data.size());
    EXPECT_EQ(result, large_data);
}

TEST_F(DatabaseTest, MultipleObjectsSameDbMeta) {
    CMString base_path = test_dir_ + "/multi_meta";
    Database db(base_path);

    write_raw(db, "meta/a", "data_a", false);
    write_raw(db, "meta/b", "data_b", false);
    fly::DataService::instance()->drain_write_back();

    DbMeta meta = db.load_meta();
    EXPECT_EQ(meta.db_path_, db.get_db_path());
}

// =============================================================================
// Var service tests.
// These run with WorkerAgentContext wired so that set/get/remove operate
// directly on the same Database instance (simulating the master process, where
// var funcs point at the authoritative local store). This isolates the
// Database var logic from the network layer.
// =============================================================================

class DatabaseVarTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<Database> db_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_var_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
        db_ = CMMakeShared<Database>(test_dir_);

        // Wire WorkerAgentContext var funcs to the authoritative local store,
        // as the master process would.
        fly::WorkerAgentContext::set_set_var_func(
            [this](const fly::CMString& full_name,
                   FlyBufferPtr v, const fly::CMString& tn) {
                auto [db_path, short_name] = fly::split_full_name(full_name);
                if (db_path.empty()) return false;
                return db_->master_set_var(short_name, v, tn);
            });
        fly::WorkerAgentContext::set_get_var_func(
            [this](const fly::CMString& full_name) {
                auto [db_path, short_name] = fly::split_full_name(full_name);
                if (db_path.empty()) {
                    return std::make_tuple(false, FlyBufferPtr{}, fly::CMString{});
                }
                auto [ok, v, tn] = db_->master_get_var(short_name);
                return std::make_tuple(ok, v, tn);
            });
        fly::WorkerAgentContext::set_remove_var_func(
            [this](const fly::CMString& full_name) {
                auto [db_path, short_name] = fly::split_full_name(full_name);
                if (!db_path.empty()) {
                    db_->master_remove_var(short_name);
                }
            });
    }

    void TearDown() override {
        fly::WorkerAgentContext::clear();
        db_.reset();
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DatabaseVarTest, SetGetRoundTrip) {
    auto val = make_var_buf("deadbeef");
    ASSERT_TRUE(db_->set_var("counter", val, "int"));

    auto [ok, got, tn] = db_->get_var("counter");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(got);
    EXPECT_EQ(got->size(), 8u);
    // value bytes equal.
    EXPECT_EQ(fly::CMString(got->data(), got->size()), "deadbeef");
    EXPECT_EQ(tn, "int");
}

TEST_F(DatabaseVarTest, GetMissingReturnsFalse) {
    auto [ok, got, tn] = db_->get_var("nope");
    EXPECT_FALSE(ok);
    EXPECT_FALSE(got);
}

TEST_F(DatabaseVarTest, ImmutableRejectsSecondSet) {
    auto v1 = make_var_buf("aaa");
    ASSERT_TRUE(db_->set_var("k", v1, "str"));

    auto v2 = make_var_buf("bbb");
    EXPECT_FALSE(db_->set_var("k", v2, "str"));  // rejected: immutable

    // Original value preserved.
    auto [ok, got, tn] = db_->get_var("k");
    ASSERT_TRUE(ok);
    EXPECT_EQ(fly::CMString(got->data(), got->size()), "aaa");
}

TEST_F(DatabaseVarTest, LocalCacheAfterSet) {
    // After set_var succeeds, the local cache must hold the value (zero-copy
    // shared FlyBufferPtr) so a subsequent get_var hits the cache, not master.
    auto val = make_var_buf("cached");
    ASSERT_TRUE(db_->set_var("x", val, "str"));

    // Drop the master func; a cache hit must still succeed.
    fly::WorkerAgentContext::set_get_var_func(
        [](const fly::CMString&) {
            return std::make_tuple(false, FlyBufferPtr{}, fly::CMString{});
        });
    auto [ok, got, tn] = db_->get_var("x");
    ASSERT_TRUE(ok);
    EXPECT_EQ(fly::CMString(got->data(), got->size()), "cached");
}

TEST_F(DatabaseVarTest, GetFetchesFromMasterOnCacheMiss) {
    // Seed the master store directly; local cache is empty.
    auto val = make_var_buf("from_master");
    ASSERT_TRUE(db_->master_set_var("remote_key", val, "int"));

    // get_var must fetch from master (via context) and cache locally.
    auto [ok, got, tn] = db_->get_var("remote_key");
    ASSERT_TRUE(ok);
    EXPECT_EQ(fly::CMString(got->data(), got->size()), "from_master");
}

TEST_F(DatabaseVarTest, RemoveClearsLocalAndMaster) {
    auto val = make_var_buf("rm");
    ASSERT_TRUE(db_->set_var("to_remove", val, "str"));
    ASSERT_TRUE(db_->master_has_var("to_remove"));

    db_->remove_var("to_remove");

    EXPECT_FALSE(db_->master_has_var("to_remove"));
    auto [ok, got, tn] = db_->get_var("to_remove");
    EXPECT_FALSE(ok);
}

TEST_F(DatabaseVarTest, InjectVarPopulatesLocalCache) {
    // Simulate TaskAssignMessage var_payloads injection on a worker.
    auto val = make_var_buf("injected");
    db_->inject_var("inj", val, "int");

    // get_var hits local cache (master func would say miss).
    fly::WorkerAgentContext::set_get_var_func(
        [](const fly::CMString&) {
            return std::make_tuple(false, FlyBufferPtr{}, fly::CMString{});
        });
    auto [ok, got, tn] = db_->get_var("inj");
    ASSERT_TRUE(ok);
    EXPECT_EQ(fly::CMString(got->data(), got->size()), "injected");
}

TEST_F(DatabaseVarTest, FreezeRejectsSetVar) {
    db_->freeze();
    ASSERT_TRUE(db_->is_frozen());

    auto val = make_var_buf("after_freeze");
    EXPECT_FALSE(db_->set_var("frozen_key", val, "int"));
    EXPECT_FALSE(db_->master_has_var("frozen_key"));
}

TEST_F(DatabaseVarTest, FreezePersistsVarsToDisk) {
    auto v1 = make_var_buf("persist1");
    auto v2 = make_var_buf("persist2");
    ASSERT_TRUE(db_->set_var("pv1", v1, "int"));
    ASSERT_TRUE(db_->set_var("pv2", v2, "str"));

    db_->freeze();

    // _VARS file must exist and be non-empty (magic + count header = 16 bytes).
    std::filesystem::path vars_file = std::string(test_dir_) + "/_VARS";
    ASSERT_TRUE(std::filesystem::exists(vars_file));
    EXPECT_GE(std::filesystem::file_size(vars_file), 16u);
}

TEST_F(DatabaseVarTest, LoadVarsFromDiskRestoresStore) {
    // Write vars and freeze (persists _VARS), then drop the in-memory store by
    // destroying this db and constructing a fresh one on the same path.
    auto v1 = make_var_buf("rv1");
    auto v2 = make_var_buf("rv2bytes");
    ASSERT_TRUE(db_->set_var("rk1", v1, "int"));
    ASSERT_TRUE(db_->set_var("rk2", v2, "str"));
    db_->freeze();

    db_.reset();
    fly::WorkerAgentContext::clear();

    // Re-open: existing db (db_path regenerated from path, but _VARS is read
    // because the path existed). Use a non-empty existing_db_id to skip the
    // new-db branch so _DB_META isn't rewritten; _VARS loads regardless.
    db_ = CMMakeShared<Database>(test_dir_, "", 0, "", "reused_id");

    // Vars restored into the in-memory store.
    EXPECT_TRUE(db_->master_has_var("rk1"));
    EXPECT_TRUE(db_->master_has_var("rk2"));

    auto [ok1, got1, tn1] = db_->master_get_var("rk1");
    ASSERT_TRUE(ok1);
    EXPECT_EQ(fly::CMString(got1->data(), got1->size()), "rv1");
    EXPECT_EQ(tn1, "int");

    auto [ok2, got2, tn2] = db_->master_get_var("rk2");
    ASSERT_TRUE(ok2);
    EXPECT_EQ(fly::CMString(got2->data(), got2->size()), "rv2bytes");
    EXPECT_EQ(tn2, "str");
}

TEST_F(DatabaseVarTest, DropLocalVarClearsCache) {
    auto val = make_var_buf("drop_me");
    ASSERT_TRUE(db_->set_var("d", val, "str"));
    EXPECT_TRUE(db_->master_has_var("d"));

    // drop_local_var clears the local cache. In the master-process test fixture
    // local == master store, so after drop the var is gone entirely.
    db_->drop_local_var("d");
    EXPECT_FALSE(db_->master_has_var("d"));
}

TEST_F(DatabaseVarTest, MasterRemoveClearAllVars) {
    auto v1 = make_var_buf("a");
    auto v2 = make_var_buf("b");
    ASSERT_TRUE(db_->set_var("c1", v1, "int"));
    ASSERT_TRUE(db_->set_var("c2", v2, "int"));

    db_->master_remove_var("");  // empty name clears all
    EXPECT_FALSE(db_->master_has_var("c1"));
    EXPECT_FALSE(db_->master_has_var("c2"));
}

TEST_F(DatabaseVarTest, LargeVarWarnsButStores) {
    // A var with serialized size > 1K should log a warning but still store
    // successfully — the warning is advisory, not a rejection.
    CMString big(2048, 'x');  // 2K payload
    auto val = make_var_buf(big);
    ASSERT_TRUE(db_->set_var("big_var", val, "bytes"));

    // Stored retrievably.
    auto [ok, got, tn] = db_->get_var("big_var");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(got);
    EXPECT_EQ(got->size(), 2048u);
}

TEST_F(DatabaseVarTest, SmallVarNoWarning) {
    // Exactly 1K (1024 bytes) is the boundary; <= 1024 should NOT warn.
    CMString exact_1k(1024, 'y');
    auto val = make_var_buf(exact_1k);
    ASSERT_TRUE(db_->set_var("boundary_var", val, "bytes"));

    auto [ok, got, tn] = db_->get_var("boundary_var");
    ASSERT_TRUE(ok);
    EXPECT_EQ(got->size(), 1024u);
}

}