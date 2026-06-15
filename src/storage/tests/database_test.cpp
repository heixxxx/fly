#include <gtest/gtest.h>
#include <storage/cpp/database.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <common/cpp/worker_context.h>
#include <filesystem>
#include <fstream>
#include <istream>

namespace {

static CMString write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    return db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
}

static CMString read_raw_string(Database& db, const CMString& name, bool backup = false) {
    auto [comp_data, py_name] = db.read_object_compressed(name, backup);
    if (comp_data.empty()) return {};
    DecompressingStreamBuf dsbuf(comp_data.data(), comp_data.size());
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
    EXPECT_TRUE(write_raw(db, "test/obj2", "data2", false).empty());
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
    EXPECT_EQ(meta.db_id_, db.get_db_id());
    EXPECT_GT(meta.created_at_, 0);
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
    EXPECT_FALSE(comp_data.empty());
}

TEST_F(DatabaseTest, MultipleTypedObjects) {
    CMString base_path = test_dir_ + "/typed_multi";
    Database db(base_path);

    db.write_pickle_bytes("type/a", "data_a", 6, "TypeA");
    db.write_pickle_bytes("type/b", "data_b", 6, "TypeB");
    fly::DataService::instance()->drain_write_back();

    auto [comp_a, py_a] = db.read_object_compressed("type/a");
    EXPECT_EQ(py_a, "TypeA");
    EXPECT_FALSE(comp_a.empty());

    auto [comp_b, py_b] = db.read_object_compressed("type/b");
    EXPECT_EQ(py_b, "TypeB");
    EXPECT_FALSE(comp_b.empty());
}

