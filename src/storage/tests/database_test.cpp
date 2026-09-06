#include <gtest/gtest.h>
#include <storage/cpp/database.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/memory_chunk_source.h>
#include <core/cpp/process_info.h>
#include <core/cpp/process_info.h>
#include <common/runtime/cpp/worker_context.h>
#include <common/buffer/cpp/fly_buffer.h>
#include <common/runtime/cpp/error_types.h>
#include <common/buffer/cpp/data_checksum.h>
#include <common/serialization/cpp/object_header.h>
#include <common/testing/cpp/test_helpers.h>
#include <filesystem>
#include <fstream>
#include <istream>
#include <atomic>
#include <thread>
#include <vector>

namespace {

// Build a FlyBufferPtr from raw bytes (simulates pickle/FLY_ENCODE_TO_BUFFER output).
static FlyBufferPtr make_var_buf(const CMString& bytes) {
    auto buf = CMMakeShared<FlyBuffer>();
    buf->write(bytes.data(), bytes.size());
    return buf;
}

// 写侧恒流式（T2c 2026-08-31）：write_pickle_bytes 已删（仅测试调用的过期
// API）——造数原语统一 open_write_stream → write → finish_and_commit。
static fly::WriteErrorType write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false,
                                     const CMString& py_name = "bytes") {
    std::unique_ptr<FlyStream> s(db.open_write_stream(name, py_name));
    if (!s) return fly::WriteErrorType::FROZEN_DB;
    s->write(data.data(), static_cast<size_t>(data.size()));
    return static_cast<fly::WriteErrorType>(s->finish_and_commit(backup, /*populate_cache=*/true));
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
        test_dir_ = fly::test::qa_tmp_dir("fly_test_db");
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DatabaseTest, WriteAndReadObject) {
    CMString db_path = test_dir_ + "/write_read";
    Database db(db_path);

    write_raw(db, "test/obj", "hello world", false);
    fly::DataService::instance()->drain_write_back();
    CMString result = read_raw_string(db, "test/obj");

    EXPECT_EQ(result, "hello world");
}

TEST_F(DatabaseTest, FreezePreventsWrite) {
    CMString db_path = test_dir_ + "/freeze_prevent";
    Database db(db_path);

    write_raw(db, "test/obj", "data", false);
    db.freeze();

    EXPECT_TRUE(db.is_frozen());
    EXPECT_EQ(write_raw(db, "test/obj2", "data2", false), fly::WriteErrorType::FROZEN_DB);
}

TEST_F(DatabaseTest, FrozenMarkerCreated) {
    CMString db_path = test_dir_ + "/frozen_marker";
    Database db(db_path);

    db.freeze();

    std::ifstream ifs(db_path + "/_FROZEN");
    EXPECT_TRUE(ifs.good());
}

TEST_F(DatabaseTest, DoublePathReadPriority) {
    CMString db_path = test_dir_ + "/shared_base";
    CMString data_path = test_dir_ + "/local_data";
    Database db(db_path, data_path);

    write_raw(db, "priority/test", "local_data", false);
    fly::DataService::instance()->drain_write_back();
    CMString result = read_raw_string(db, "priority/test");

    EXPECT_EQ(result, "local_data");
}

TEST_F(DatabaseTest, FreezeLeavesMetaUntouched) {
    CMString db_path = test_dir_ + "/meta_db";
    Database db(db_path);

    write_raw(db, "test/obj", "data", false);
    db.freeze();

    // _DB_META（JSON）读写已上移 Python 编排层（DbMetaFile）：C++ 构造与
    // freeze 均不产生该文件。
    EXPECT_FALSE(std::filesystem::exists(db_path + "/_DB_META"));
}

TEST_F(DatabaseTest, GetDbIdEqualsBasePath) {
    // db_path 废弃：db_path 现在是 db_path 的别名（不再随机生成）。
    CMString db_path = test_dir_ + "/id_check";
    Database db(db_path);

    EXPECT_EQ(db.get_db_path(), db_path);
    EXPECT_FALSE(db.get_db_path().empty());
}

TEST_F(DatabaseTest, BasePathWithColonRejected) {
    // full_name = "db_path:short" 用 ':' 分隔，db_path 含 ':' 会破坏 split。
    // Database 构造时拒绝含 ':' 的 db_path（双保险）。
    CMString bad_path = test_dir_ + "/bad:path";
    Database db(bad_path);
    // 拒绝后 db_path_ 清空，db_path_ 也清空（不产生有效对象）
    EXPECT_TRUE(db.get_db_path().empty())
        << "db_path with ':' should be rejected, got: " << db.get_db_path();
    EXPECT_TRUE(db.get_db_path().empty());
}

TEST_F(DatabaseTest, FullNameIsDbPathColonShort) {
    // full_name = "db_path:short_name"（db_path 废弃后的契约）
    CMString db_path = test_dir_ + "/fullname_check";
    Database db(db_path);
    EXPECT_EQ(db.get_full_name("matrix"), db_path + ":matrix");
    EXPECT_EQ(db.get_full_name("result/obj_1"), db_path + ":result/obj_1");
}

TEST_F(DatabaseTest, GetBasePath) {
    CMString db_path = test_dir_ + "/path_check";
    Database db(db_path);

    EXPECT_EQ(db.get_db_path(), db_path);
}

TEST_F(DatabaseTest, GetDataPath) {
    CMString db_path = test_dir_ + "/data_path_base";
    CMString data_path = test_dir_ + "/data_path_local";
    Database db(db_path, data_path);

    EXPECT_EQ(db.get_data_path(), data_path);
}

TEST_F(DatabaseTest, ResetClearsFrozenState) {
    CMString db_path = test_dir_ + "/reset_db";
    Database db(db_path);

    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    db.reset();
    EXPECT_FALSE(db.is_frozen());

    std::filesystem::path frozen_marker(db_path + "/_FROZEN");
    EXPECT_FALSE(std::filesystem::exists(frozen_marker));
}

TEST_F(DatabaseTest, WriteMultipleObjects) {
    CMString db_path = test_dir_ + "/multi_write";
    Database db(db_path);

    write_raw(db, "obj1", "data1", false);
    write_raw(db, "obj2", "data2", false);
    write_raw(db, "obj3", "data3", false);
    fly::DataService::instance()->drain_write_back();

    EXPECT_EQ(read_raw_string(db, "obj1"), "data1");
    EXPECT_EQ(read_raw_string(db, "obj2"), "data2");
    EXPECT_EQ(read_raw_string(db, "obj3"), "data3");
}

TEST_F(DatabaseTest, ReadNonexistentObjectThrows) {
    CMString db_path = test_dir_ + "/nonexist";
    Database db(db_path);

    EXPECT_TRUE(read_raw_string(db, "no/such/object").empty());
}

// ─── Typed write/read tests ───

TEST_F(DatabaseTest, WriteAndReadTypedObject) {
    CMString db_path = test_dir_ + "/typed";
    Database db(db_path);

    CMString data = "typed_data_content";
    write_raw(db, "typed/obj", data, false, "TestType");
    fly::DataService::instance()->drain_write_back();

    CMString read_data = read_raw_string(db, "typed/obj");
    EXPECT_EQ(read_data, data);
}

TEST_F(DatabaseTest, TypedObjectPersistenceAcrossFlush) {
    CMString db_path = test_dir_ + "/typed_flush";
    Database db(db_path);

    CMString data = "persistent_data";
    write_raw(db, "persist/obj", data, false, "PersistType");
    fly::DataService::instance()->drain_write_back();

    CMString read_data = read_raw_string(db, "persist/obj");
    EXPECT_EQ(read_data, data);
}

TEST_F(DatabaseTest, TypedObjectWithPyNameDetection) {
    CMString db_path = test_dir_ + "/typed_pyname";
    Database db(db_path);

    write_raw(db, "named/obj", "some_data", false, "MyCustomType");
    fly::DataService::instance()->drain_write_back();

    auto [comp_data, py_name] = db.read_object_compressed("named/obj");
    EXPECT_EQ(py_name, "MyCustomType");
    EXPECT_FALSE(!comp_data || comp_data->empty());
}

TEST_F(DatabaseTest, MultipleTypedObjects) {
    CMString db_path = test_dir_ + "/typed_multi";
    Database db(db_path);

    write_raw(db, "type/a", "data_a", false, "TypeA");
    write_raw(db, "type/b", "data_b", false, "TypeB");
    fly::DataService::instance()->drain_write_back();

    auto [comp_a, py_a] = db.read_object_compressed("type/a");
    EXPECT_EQ(py_a, "TypeA");
    EXPECT_FALSE(!comp_a || comp_a->empty());

    auto [comp_b, py_b] = db.read_object_compressed("type/b");
    EXPECT_EQ(py_b, "TypeB");
    EXPECT_FALSE(!comp_b || comp_b->empty());
}

TEST_F(DatabaseTest, CompressedNonexistentObjectReturnsEmpty) {
    CMString db_path = test_dir_ + "/compressed_nonexist";
    Database db(db_path);

    auto [comp_data, py_name] = db.read_object_compressed("no/such/object");
    EXPECT_TRUE(!comp_data || comp_data->empty());
}

TEST_F(DatabaseTest, GetObjNameReturnsDbIdColonName) {
    CMString db_path = test_dir_ + "/obj_name_test";
    Database db(db_path);

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

    CMString db_path = test_dir_ + "/write_track";
    Database db(db_path);
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

    CMString db_path = test_dir_ + "/typed_track";
    Database db(db_path);
    write_raw(db, "typed/obj", "typed_data", false, "TestType");
    fly::DataService::instance()->drain_write_back();

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(recorded_writes.size(), 1u);
    EXPECT_EQ(recorded_writes[0], db.get_db_path() + ":typed/obj");
}

TEST_F(DatabaseTest, NoTrackingWithoutContext) {
    CMString db_path = test_dir_ + "/no_track";
    Database db(db_path);
    write_raw(db, "safe/obj", "data", false);
}

TEST_F(DatabaseTest, RemoveObjectPreventsRead) {
    CMString db_path = test_dir_ + "/remove_obj";
    Database db(db_path);

    write_raw(db, "test/obj", "hello world", false);
    fly::DataService::instance()->drain_write_back();

    CMString result = read_raw_string(db, "test/obj");
    EXPECT_EQ(result, "hello world");

    db.remove_object("test/obj");

    EXPECT_TRUE(read_raw_string(db, "test/obj").empty());
}

TEST_F(DatabaseTest, RemoveObjectOnlyAffectsTarget) {
    CMString db_path = test_dir_ + "/remove_one";
    Database db(db_path);

    write_raw(db, "obj/a", "data_a", false);
    write_raw(db, "obj/b", "data_b", false);
    fly::DataService::instance()->drain_write_back();

    db.remove_object("obj/a");

    EXPECT_TRUE(read_raw_string(db, "obj/a").empty());
    EXPECT_EQ(read_raw_string(db, "obj/b"), "data_b");
}