TEST_F(DatabaseTest, CompressedNonexistentObjectReturnsEmpty) {
    CMString base_path = test_dir_ + "/compressed_nonexist";
    Database db(base_path);

    auto [comp_data, py_name] = db.read_object_compressed("no/such/object");
    EXPECT_TRUE(comp_data.empty());
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

TEST_F(DatabaseTest, DbIdIsBase62Format) {
    CMString base_path = test_dir_ + "/uuid_test";
    Database db(base_path);
    CMString db_id = db.get_db_id();
    // db_id: 4 path-hash + 6 random = 10 base62 chars
    EXPECT_EQ(db_id.size(), fly::db_id_len());
    for (char c : db_id) {
        EXPECT_TRUE((c >= '0' && c <= '9')
                    || (c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z'))
            << "non-base62 char in db_id: " << c;
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
    fly::WorkerAgentContext::set_record_write_func(
        [&recorded_writes](const CMString& db_id, const CMString& name) {
            recorded_writes.push_back(db_id + ":" + name);
        }
    );

    CMString base_path = test_dir_ + "/write_track";
    Database db(base_path);
    write_raw(db, "test/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(recorded_writes.size(), 1u);
    EXPECT_EQ(recorded_writes[0], db.get_db_id() + ":test/obj");
}

TEST_F(DatabaseTest, WriteTypedObjectTracksWrite) {
    CMVector<CMString> recorded_writes;
    fly::WorkerAgentContext::set_record_write_func(
        [&recorded_writes](const CMString& db_id, const CMString& name) {
            recorded_writes.push_back(db_id + ":" + name);
        }
    );

    CMString base_path = test_dir_ + "/typed_track";
    Database db(base_path);
    db.write_pickle_bytes("typed/obj", "typed_data", 10, "TestType");
    fly::DataService::instance()->drain_write_back();

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(recorded_writes.size(), 1u);
    EXPECT_EQ(recorded_writes[0], db.get_db_id() + ":typed/obj");
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
        [&remove_requests](const CMString& db_id, const CMString& name) {
            remove_requests.push_back(db_id + ":" + name);
        }
    );

    CMString base_path = test_dir_ + "/remove_trampoline";
    Database db(base_path);
    write_raw(db, "notify/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

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
    EXPECT_EQ(meta.db_id_, db.get_db_id());
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
    EXPECT_EQ(meta.db_id_, db.get_db_id());
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
    EXPECT_EQ(meta.db_id_, db.get_db_id());
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
    EXPECT_TRUE(write_raw(db, "after/freeze", "data2", false).empty());
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

    EXPECT_TRUE(write_raw(db, "after/freeze", "data2", false).empty());
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
    EXPECT_TRUE(comp_data.empty());
    EXPECT_TRUE(py_name.empty());
}

TEST_F(DatabaseTest, WriteAndReadViaReadObjectCompressed) {
    CMString base_path = test_dir_ + "/write_read_comp";
    Database db(base_path);

    write_raw(db, "comp/test", "compressed_payload", false);
    fly::DataService::instance()->drain_write_back();

    auto [comp_data, py_name] = db.read_object_compressed("comp/test");
    EXPECT_FALSE(comp_data.empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DatabaseTest, GetWriterIdIsNotEmpty) {
    CMString base_path = test_dir_ + "/writer_id";
    Database db(base_path);

    EXPECT_FALSE(db.get_writer_id().empty());
}

TEST_F(DatabaseTest, SetDbIdUpdatesRegistration) {
    CMString base_path = test_dir_ + "/set_dbid";
    Database db(base_path);

    CMString old_id = db.get_db_id();
    CMString new_id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    db.set_db_id(new_id);

    EXPECT_EQ(db.get_db_id(), new_id);
    EXPECT_NE(db.get_db_id(), old_id);
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
    EXPECT_TRUE(meta.db_id_.empty());
}

TEST_F(DatabaseTest, DatabaseWithExistingDbId) {
    CMString base_path = test_dir_ + "/existing_id";
    CMString existing_id = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    {
        Database db(base_path, "", 0, "", existing_id);
        EXPECT_EQ(db.get_db_id(), existing_id);
        EXPECT_EQ(db.get_base_path(), base_path);
    }
}

TEST_F(DatabaseTest, TempStorePutGetHasRemove) {
    CMString base_path = test_dir_ + "/temp_store";
    Database db(base_path);

    CMString compressed = "compressed_temp_data";
    db.put_temp("temp/obj", compressed);

    EXPECT_TRUE(db.has_temp("temp/obj"));

    auto [found, data] = db.get_temp("temp/obj");
    EXPECT_TRUE(found);
    EXPECT_EQ(data, compressed);

    db.remove_temp("temp/obj");
    EXPECT_FALSE(db.has_temp("temp/obj"));
}

TEST_F(DatabaseTest, TempGetReturnsFalseForMissing) {
    CMString base_path = test_dir_ + "/temp_missing";
    Database db(base_path);

    auto [found, data] = db.get_temp("missing/temp");
    EXPECT_FALSE(found);
}

TEST_F(DatabaseTest, TempHasReturnsFalseForMissing) {
    CMString base_path = test_dir_ + "/temp_has_missing";
    Database db(base_path);

    EXPECT_FALSE(db.has_temp("never/temp"));
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

    CMString result = write_raw(db, "empty/obj", "", false);
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
    EXPECT_EQ(meta.db_id_, db.get_db_id());
}

// db_id = <4-char path-hash prefix><6-char random suffix>.
// Same base_path -> same prefix (deterministic), different suffix (random).
// Different base_path -> different prefix with overwhelming probability.
TEST_F(DatabaseTest, DbIdPrefixIsPathDerived) {
    constexpr size_t kPrefixLen = 4;
    CMString path_a = test_dir_ + "/prefix_a";
    CMString path_b = test_dir_ + "/prefix_b";

    // Two dbs on the SAME path: identical prefix, different full id.
    Database db_a1(path_a);
    Database db_a2(path_a);
    CMString id_a1 = db_a1.get_db_id();
    CMString id_a2 = db_a2.get_db_id();
    EXPECT_EQ(id_a1.substr(0, kPrefixLen), id_a2.substr(0, kPrefixLen))
        << "same path must yield same prefix";
    EXPECT_NE(id_a1, id_a2)
        << "random suffix must differ between two dbs on the same path";

    // A db on a DIFFERENT path: prefix differs.
    Database db_b(path_b);
    CMString id_b = db_b.get_db_id();
    EXPECT_NE(id_a1.substr(0, kPrefixLen), id_b.substr(0, kPrefixLen))
        << "different paths should yield different prefixes";
}

}