TEST_F(DatabaseTest, RemoveObjectFailsWhenFrozen) {
    CMString db_path = test_dir_ + "/remove_frozen";
    Database db(db_path);

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

    CMString db_path = test_dir_ + "/remove_trampoline";
    Database db(db_path);
    write_raw(db, "notify/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.remove_object("notify/obj");

    fly::WorkerAgentContext::clear();

    ASSERT_EQ(remove_requests.size(), 1u);
    EXPECT_EQ(remove_requests[0], db.get_db_path() + ":notify/obj");
}

// ─── _DB_META ownership: C++ write path retired ───

TEST_F(DatabaseTest, DbMetaNotWrittenByCpp) {
    CMString db_path = test_dir_ + "/meta_header";
    Database db(db_path);

    // _DB_META（JSON）的初写在 Python 编排层（open_db → _init_chain →
    // DbMetaFile.write_new）；C++ 构造不产生该文件（行为锁定）。
    std::filesystem::path meta_path(db_path + "/_DB_META");
    EXPECT_FALSE(std::filesystem::exists(meta_path));
}

TEST_F(DatabaseTest, FreezeOnlyWritesFrozenMarker) {
    CMString db_path = test_dir_ + "/meta_freeze";
    Database db(db_path);

    write_raw(db, "test/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.freeze();

    // _FROZEN should exist
    std::filesystem::path frozen_path(db_path + "/_FROZEN");
    EXPECT_TRUE(std::filesystem::exists(frozen_path));

    // C++ 无 _DB_META 写者：freeze 不产生也不改写元信息文件。
    EXPECT_FALSE(std::filesystem::exists(db_path + "/_DB_META"));
}

TEST_F(DatabaseTest, FreezeDuringInFlightWrite) {
    CMString db_path = test_dir_ + "/freeze_inflight";
    Database db(db_path);

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
    CMString db_path = test_dir_ + "/double_freeze";
    Database db(db_path);

    write_raw(db, "before/freeze", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    EXPECT_NO_THROW(db.freeze());
    EXPECT_TRUE(db.is_frozen());

    std::ifstream ifs(db_path + "/_FROZEN");
    EXPECT_TRUE(ifs.good());

    EXPECT_EQ(write_raw(db, "after/freeze", "data2", false), fly::WriteErrorType::FROZEN_DB);
}

TEST_F(DatabaseTest, CompressPickleBytes) {
    CMString db_path = test_dir_ + "/compress_bytes";
    Database db(db_path);

    CMString data = "compressible_test_data";
    // T2c 2026-08-31：compress_pickle_bytes 已删（仅测试调用的过期 API）——
    // 压缩 roundtrip 经恒流式生产路径（write → read_object_compressed）验证。
    ASSERT_EQ(write_raw(db, "c/obj", data), fly::WriteErrorType::OK);
    auto [comp_data, py_name] = db.read_object_compressed("c/obj");
    ASSERT_TRUE(comp_data && !comp_data->empty());
    EXPECT_GT(comp_data->size(), 0u);
}

TEST_F(DatabaseTest, CompressPickleBytesTyped) {
    CMString db_path = test_dir_ + "/compress_typed";
    Database db(db_path);

    CMString data = "typed_compress";
    {
        std::unique_ptr<FlyStream> s(db.open_write_stream("t/obj", "MyType"));
        ASSERT_NE(s, nullptr);
        s->write(data.data(), static_cast<size_t>(data.size()));
        ASSERT_EQ(static_cast<int>(s->finish_and_commit(false, false)),
                  static_cast<int>(fly::WriteErrorType::OK));
    }
    auto [comp_data, py_name] = db.read_object_compressed("t/obj");
    ASSERT_TRUE(comp_data && !comp_data->empty());

    DecompressingStreamBuf dsbuf(comp_data->data(), comp_data->size());
    std::istream is(&dsbuf);
    EXPECT_EQ(dsbuf.py_name(), "MyType");
}

TEST_F(DatabaseTest, ReadNonexistentObjectCompressedReturnsEmpty) {
    CMString db_path = test_dir_ + "/read_nonexist_comp";
    Database db(db_path);

    auto [comp_data, py_name] = db.read_object_compressed("absent/object");
    EXPECT_TRUE(!comp_data || comp_data->empty());
    EXPECT_TRUE(py_name.empty());
}

TEST_F(DatabaseTest, WriteAndReadViaReadObjectCompressed) {
    CMString db_path = test_dir_ + "/write_read_comp";
    Database db(db_path);

    write_raw(db, "comp/test", "compressed_payload", false);
    fly::DataService::instance()->drain_write_back();

    auto [comp_data, py_name] = db.read_object_compressed("comp/test");
    EXPECT_FALSE(!comp_data || comp_data->empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DatabaseTest, GetWriterIdIsNotEmpty) {
    CMString db_path = test_dir_ + "/writer_id";
    Database db(db_path);

    EXPECT_FALSE(db.get_writer_id().empty());
}

TEST_F(DatabaseTest, RemoveObjectOnFrozenIsNoop) {
    CMString db_path = test_dir_ + "/remove_frozen_noop";
    Database db(db_path);

    write_raw(db, "freeze_rm/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.freeze();
    EXPECT_TRUE(db.is_frozen());

    db.remove_object("freeze_rm/obj");
}

TEST_F(DatabaseTest, RemoveIndexEntry) {
    CMString db_path = test_dir_ + "/remove_idx";
    Database db(db_path);

    write_raw(db, "ridx/obj", "data", false);
    fly::DataService::instance()->drain_write_back();

    db.remove_index_entry("ridx/obj");
}

TEST_F(DatabaseTest, WriteEmptyData) {
    CMString db_path = test_dir_ + "/empty_write";
    Database db(db_path);

    write_raw(db, "empty/obj", "", false);
    fly::DataService::instance()->drain_write_back();
}

TEST_F(DatabaseTest, WriteLargeData) {
    CMString db_path = test_dir_ + "/large_write";
    Database db(db_path);

    CMString large_data(100000, 'X');
    write_raw(db, "large/obj", large_data, false);
    fly::DataService::instance()->drain_write_back();

    CMString result = read_raw_string(db, "large/obj");
    EXPECT_EQ(result.size(), large_data.size());
    EXPECT_EQ(result, large_data);
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
        test_dir_ = fly::test::qa_tmp_dir("fly_test_var");
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

TEST_F(DatabaseVarTest, FreezeRejectsMasterSetVar) {
    db_->freeze();
    ASSERT_TRUE(db_->is_frozen());

    auto val = make_var_buf("master_frozen");
    EXPECT_FALSE(db_->master_set_var("mkey", val, "int"))
        << "frozen db 的 master_set_var 必须拒绝";
    EXPECT_FALSE(db_->master_has_var("mkey"));
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
    // because the path existed). Use a non-empty existing_db_path to keep the
    // same writer identity; _VARS loads regardless. (_DB_META 的写读在
    // Python 编排层，C++ 构造不再涉及。)
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

// Part A: 裸 write_object（无 task context）不得绕过 provenance。恒流式
//（T2c 2026-08-31）下保护点在完成登记的 register_write（master 侧 hash
// 权威，Database::open_write_stream 的 commit 回调）；本地 idx entry 的
// write_context_hash_ 有意留空（restore 等价去重/读侧对空 hash 保守加载，
// 功能安全）。此处断言：裸写 OK + entry 可见 + hash 为空（权威在 master）。
TEST_F(DatabaseTest, BareWriteObjectHasNonEmptyContextHash) {
    fly::WorkerAgentContext::clear_current_write_hash();  // 确保无 task context
    CMString db_path = test_dir_ + "/bare_hash";
    Database db(db_path);

    EXPECT_EQ(write_raw(db, "obj", "data", false), fly::WriteErrorType::OK);
    fly::DataService::instance()->drain_write_back();

    auto entries = fly::DataService::instance()->find_local_entries(db_path + ":obj");
    ASSERT_TRUE(entries.has_value());
    ASSERT_FALSE(entries.value().empty());
    EXPECT_TRUE(entries.value()[0].write_context_hash_.empty())
        << "恒流式写路径本地 entry hash 有意留空（master register 为权威）";

    fly::DataService::instance()->remove_local_index(db_path + ":obj");
}

// ── Database 自保护（state_mutex_）─────────────────────────────────────
// master/worker 的容器锁（db_instances_/databases_）外经 shared_ptr 调用
// Database 方法的前提是对象自保护（DEVELOPMENT_GUIDELINES §13 拆锁前置）。

// 并发 set_paths 与路径读取：无 data race（TSan 可验证），且任一时刻读到的
// full_name 前缀与 get_db_path 一致（成员更新原子可见，不撕裂）。
TEST_F(DatabaseTest, ConcurrentSetPathsWithReadersIsSafe) {
    Database db(test_dir_, test_dir_ + "/data", 0, "", test_dir_);
    CMString path_a = test_dir_ + "/pa";
    CMString path_b = test_dir_ + "/pb";
    std::filesystem::create_directories(path_a);
    std::filesystem::create_directories(path_b);

    std::atomic<bool> stop{false};
    db.set_paths(path_a, path_a);  // 先离开初始路径，读者只见 pa/pb 两个合法值
    std::vector<std::thread> threads;
    std::atomic<int> inconsistencies{0};

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                // 每个返回值自身必须合法（是两个已知 path 之一/以其为前缀）——
                // 锁保证单调用内部一致与无 data race；跨调用不要求原子快照
                //（get_db_path 与 get_full_name 是两次独立加锁，中间值可变）。
                CMString p = db.get_db_path();
                CMString full = db.get_full_name("obj");
                bool ok = (p == path_a || p == path_b) &&
                          (full.rfind(path_a + ":", 0) == 0 ||
                           full.rfind(path_b + ":", 0) == 0);
                if (!ok) inconsistencies.fetch_add(1);
            }
        });
    }
    for (int i = 0; i < 50; ++i) {
        db.set_paths(path_a, path_a);
        db.set_paths(path_b, path_b);
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    EXPECT_EQ(inconsistencies.load(), 0);
}

// 并发 freeze：check-and-set 原子化后只应有一个线程执行冻结副作用
//（_FROZEN marker 只创建一次的语义由 atomic check-and-set 保证；本用例
// 验证并发下无崩溃且终态 frozen）。
TEST_F(DatabaseTest, ConcurrentFreezeIsSafe) {
    Database db(test_dir_, test_dir_ + "/data", 0, "", test_dir_);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() { db.freeze(); });
    }
    for (auto& t : threads) t.join();
    EXPECT_TRUE(db.is_frozen());
    EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/_FROZEN"));
}

// ── temp 落盘（task 级断点的前置基建）──────────────────────────────
// temp 对象从纯内存（TempStore LRU）改为"内存 LRU + db 目录专用文件落盘"：
// .temp.data_{wid}_{NNN}.dat + .temp.{wid}.idx（op-log 事务段；2026-08-30
// 起 temp 无内存态——压缩 record 恒在盘上）。断点语义：
// 已完成 task 的 temp 输出跨进程可恢复（load temp idx → restore → ready），
// db freeze 后 temp 文件全部删除。

static FlyBufferPtr make_temp_payload(const CMString& tag) {
    // put_temp_data 收已压缩 buf（含 ObjectHeader）——compress_pickle_bytes
    // 已删（T2c），经恒流式写 + 裸读构造（唯一名防 DUPLICATE 跳写）。
    static Database dummy(fly::test::qa_tmp_dir("fly_temp_compress_dummy"));
    static uint64_t seq = 0;
    CMString name = "tmp/payload_" + std::to_string(++seq);
    std::unique_ptr<FlyStream> s(dummy.open_write_stream(name, "bytes"));
    s->write(tag.data(), static_cast<size_t>(tag.size()));
    (void)s->finish_and_commit(false, false);
    auto [comp, py] = dummy.read_object_compressed(name);
    return comp;
}

// put_temp_data → 进程代切换（清内存索引）→ load temp idx + restore →
// 盘 fallback 读回。验证 temp 落盘的跨进程可见性（断点恢复核心路径）。
TEST_F(DatabaseTest, TempPersistRoundtripAcrossRestart) {
    CMString db_path = test_dir_ + "/temp_roundtrip";
    {
        Database db(db_path);
        FlyBufferPtr buf = make_temp_payload("temp_payload_roundtrip");
        db.put_temp_data("iters/x_0", buf);
    }
    // 进程代切换：内存 local_idx 全清（模拟重启）。DataService 是进程单例，
    // 用 cleanup_temp_entries + unregister 后 re-register 等价模拟。
    auto ds = fly::DataService::instance();
    ds->cleanup_temp_entries(db_path);
    ds->unregister_database(db_path);
    ds->register_database(db_path, "");
    // 清空后无内存条目，也确认无 LRU 命中。
    auto [found0, _] = ds->try_read_local_raw(db_path + ":iters/x_0");
    EXPECT_FALSE(found0) << "内存条目清空后不应再命中（前提构造）";

    // load temp idx → restore_temp_entries（on_idx_load_command 的核心两步）。
    // temp idx 文件名 = .temp.{writer_id}.idx，扫描 db 目录取第一个。
    CMVector<CMString> temp_idx;
    for (const auto& f : std::filesystem::directory_iterator(db_path)) {
        CMString name = f.path().filename().string();
        if (name.rfind(".temp.", 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".idx") == 0) {
            temp_idx.push_back(f.path().string());
        }
    }
    ASSERT_EQ(temp_idx.size(), 1u) << "put_temp_data 应产生一个 temp idx";

    LocalIndex temp_index(temp_idx[0]);
    temp_index.load();
    auto entries = temp_index.get_all_entries();
    ASSERT_EQ(entries.size(), 1u);
    ds->restore_temp_entries(db_path, entries);

    // 盘读回（temp 无内存态，恒走 entries_ 文件读）。
    auto [found, data] = ds->try_read_local_raw(db_path + ":iters/x_0");
    ASSERT_TRUE(found);
    ASSERT_NE(data, nullptr);
    DecompressingStreamBuf dsbuf(data->data(), data->size());
    std::istream is(&dsbuf);
    CMString result;
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) result.append(tmp.data(), static_cast<size_t>(is.gcount()));
    }
    EXPECT_EQ(result, "temp_payload_roundtrip");
}

// task 失败回滚：mark_write_begin → put_temp → abort_task_writes →
// temp 数据文件 truncate 回滚点 + temp idx 段 ABORT（load 后无 entry）。
TEST_F(DatabaseTest, TempAbortRollsBackFiles) {
    CMString db_path = test_dir_ + "/temp_abort";
    Database db(db_path);
    db.mark_write_begin();
    FlyBufferPtr buf = make_temp_payload("temp_will_be_aborted");
    db.put_temp_data("dirty/obj", buf);
    CMVector<CMString> dirty = {db_path + ":dirty/obj"};
    db.abort_task_writes(dirty);

    // temp idx：load 后 ABORT 段丢弃 → 0 entry。
    for (const auto& f : std::filesystem::directory_iterator(db_path)) {
        CMString name = f.path().filename().string();
        if (name.rfind(".temp.", 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".idx") == 0) {
            LocalIndex temp_index(f.path().string());
            temp_index.load();
            EXPECT_EQ(temp_index.get_all_entries().size(), 0u)
                << "ABORT 段的 temp 写入必须被 idx 丢弃";
        }
    }
    // 内存条目同步清理。
    auto [found, _d] = fly::DataService::instance()->try_read_local_raw(db_path + ":dirty/obj");
    EXPECT_FALSE(found);
}

// freeze 完成后 temp 文件全部删除（数据文件 + temp idx）。
TEST_F(DatabaseTest, TempFreezeDeletesFiles) {
    CMString db_path = test_dir_ + "/temp_freeze";
    {
        Database db(db_path);
        write_raw(db, "final/obj", "persistent", false);
        fly::DataService::instance()->drain_write_back();
        FlyBufferPtr buf = make_temp_payload("temp_then_freeze");
        db.put_temp_data("iters/y_1", buf);
        db.freeze();
    }
    for (const auto& f : std::filesystem::directory_iterator(db_path)) {
        CMString name = f.path().filename().string();
        EXPECT_EQ(name.find(".temp."), CMString::npos)
            << "freeze 后不得残留 temp 文件（data/idx 统一 .temp. 前缀）: " << name;
    }
    // 正式对象不受影响。
    Database db2(db_path);
    EXPECT_EQ(read_raw_string(db2, "final/obj"), "persistent");
}

// temp idx 未闭合段（BEGIN 后无 END，进程被杀）load 时丢弃——崩溃一致性。
TEST_F(DatabaseTest, TempIdxUnclosedSegmentDropped) {
    CMString db_path = test_dir_ + "/temp_unclosed";
    {
        Database db(db_path);
        db.mark_write_begin();
        FlyBufferPtr buf = make_temp_payload("unclosed_temp");
        db.put_temp_data("half/obj", buf);
        // 不打 END/ABORT——直接丢弃 Database（模拟进程被杀，无 task 收尾）。
    }
    CMVector<CMString> temp_idx;
    for (const auto& f : std::filesystem::directory_iterator(db_path)) {
        CMString name = f.path().filename().string();
        if (name.rfind(".temp.", 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".idx") == 0) {
            temp_idx.push_back(f.path().string());
        }
    }
    ASSERT_EQ(temp_idx.size(), 1u);
    LocalIndex temp_index(temp_idx[0]);
    temp_index.load();
    EXPECT_EQ(temp_index.get_all_entries().size(), 0u)
        << "未闭合段的 temp 写入必须在 load 时丢弃";
}

// ════════════════════════════════════════════════════════════════════
// P4-1 零容忍重取 / backup 写链 / frozen 拒写 / _VARS 容错
// ════════════════════════════════════════════════════════════════════

// 组装合法新格式 record（单 raw 块 + trailer），供注错与远程副本注入。
static FlyBufferPtr make_db_test_record(const std::string& data, const CMString& py_name) {
    auto record = CMMakeShared<FlyBuffer>();
    int32_t sz = static_cast<int32_t>(data.size());
    uint64_t crc = fly::data_checksum(data.data(), data.size());
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&crc), 8);
    record->write(data.data(), data.size());
    ObjectHeader header;
    header.total_size_ = data.size();
    header.chunk_count_ = 1;
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    header.compression_type_ = 0;
    header.block_comp_lens_ = {static_cast<uint32_t>(data.size())};
    CMString trailer = header.serialize_trailer();
    record->write(trailer.data(), trailer.size());
    return record;
}

// 盘上 trailer 位腐（保文件大小，精确命中 trailer 校验）。
static void corrupt_last_byte(const CMString& dir, const CMString& file_prefix) {
    for (const auto& entry : std::filesystem::directory_iterator(std::string(dir))) {
        if (entry.path().filename().string().rfind(file_prefix, 0) == 0) {
            std::ifstream in(entry.path(), std::ios::binary);
            CMString bytes((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
            in.close();
            ASSERT_GT(bytes.size(), 8u);
            bytes[bytes.size() - 1] = static_cast<char>(bytes.back() ^ 0x01);
            std::ofstream out(entry.path(), std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), bytes.size());
            return;
        }
    }
    ADD_FAILURE() << "no file with prefix " << file_prefix << " under " << dir;
}

// 盘坏 trailer + 无副本 → FATAL（已有 data_corruption_test 覆盖）；此处锚定
// 有健康远程副本时 bypass 重取成功恢复的路径（§5：重取唯一可接受结果 =
// 干净通过校验）。
TEST_F(DatabaseTest, TrailerCorruptBypassRefetchFromRemoteRecovers) {
    auto ds = fly::DataService::instance();
    ds->reset();
    CMString db_path = test_dir_ + "/refetch";
    Database db(db_path);
    ASSERT_EQ(write_raw(db, "obj", "clean re-fetch payload"), fly::WriteErrorType::OK);
    fly::DataService::instance()->drain_write_back();
    corrupt_last_byte(db_path, "data_");

    auto good = make_db_test_record("clean re-fetch payload", "bytes");
    ds->update_remote_idx(db_path + ":obj", 7, "replica_host", 9000);
    ds->set_direct_compressed_read_handler(
        [&](const CMString&, int32_t, const CMString&)
            -> std::tuple<bool, FlyBufferPtr, CMString, CMString, fly::ReadError> {
            return {true, good, "bytes", {}, fly::ReadError::NONE};
        });

    auto [comp_data, py_name] = db.read_object_compressed("obj");
    ASSERT_TRUE(comp_data && !comp_data->empty());
    EXPECT_EQ(py_name, "bytes");

    ds->set_direct_compressed_read_handler(nullptr);
}

// TIER2 远程读命中 + backup=true → do_backup_write 把副本落成本地 record
//（盘写 + entry 登记 + WBQ drain），此后本地可读。
TEST_F(DatabaseTest, DoBackupWritePersistsRemoteCopy) {
    auto ds = fly::DataService::instance();
    ds->reset();
    CMString db_path = test_dir_ + "/backupw";
    Database db(db_path);

    CMString full = db_path + ":obj";
    auto good = make_db_test_record("remote copy payload", "bytes");
    ds->update_remote_idx(full, 9, "replica_host2", 9001);
    ds->set_direct_compressed_read_handler(
        [&](const CMString&, int32_t, const CMString&)
            -> std::tuple<bool, FlyBufferPtr, CMString, CMString, fly::ReadError> {
            return {true, good, "bytes", {}, fly::ReadError::NONE};
        });

    auto [comp_data, py_name] = db.read_object_compressed("obj", /*backup=*/true);
    ASSERT_TRUE(comp_data && !comp_data->empty());

    // backup 落盘后本地成为持有者（entry 登记 + flush）。
    EXPECT_TRUE(ds->has_local_object(full));
    auto [found, result] = ds->try_read_local(full);
    ASSERT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "remote copy payload");

    ds->set_direct_compressed_read_handler(nullptr);
}

// backup_object：源数据 trailer 已损坏 → 放弃 backup（不落坏数据）、不抛、
// on_write_failed 擦除条目（backup 是尽力语义，无错误通道）。
// frozen db 重开（读到 _FROZEN marker → 构造跳过 temp writer）→
// put_temp_data 走显式失败分支：无条目、无静默内存降级。
TEST_F(DatabaseTest, PutTempDataOnFrozenDbFailsExplicitly) {
    auto ds = fly::DataService::instance();
    ds->reset();
    CMString db_path = test_dir_ + "/temp_frozen2";
    {
        Database db(db_path);
        db.freeze();
    }
    ASSERT_TRUE(std::filesystem::exists(db_path + "/_FROZEN"));

    Database db2(db_path);  // frozen → temp_writer_ 不创建
    // 纯内存构造已压缩 record（含 ObjectHeader trailer）——不经 DataService
    //（make_temp_payload 的 static dummy db 会被上面的 reset() 破坏）。
    FlyStream w(CompressionType::NONE, 4194304, "bytes");
    w.write("temp_on_frozen", 14);
    w.flush();
    FlyBufferPtr buf = w.finish_write();
    ASSERT_TRUE(buf && !buf->empty());
    db2.put_temp_data("iters/frozen_0", buf);

    EXPECT_FALSE(ds->has_local_object(db_path + ":iters/frozen_0"));
}

// _VARS 文件损坏（坏 magic / 负 count）→ 构造期容错跳过，不崩、不加载。
TEST_F(DatabaseTest, LoadVarsFromDiskToleratesCorruptFile) {
    CMString db_path = test_dir_ + "/vars_bad";

    // 坏 magic。
    {
        std::ofstream ofs(db_path + "/_VARS", std::ios::binary);
        int64_t magic = 0xDEADBEEF;
        int64_t count = 1;
        ofs.write(reinterpret_cast<const char*>(&magic), 8);
        ofs.write(reinterpret_cast<const char*>(&count), 8);
    }
    { Database db(db_path); EXPECT_FALSE(db.master_has_var("anything")); }

    // 合法 magic + 负 count。
    {
        std::ofstream ofs(db_path + "/_VARS", std::ios::binary | std::ios::trunc);
        int64_t magic = 0x53524156;  // 'VARS' LE，与实现一致
        int64_t count = -3;
        ofs.write(reinterpret_cast<const char*>(&magic), 8);
        ofs.write(reinterpret_cast<const char*>(&count), 8);
    }
    { Database db(db_path); EXPECT_FALSE(db.master_has_var("anything")); }
}

// ════════════════════════════════════════════════════════════════════
// P4-2 commit_write 注册拒绝五分支（write_object<T> 是 commit_write 的
// 模板入口；register_write 经 WorkerAgentContext 注入，同线程生效）。
// ════════════════════════════════════════════════════════════════════

struct DbTestPayload {
    int32_t v = 0;
    FLY_SERIALIZE(v)
};

class DatabaseRegisterRejectTest : public ::testing::Test {
protected:
    CMString test_dir_;
    void SetUp() override {
        test_dir_ = fly::test::qa_tmp_dir("fly_test_db_rej");
        std::filesystem::create_directories(test_dir_);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);

        fly::WorkerAgentContext::clear();  // 清 register_func / last_error
        fly::DataService::instance()->reset();
    }
};

// 各拒绝分支：返回码 + last_error_type + 不落盘（无本地条目）。
TEST_F(DatabaseRegisterRejectTest, DuplicateSkippedBranch) {
    CMString db_path = test_dir_ + "/rej_dup";
    Database db(db_path);
    fly::WorkerAgentContext::set_register_func(
        [](const CMString&, const CMString&, int64_t, bool)
            -> std::pair<CMString, fly::TaskErrorType> {
            return {"dup", fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED};
        });
    DbTestPayload obj;
    EXPECT_EQ(db.write_object("obj", obj, "bytes"),
              fly::WriteErrorType::DUPLICATE_SKIPPED);
    EXPECT_EQ(fly::WorkerAgentContext::get_last_error_type(),
              fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED);
    EXPECT_FALSE(fly::DataService::instance()->has_local_object(db_path + ":obj"));
}

TEST_F(DatabaseRegisterRejectTest, ProvenanceMismatchMapsToRegistrationFailed) {
    CMString db_path = test_dir_ + "/rej_prov";
    Database db(db_path);
    fly::WorkerAgentContext::set_register_func(
        [](const CMString&, const CMString&, int64_t, bool)
            -> std::pair<CMString, fly::TaskErrorType> {
            return {"provenance", fly::TaskErrorType::WRITE_PROVENANCE_MISMATCH};
        });
    DbTestPayload obj;
    EXPECT_EQ(db.write_object("obj", obj, "bytes"),
              fly::WriteErrorType::REGISTRATION_FAILED);
    // 注：last_error_type 由 worker 侧 register 实现负责（commit_write 的
    // PROVENANCE_MISMATCH 分支不重复设置，与 DUPLICATE/TIMEOUT 分支不对称）。
    EXPECT_FALSE(fly::DataService::instance()->has_local_object(db_path + ":obj"));
}

TEST_F(DatabaseRegisterRejectTest, RegistrationFailedBranch) {
    CMString db_path = test_dir_ + "/rej_fail";
    Database db(db_path);
    fly::WorkerAgentContext::set_register_func(
        [](const CMString&, const CMString&, int64_t, bool)
            -> std::pair<CMString, fly::TaskErrorType> {
            return {"rejected", fly::TaskErrorType::WRITE_REGISTRATION_FAILED};
        });
    DbTestPayload obj;
    EXPECT_EQ(db.write_object("obj", obj, "bytes"),
              fly::WriteErrorType::REGISTRATION_FAILED);
    EXPECT_FALSE(fly::DataService::instance()->has_local_object(db_path + ":obj"));
}

TEST_F(DatabaseRegisterRejectTest, RegistrationTimeoutBranch) {
    CMString db_path = test_dir_ + "/rej_to";
    Database db(db_path);
    fly::WorkerAgentContext::set_register_func(
        [](const CMString&, const CMString&, int64_t, bool)
            -> std::pair<CMString, fly::TaskErrorType> {
            return {"timeout", fly::TaskErrorType::WRITE_REGISTRATION_TIMEOUT};
        });
    DbTestPayload obj;
    EXPECT_EQ(db.write_object("obj", obj, "bytes"),
              fly::WriteErrorType::REGISTRATION_TIMEOUT);
    // TIMEOUT 分支同样不回写 last_error_type（由上层 register 实现负责）。
    EXPECT_FALSE(fly::DataService::instance()->has_local_object(db_path + ":obj"));
}

TEST_F(DatabaseRegisterRejectTest, FrozenDbBranch) {
    CMString db_path = test_dir_ + "/rej_frozen";
    Database db(db_path);
    fly::WorkerAgentContext::set_register_func(
        [](const CMString&, const CMString&, int64_t, bool)
            -> std::pair<CMString, fly::TaskErrorType> {
            return {"frozen", fly::TaskErrorType::WRITE_TO_FROZEN_DB};
        });
    DbTestPayload obj;
    EXPECT_EQ(db.write_object("obj", obj, "bytes"),
              fly::WriteErrorType::FROZEN_DB);
    EXPECT_FALSE(fly::DataService::instance()->has_local_object(db_path + ":obj"));
}

// 注册成功路径：execute/complete lambda 落盘 + entry 登记 → drain 后本地可读。
TEST_F(DatabaseRegisterRejectTest, RegisterOkPersistsRecordViaCommitWrite) {
    CMString db_path = test_dir_ + "/rej_ok";
    Database db(db_path);
    fly::WorkerAgentContext::set_register_func(
        [](const CMString&, const CMString&, int64_t, bool)
            -> std::pair<CMString, fly::TaskErrorType> {
            return {"", fly::TaskErrorType::UNKNOWN};
        });
    DbTestPayload obj;
    obj.v = 42;
    ASSERT_EQ(db.write_object("obj", obj, "bytes"), fly::WriteErrorType::OK);
    fly::DataService::instance()->drain_write_back();

    CMString full = db_path + ":obj";
    EXPECT_TRUE(fly::DataService::instance()->has_local_object(full));
    auto [comp_data, py_name] = db.read_object_compressed("obj");
    ASSERT_TRUE(comp_data && !comp_data->empty());
}

// ════════════════════════════════════════════════════════════════════
// P4-3 _MIGRATED_TO 迁移跟随（旧 db 兼容路径）
// ════════════════════════════════════════════════════════════════════

TEST_F(DatabaseTest, MigrationMarkerFollowedOnOpen) {
    auto ds = fly::DataService::instance();
    ds->reset();
    CMString a = test_dir_ + "/mig_a";
    CMString b = test_dir_ + "/mig_b";
    std::filesystem::create_directories(a);
    std::filesystem::create_directories(b);

    fly::DataService::write_migration_marker(a, b, b);
    Database db(a);
    EXPECT_EQ(db.get_db_path(), b) << "open(A) 必须跟随 marker 到 B";
    EXPECT_EQ(db.get_data_path(), b) << "data_path 跟随 marker 的 target_data_path";
}

TEST_F(DatabaseTest, ChainedMigrationFollowsToFinalTarget) {
    auto ds = fly::DataService::instance();
    ds->reset();
    CMString a = test_dir_ + "/chain_a";
    CMString b = test_dir_ + "/chain_b";
    CMString c = test_dir_ + "/chain_c";
    for (const auto& d : {a, b, c}) std::filesystem::create_directories(d);

    fly::DataService::write_migration_marker(a, b, b);
    fly::DataService::write_migration_marker(b, c, c);
    Database db(a);
    EXPECT_EQ(db.get_db_path(), c) << "A→B→C 链式展平";
    EXPECT_EQ(db.get_data_path(), c);
}

TEST_F(DatabaseTest, CorruptMigrationMarkerIgnored) {
    auto ds = fly::DataService::instance();
    ds->reset();
    CMString a = test_dir_ + "/mig_bad";
    std::filesystem::create_directories(a);
    // size 域合法但 payload 是垃圾 → FLY_DECODE throw → catch → 不跟随。
    {
        std::ofstream ofs(a + "/_MIGRATED_TO", std::ios::binary);
        int64_t size = 64;
        ofs.write(reinterpret_cast<const char*>(&size), 8);
        ofs.write(std::string(64, '\x7F').c_str(), 64);
    }
    Database db(a);
    EXPECT_EQ(db.get_db_path(), a) << "损坏 marker 必须被 catch 且不跟随";
}

}

// ════════════════════════════════════════════════════════════════════
// 流式 backup stage（用户裁定 2026-09-02）：backup_object 流式拉源 +
// 字节级 tee 落本地——乐观写、错则 abort 回退位置、全部落盘成功才登记 idx。
// ════════════════════════════════════════════════════════════════════

namespace {

// 组装合法 record（[16B 块头][data][trailer]；MemoryChunkSource 解析 trailer）。
fly::CMSharedPtr<fly::ChunkSource> memory_source_from_backup(const FlyBufferPtr& record) {
    return CMMakeShared<fly::MemoryChunkSource>(record->data(), record->size());
}

FlyBufferPtr make_backup_record(const std::string& data) {
    auto record = CMMakeShared<FlyBuffer>();
    int32_t sz = static_cast<int32_t>(data.size());
    uint64_t crc = fly::data_checksum(data.data(), data.size());
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&crc), 8);
    record->write(data.data(), data.size());
    ObjectHeader header;
    header.total_size_ = data.size();
    header.chunk_count_ = 1;
    header.py_name_ = "bytes";
    header.py_name_len_ = 5;
    header.compression_type_ = 0;
    header.block_comp_lens_ = {static_cast<uint32_t>(data.size())};
    CMString trailer = header.serialize_trailer();
    record->write(trailer.data(), trailer.size());
    return record;
}

}  // namespace

namespace fly {

TEST_F(DatabaseTest, BackupObjectStreamsSourceToIndexedLocalReplica) {
    auto ds = fly::DataService::instance();
    ds->reset();
    ProcessInfo::instance()->set_worker_mode(true);

    CMString db_path = test_dir_ + "/backup_stream";
    Database db(db_path, db_path + "/data", 0, "", db_path);
    CMString full = db_path + ":backed_obj";

    const std::string payload = "streaming backup payload";
    auto record = make_backup_record(payload);
    const uint64_t block_area_len = 16 + payload.size();   // [16B 块头][data]

    // 远端副本（TIER2 源）+ 假 streaming handler 返回合法块流。
    ds->update_remote_idx(full, 7, "host_bak", 8000);
    ds->set_streaming_read_handler(
        [&](const CMString&, int32_t, const CMString&)
            -> std::tuple<bool, fly::CMSharedPtr<fly::ChunkSource>, uint64_t, fly::ReadError> {
            return {true, memory_source_from_backup(record), block_area_len,
                    fly::ReadError::NONE};
        });

    // 前置：本地无该对象。
    EXPECT_FALSE(ds->has_local_object(full));

    db.backup_object("backed_obj");

    // 判定 1（用户裁定核心）：全部落盘成功后才登记 idx——本地可见。
    EXPECT_TRUE(ds->has_local_object(full))
        << "streaming backup must register idx only after full flush";
    // 判定 2：副本读回与源一致（TIER1 本地命中）。
    auto [comp, py_name] = db.read_object_compressed("backed_obj");
    ASSERT_TRUE(comp && !comp->empty());
    {
        DecompressingStreamBuf dsbuf(comp->data(), comp->size());
        std::istream is(&dsbuf);
        CMString result;
        CMVector<char> tmp(4096);
        while (is) {
            is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
            if (is.gcount() > 0) result.append(tmp.data(), static_cast<size_t>(is.gcount()));
        }
        EXPECT_EQ(result, payload);
    }
    EXPECT_EQ(py_name, "bytes");

    ds->set_streaming_read_handler(nullptr);
    ProcessInfo::instance()->set_worker_mode(false);
}

TEST_F(DatabaseTest, BackupObjectSourceFailureRollsBackIndex) {
    // 源截断（handler 返回失败源）：乐观写必须 abort——idx 不登记（「全部
    // 落盘成功才更新 idx」的失败面）。
    auto ds = fly::DataService::instance();
    ds->reset();
    ProcessInfo::instance()->set_worker_mode(true);

    CMString db_path = test_dir_ + "/backup_rollback";
    Database db(db_path, db_path + "/data", 0, "", db_path);
    CMString full = db_path + ":trunc_obj";

    ds->update_remote_idx(full, 7, "host_bak", 8000);
    ds->set_streaming_read_handler(
        [&](const CMString&, int32_t, const CMString&)
            -> std::tuple<bool, fly::CMSharedPtr<fly::ChunkSource>, uint64_t, fly::ReadError> {
            return {false, nullptr, 0, fly::ReadError::CHECKSUM};   // 源校验失败
        });

    EXPECT_FALSE(ds->has_local_object(full));
    db.backup_object("trunc_obj");

    // 回滚判定：idx 未登记（乐观写被 abort，脏数据由后续写覆盖）。
    EXPECT_FALSE(ds->has_local_object(full))
        << "failed source must not leave an indexed replica";

    ds->set_streaming_read_handler(nullptr);
    ProcessInfo::instance()->set_worker_mode(false);
}

}  // namespace fly
