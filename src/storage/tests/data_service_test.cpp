#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <network/cpp/net_quality_monitor.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/worker_context.h>   // WorkerAgentContext::set_suggest_backup_func
#include <core/cpp/process_info.h>       // master/worker 进程语义切换（权威 remote_idx 保护测试）
#include <core/cpp/config.h>
#include <filesystem>
#include <istream>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <vector>

namespace {

static void write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
}

// db_path 废弃：db_path 现在是 db_path 的别名（不含 ':'，否则 split 歧义）。
// db32() 生成不含 ':' 的 db_path 用于测试。保留函数名 db32 仅为调用点兼容。
static CMString db32(const CMString& hint) {
    // 用路径风格（含 '/' 但不含 ':'），模拟真实 db_path。
    return "/test/" + hint;
}

class DataServiceTest : public ::testing::Test {
protected:
    CMString test_dir_;
    fly::CMSharedPtr<fly::DataService> ds_ = fly::DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_ds_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
        // DataService is a process-wide singleton shared across tests. Prior
        // tests may have left handler lambdas (capturing local references) and
        // index state behind; without a reset a later test that walks TIER2/TIER3
        // can invoke a dangling lambda → use-after-free. reset() clears the
        // handlers and all indexes so every test starts from a clean slate.
        ds_->reset();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DataServiceTest, TryReadLocalReturnsFalseForUnknownObject) {
    auto [found, result] = ds_->try_read_local("no_such_object");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, HasLocalObjectReturnsFalseWhenEmpty) {
    EXPECT_FALSE(ds_->has_local_object("anything"));
}

TEST_F(DataServiceTest, OnObjectWrittenAndFlushEnablesLocalRead) {
    CMString db_path = test_dir_ + "/local_read";
    Database db(db_path);

    write_raw(db, "local/obj", "hello", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("local/obj");
    EXPECT_TRUE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "hello");
}

TEST_F(DataServiceTest, IncompleteObjectNotReadable) {
    CMString db_path = db32("test_db");
    CMString full = db_path + ":pending/obj";

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, WriteCompletedMarksObjectReadable) {
    CMString db_path = db32("flush_db");
    CMString full = db_path + ":flush/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 10;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));

    CMVector<IndexEntry> entries = {entry};
    ds_->on_write_completed(db_path, full, entries);
    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, UpdateRemoteIdxAndLookup) {
    CMString full = db32("remote") + ":obj";
    EXPECT_FALSE(ds_->has_remote_location(full));

    ds_->update_remote_idx(full, 42, "192.168.1.10", 9000);

    EXPECT_TRUE(ds_->has_remote_location(full));

    auto info = ds_->lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id_, 42u);
    EXPECT_EQ(info.host_, "192.168.1.10");
    EXPECT_EQ(info.port_, 9000);
}

TEST_F(DataServiceTest, LookupRemoteIdxReturnsEmptyForUnknown) {
    auto info = ds_->lookup_remote_idx("unknown/obj");
    EXPECT_EQ(info.worker_id_, 0u);
    EXPECT_TRUE(info.host_.empty());
    EXPECT_EQ(info.port_, 0);
}

TEST_F(DataServiceTest, RegisterWorkerAndGetAddress) {
    ds_->register_worker(1, "10.0.0.1", 8001);
    ds_->register_worker(2, "10.0.0.2", 8002);

    auto addr1 = ds_->get_worker_address(1);
    EXPECT_EQ(addr1.host_, "10.0.0.1");
    EXPECT_EQ(addr1.port_, 8001);

    auto addr2 = ds_->get_worker_address(2);
    EXPECT_EQ(addr2.host_, "10.0.0.2");
    EXPECT_EQ(addr2.port_, 8002);
}

// get_all_workers returns a detached snapshot of every registered data-server
// peer. The bandwidth-probe thread uses it to pick its targets.
TEST_F(DataServiceTest, GetAllWorkersReturnsSnapshot) {
    ds_->register_worker(9101, "probe_a", 9101);
    ds_->register_worker(9102, "probe_b", 9102);

    auto peers = ds_->get_all_workers();
    bool has_a = false, has_b = false;
    for (const auto& p : peers) {
        if (p.worker_id_ == 9101) { has_a = true; EXPECT_EQ(p.host_, "probe_a"); }
        if (p.worker_id_ == 9102) { has_b = true; EXPECT_EQ(p.host_, "probe_b"); }
    }
    EXPECT_TRUE(has_a);
    EXPECT_TRUE(has_b);
}

TEST_F(DataServiceTest, GetWorkerAddressReturnsEmptyForUnknown) {
    auto addr = ds_->get_worker_address(9999);
    EXPECT_EQ(addr.worker_id_, 0u);
    EXPECT_TRUE(addr.host_.empty());
}

TEST_F(DataServiceTest, MultipleObjectsInSameDatabase) {
    CMString db_path = test_dir_ + "/multi_obj";
    Database db(db_path);

    write_raw(db, "multi/a", "data_a", false);
    write_raw(db, "multi/b", "data_b", false);
    write_raw(db, "multi/c", "data_c", false);
    fly::DataService::instance()->drain_write_back();

    EXPECT_TRUE(ds_->has_local_object(db.get_full_name("multi/a")));
    EXPECT_TRUE(ds_->has_local_object(db.get_full_name("multi/b")));
    EXPECT_TRUE(ds_->has_local_object(db.get_full_name("multi/c")));

    auto [fa, ra] = ds_->try_read_local(db.get_full_name("multi/a"));
    EXPECT_TRUE(fa);
    CMString da(ra.data_buffer_.begin(), ra.data_buffer_.end());
    EXPECT_EQ(da, "data_a");

    auto [fb, rb] = ds_->try_read_local(db.get_full_name("multi/b"));
    EXPECT_TRUE(fb);
    CMString db2(rb.data_buffer_.begin(), rb.data_buffer_.end());
    EXPECT_EQ(db2, "data_b");
}

TEST_F(DataServiceTest, TypedObjectReadableViaDataService) {
    CMString db_path = test_dir_ + "/typed_ds";
    Database db(db_path);

    db.write_pickle_bytes("typed/ds_obj", "typed_payload", 13, "MyType");
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("typed/ds_obj");
    auto [found, result] = ds_->try_read_local(full);
    EXPECT_TRUE(found);
    EXPECT_EQ(result.py_name_, "MyType");
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "typed_payload");
}

TEST_F(DataServiceTest, RemoteIdxSupportsMultipleWorkers) {
    CMString full = db32("multi") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    // Both workers should be tracked
    auto workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 2u);
    EXPECT_EQ(workers[0], 1u);
    EXPECT_EQ(workers[1], 2u);

    // Lookup returns first registered worker
    auto info = ds_->lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id_, 1u);
    EXPECT_EQ(info.host_, "host_a");
    EXPECT_EQ(info.port_, 8000);
}

TEST_F(DataServiceTest, LookupAllRemoteIdxReturnsEveryReplica) {
    CMString full = db32("allrepl") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);
    ds_->update_remote_idx(full, 3, "host_c", 7000);

    // Must return ALL replicas (worker + host + port), not just the first.
    auto all = ds_->lookup_all_remote_idx(full);
    EXPECT_EQ(all.size(), 3u);

    // Each replica's address must resolve via worker_registry_.
    std::set<uint64_t> seen_wids;
    for (const auto& loc : all) {
        EXPECT_FALSE(loc.host_.empty());
        seen_wids.insert(loc.worker_id_);
    }
    EXPECT_EQ(seen_wids.size(), 3u);
}

TEST_F(DataServiceTest, LookupAllRemoteIdxEmptyForUnknownObject) {
    auto all = ds_->lookup_all_remote_idx(db32("none") + ":missing");
    EXPECT_TRUE(all.empty());
}

// TIER2/TIER3 副本遍历顺序：storage_only 优先于 hybrid（即便 hybrid 先注册）。
TEST_F(DataServiceTest, LookupAllPrefersStorageRole) {
    CMString full = db32("pref_storage") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);                  // hybrid（先注册）
    ds_->update_remote_idx(full, 2, "host_b", 9000, 0, true);         // storage_only

    auto all = ds_->lookup_all_remote_idx(full);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].worker_id_, 2u);  // storage 优先
    EXPECT_TRUE(all[0].storage_only_);
    EXPECT_EQ(all[1].worker_id_, 1u);
    EXPECT_FALSE(all[1].storage_only_);
}

// 已死 holder 排尾（判死后 remote_idx 条目保留的语义下，读侧不浪费时间
// connect 死副本）。storage 标死后同样让位于存活 hybrid。
TEST_F(DataServiceTest, LookupAllDeprioritizesDead) {
    CMString full = db32("pref_alive") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000, 0, true);         // storage
    ds_->update_remote_idx(full, 3, "host_c", 7000);

    ds_->set_worker_alive(2, false);  // storage 判死 → 排尾

    auto all = ds_->lookup_all_remote_idx(full);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].worker_id_, 1u);  // 存活 hybrid
    EXPECT_EQ(all[1].worker_id_, 3u);  // 存活 hybrid
    EXPECT_EQ(all[2].worker_id_, 2u);  // 死副本最末
    EXPECT_FALSE(all[2].alive_);
}

// set_worker_alive 对未登记 worker 是 no-op；is_storage_worker 缺省 false。
TEST_F(DataServiceTest, WorkerAliveAndRoleAccessors) {
    ds_->set_worker_alive(999, false);  // no-op，不崩溃
    EXPECT_FALSE(ds_->is_storage_worker(999));

    CMString full = db32("accessor") + ":obj";
    ds_->update_remote_idx(full, 7, "host_x", 6000, 0, true);
    EXPECT_TRUE(ds_->is_storage_worker(7));
    EXPECT_TRUE(ds_->get_worker_address(7).alive_);

    ds_->set_worker_alive(7, false);
    EXPECT_TRUE(ds_->is_storage_worker(7));   // role 不因判死丢失
    EXPECT_FALSE(ds_->get_worker_address(7).alive_);
}

TEST_F(DataServiceTest, RegisterDatabaseAndLocalRead) {
    CMString db_path = test_dir_ + "/reg_db";
    std::filesystem::create_directories(db_path);

    ds_->register_database(db_path, "");

    CMString full = db_path + ":manual/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);

    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, RemoveLocalIndexMakesObjectUnreadable) {
    CMString db_path = test_dir_ + "/remove_local";
    Database db(db_path);

    write_raw(db, "remove/local", "data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("remove/local");
    EXPECT_TRUE(ds_->has_local_object(full));

    ds_->remove_local_index(full);

    EXPECT_FALSE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, RemoveLocalIndexOnlyAffectsTarget) {
    CMString db_path = test_dir_ + "/remove_one_local";
    Database db(db_path);

    write_raw(db, "keep/me", "keep_data", false);
    write_raw(db, "remove/me", "remove_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString keep_full = db.get_full_name("keep/me");
    CMString remove_full = db.get_full_name("remove/me");

    ds_->remove_local_index(remove_full);

    EXPECT_TRUE(ds_->has_local_object(keep_full));
    EXPECT_FALSE(ds_->has_local_object(remove_full));
}

TEST_F(DataServiceTest, RemoveRemoteIndexClearsLocation) {
    CMString full = db32("remote") + ":remove_test";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    EXPECT_TRUE(ds_->has_remote_location(full));

    ds_->remove_remote_index(full);

    EXPECT_FALSE(ds_->has_remote_location(full));

    auto info = ds_->lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id_, 0u);
    EXPECT_TRUE(info.host_.empty());
}

TEST_F(DataServiceTest, RemoveRemoteIndexOnlyAffectsTarget) {
    CMString keep_full = db32("remote") + ":keep";
    CMString remove_full = db32("remote") + ":remove";
    ds_->update_remote_idx(keep_full, 1, "host_a", 8000);
    ds_->update_remote_idx(remove_full, 2, "host_b", 9000);

    ds_->remove_remote_index(remove_full);

    EXPECT_TRUE(ds_->has_remote_location(keep_full));
    EXPECT_FALSE(ds_->has_remote_location(remove_full));
}

TEST_F(DataServiceTest, RestoreEntriesMakesObjectsReadable) {
    CMString db_path = test_dir_ + "/restore_test";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name_ = db_path + ":obj_a";
    e1.file_name_ = "test.dat";
    e1.offset_ = 0;
    e1.size_ = 5;
    e1.is_large_ = false;
    e1.block_count_ = 0;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name_ = db_path + ":obj_b";
    e2.file_name_ = "test.dat";
    e2.offset_ = 10;
    e2.size_ = 7;
    e2.is_large_ = false;
    e2.block_count_ = 0;
    entries.push_back(e2);

    ds_->restore_entries(db_path, entries);

    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj_a"));
    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj_b"));
}

TEST_F(DataServiceTest, RestoreEntriesMultipleEntriesPerObject) {
    CMString db_path = test_dir_ + "/restore_multi";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name_ = db_path + ":large_obj";
    e1.file_name_ = "data_0.dat";
    e1.offset_ = 0;
    e1.size_ = 100;
    e1.is_large_ = true;
    e1.block_count_ = 1;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name_ = db_path + ":large_obj";
    e2.file_name_ = "data_0.dat";
    e2.offset_ = 100;
    e2.size_ = 200;
    e2.is_large_ = true;
    e2.block_count_ = 1;
    entries.push_back(e2);

    ds_->restore_entries(db_path, entries);

    EXPECT_TRUE(ds_->has_local_object(db_path + ":large_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesAppendsToExisting) {
    CMString db_path = test_dir_ + "/restore_append";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    IndexEntry e1;
    e1.object_name_ = db_path + ":obj";
    e1.file_name_ = "test.dat";
    e1.offset_ = 0;
    e1.size_ = 5;
    e1.is_large_ = false;
    e1.block_count_ = 0;
    ds_->on_object_written(db_path, db_path + ":obj", e1);
    ds_->on_flush(db_path);
    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj"));
    CMVector<IndexEntry> restore_entries_vec;
    IndexEntry e2;
    e2.object_name_ = db_path + ":new_obj";
    e2.file_name_ = "test.dat";
    e2.offset_ = 100;
    e2.size_ = 10;
    e2.is_large_ = false;
    e2.block_count_ = 0;
    restore_entries_vec.push_back(e2);

    ds_->restore_entries(db_path, restore_entries_vec);

    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj"));
    EXPECT_TRUE(ds_->has_local_object(db_path + ":new_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesEmptyVectorIsNoop) {
    CMVector<IndexEntry> empty;
    ds_->restore_entries(db32("empty_db"), empty);
}

// 接管/重载的等价去重：同对象已有 entry 的 write_context_hash_ 与来者相同
// → 字节等价副本跳过，entries_ 不膨胀（backup 副本 vs 接管副本场景）。
TEST_F(DataServiceTest, RestoreEntriesSkipsEquivalentHash) {
    CMString db_path = test_dir_ + "/restore_equiv";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    IndexEntry first;
    first.object_name_ = db_path + ":obj";
    first.file_name_ = "data_dead_w1.dat";
    first.offset_ = 0;
    first.size_ = 5;
    first.write_context_hash_ = "hash_abc";
    ds_->restore_entries(db_path, {first});

    // 接管副本：内容等价（hash 相同），来源不同的物理文件。
    IndexEntry takeover;
    takeover.object_name_ = db_path + ":obj";
    takeover.file_name_ = "data_backup_w2.dat";
    takeover.offset_ = 100;
    takeover.size_ = 5;
    takeover.write_context_hash_ = "hash_abc";
    ds_->restore_entries(db_path, {takeover});

    auto entries = ds_->find_local_entries(db_path + ":obj");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 1u);  // 等价跳过，无膨胀
}

// hash 不同（backup 之后源重写过）→ 必须保留两个 entry，读路径按
// entries.back() 选最新——禁止按「对象已存在」跳过加载（数据回退防护）。
TEST_F(DataServiceTest, RestoreEntriesKeepsDifferentHash) {
    CMString db_path = test_dir_ + "/restore_newer";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    IndexEntry backup_copy;
    backup_copy.object_name_ = db_path + ":obj";
    backup_copy.file_name_ = "data_backup.dat";
    backup_copy.offset_ = 0;
    backup_copy.size_ = 5;
    backup_copy.write_context_hash_ = "hash_old";
    ds_->restore_entries(db_path, {backup_copy});

    IndexEntry takeover_newer;
    takeover_newer.object_name_ = db_path + ":obj";
    takeover_newer.file_name_ = "data_source.dat";
    takeover_newer.offset_ = 50;
    takeover_newer.size_ = 9;
    takeover_newer.write_context_hash_ = "hash_new";
    ds_->restore_entries(db_path, {takeover_newer});

    auto entries = ds_->find_local_entries(db_path + ":obj");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 2u);
    EXPECT_EQ(entries->back().write_context_hash_, "hash_new");  // back() = 最新
}

// hash 为空（无指纹）不判等价：保守加载，两 entry 共存。
TEST_F(DataServiceTest, RestoreEntriesNoDedupWithoutHash) {
    CMString db_path = test_dir_ + "/restore_nohash";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    IndexEntry e1;
    e1.object_name_ = db_path + ":obj";
    e1.file_name_ = "a.dat";
    e1.offset_ = 0;
    e1.size_ = 5;
    ds_->restore_entries(db_path, {e1});
    ds_->restore_entries(db_path, {e1});

    auto entries = ds_->find_local_entries(db_path + ":obj");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 2u);
}

TEST_F(DataServiceTest, RestoreEntriesFromLocalIndexFile) {
    CMString db_path = test_dir_ + "/restore_from_idx";
    Database db(db_path);

    write_raw(db, "idx/obj1", "data1", false);
    write_raw(db, "idx/obj2", "data2", false);
    fly::DataService::instance()->drain_write_back();

    CMString idx_path = db_path + "/" + db.get_writer_id() + ".idx";

    LocalIndex source_idx(idx_path);
    source_idx.load();
    auto all_entries = source_idx.get_all_entries();
    ASSERT_FALSE(all_entries.empty());

    CMString original_db_path = db.get_db_path();
    CMString restore_base = test_dir_ + "/restored_db";
    std::filesystem::create_directories(restore_base);
    ds_->register_database(original_db_path, restore_base, "");

    ds_->restore_entries(original_db_path, all_entries);

    // entry.object_name_ 是 short_name（LocalIndex 不再存 db_path 前缀），
    // has_local_object 用 full_name 作 key，需拼接。
    for (const auto& e : all_entries) {
        EXPECT_TRUE(ds_->has_local_object(original_db_path + ":" + e.object_name_));
    }
}

TEST_F(DataServiceTest, DbPathWithSlashesHandledCorrectly) {
    // db_path 废弃后 db_path == db_path（含 '/'，不含 ':'）。
    // full_name = "db_path:short"，split 用 rfind(':') 正确切分。
    CMString db_path = db32("path_test");  // "/test/path_test"
    CMString full = db_path + ":obj/with/slashes";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);

    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, SplitFullRfindHandlesLongDbPath) {
    // db_path 变长（不再是固定 10 字符），rfind(':') 必须正确切分。
    // 验证 split_full_name 对长 db_path + short 的切分。
    auto [db, short_name] = fly::split_full_name("/a/very/long/db/path:my_obj");
    EXPECT_EQ(db, "/a/very/long/db/path");
    EXPECT_EQ(short_name, "my_obj");
}

TEST_F(DataServiceTest, ShortFullNameTreatedAsNoDbId) {
    CMString short_name = "short_obj_name";

    IndexEntry entry;
    entry.object_name_ = short_name;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written("", short_name, entry);
    ds_->on_flush("");

    EXPECT_TRUE(ds_->has_local_object(short_name));
}

TEST_F(DataServiceTest, RemoveIndexByShortName) {
    CMString db_path = db32("remove_short");
    CMString full = db_path + ":remove_target";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);
    EXPECT_TRUE(ds_->has_local_object(full));

    ds_->remove_local_index(full);
    EXPECT_FALSE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, WriteCompletedOnlyAffectsTargetDb) {
    CMString db_a = db32("flush_db_a");
    CMString db_b = db32("flush_db_b");
    CMString full_a = db_a + ":obj_a";
    CMString full_b = db_b + ":obj_b";

    IndexEntry ea;
    ea.object_name_ = full_a;
    ea.file_name_ = "test.dat";
    ea.offset_ = 0;
    ea.size_ = 5;
    ea.is_large_ = false;
    ea.block_count_ = 0;

    IndexEntry eb;
    eb.object_name_ = full_b;
    eb.file_name_ = "test.dat";
    eb.offset_ = 0;
    eb.size_ = 5;
    eb.is_large_ = false;
    eb.block_count_ = 0;

    ds_->on_write_started(db_a, full_a);
    ds_->on_write_started(db_b, full_b);

    CMVector<IndexEntry> entries_a = {ea};
    ds_->on_write_completed(db_a, full_a, entries_a);
    EXPECT_TRUE(ds_->has_local_object(full_a));
    EXPECT_FALSE(ds_->has_local_object(full_b));

    CMVector<IndexEntry> entries_b = {eb};
    ds_->on_write_completed(db_b, full_b, entries_b);
    EXPECT_TRUE(ds_->has_local_object(full_b));
}

TEST_F(DataServiceTest, HasDatabaseReturnsTrue) {
    CMString db_path = test_dir_ + "/has_db";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");
    EXPECT_TRUE(ds_->has_database(db_path));
}

TEST_F(DataServiceTest, HasDatabaseReturnsFalseForUnknown) {
    EXPECT_FALSE(ds_->has_database(db32("unknown_db")));
}

TEST_F(DataServiceTest, UnregisterDatabaseRemovesIt) {
    CMString db_path = test_dir_ + "/unreg_db";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");
    EXPECT_TRUE(ds_->has_database(db_path));

    ds_->unregister_database(db_path);
    EXPECT_FALSE(ds_->has_database(db_path));
}

TEST_F(DataServiceTest, RemoveRemoteLocationByWorkerId) {
    // 踢副本是 worker 进程的自愈行为（master 进程豁免，见
    // MasterProcessKeepsAuthoritativeLocationOnReadFailure）——显式切 worker 模式。
    ProcessInfo::instance()->set_worker_mode(true);
    CMString full = db32("rr_worker") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    auto workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 2u);

    ds_->remove_remote_location(full, 1);

    workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 2u);
    ProcessInfo::instance()->set_worker_mode(false);
}

TEST_F(DataServiceTest, RemoveRemoteLocationByWorkerIdCleansUpWhenEmpty) {
    ProcessInfo::instance()->set_worker_mode(true);
    CMString full = db32("rr_cleanup") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);

    ds_->remove_remote_location(full, 1);

    EXPECT_FALSE(ds_->has_remote_location(full));
    EXPECT_TRUE(ds_->get_remote_workers(full).empty());
    ProcessInfo::instance()->set_worker_mode(false);
}

TEST_F(DataServiceTest, OnObjectWrittenSetsComplete) {
    CMString db_path = db32("flush_obj_db");
    CMString full = db_path + ":flush/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteStartedCreatesEntry) {
    CMString db_path = db32("start_db");
    CMString full = db_path + ":started/obj";

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteFailedRemovesEntry) {
    CMString db_path = db32("fail_db");
    CMString full = db_path + ":failed/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);
    EXPECT_TRUE(ds_->has_local_object(full));

    ds_->on_write_failed(db_path, full, "error msg");
    EXPECT_FALSE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, AddRemoteLocation) {
    CMString full = db32("add_remote") + ":obj";
    ds_->register_worker(1, "host_a", 8000);
    ds_->add_remote_location(full, 1);

    EXPECT_TRUE(ds_->has_remote_location(full));
    auto workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 1u);
}

TEST_F(DataServiceTest, GetRemoteWorkersEmptyForMissing) {
    auto workers = ds_->get_remote_workers("no/such/obj");
    EXPECT_TRUE(workers.empty());
}

TEST_F(DataServiceTest, HasRemoteLocationFalseForMissing) {
    EXPECT_FALSE(ds_->has_remote_location("missing/remote"));
}

// 2026-08-16 冗余清理：WriteBackQueueStartStop / EnqueueWriteBackAutoStarts 与
// write_back_queue_test（属主测试）的 StartStop/BasicEnqueueAndDrain 重复，已删除。

TEST_F(DataServiceTest, FindLocalEntriesReturnsNoneForMissing) {
    auto entries = ds_->find_local_entries("no/local/entries");
    EXPECT_FALSE(entries.has_value());
}

TEST_F(DataServiceTest, TryReadLocalRawReturnsData) {
    CMString db_path = test_dir_ + "/raw_read";
    Database db(db_path);

    write_raw(db, "raw/obj", "raw_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("raw/obj");
    auto [found, raw] = ds_->try_read_local_raw(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!raw || raw->empty());
}

TEST_F(DataServiceTest, TryReadLocalRawReturnsFalseForMissing) {
    auto [found, raw] = ds_->try_read_local_raw("missing/obj");
    EXPECT_FALSE(found);
    EXPECT_TRUE(!raw);
}

TEST_F(DataServiceTest, TryReadRemoteReturnsLocalIfAvailable) {
    CMString db_path = test_dir_ + "/remote_local";
    Database db(db_path);

    write_raw(db, "remote/local_obj", "local_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("remote/local_obj");
    auto [found, result] = ds_->try_read_remote(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "local_data");
}

TEST_F(DataServiceTest, TryReadRemoteReturnsFalseForMissing) {
    auto [found, result] = ds_->try_read_remote("no_such_remote_obj");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadRemoteSetsCanStillProduceFlag) {
    auto [found, result] = ds_->try_read_remote("missing_remote_data");
    EXPECT_FALSE(found);
    EXPECT_FALSE(result.can_still_produce_);
}

TEST_F(DataServiceTest, ReadRawCompressedReturnsLocalRaw) {
    CMString db_path = test_dir_ + "/raw_comp";
    Database db(db_path);

    write_raw(db, "comp/obj", "comp_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("comp/obj");
    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!raw || raw->empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DataServiceTest, ReadRawCompressedReturnsFalseForMissing) {
    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed("missing_comp");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, RegisterDatabaseDuplicateUpdatesExisting) {
    CMString db_path = db32("dup_db");
    CMString base1 = test_dir_ + "/dup_db1";
    CMString base2 = test_dir_ + "/dup_db2";
    std::filesystem::create_directories(base1);
    std::filesystem::create_directories(base2);

    ds_->register_database(db_path, base1, "");
    EXPECT_TRUE(ds_->has_database(db_path));

    ds_->register_database(db_path, base2, "");
    EXPECT_TRUE(ds_->has_database(db_path));
}

TEST_F(DataServiceTest, RemoveRemoteLocationByFullObject) {
    CMString full = db32("rr_full") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    EXPECT_TRUE(ds_->has_remote_location(full));

    ds_->remove_remote_location(full);
    EXPECT_FALSE(ds_->has_remote_location(full));
}

TEST_F(DataServiceTest, RestoreEntriesWithShortObjectNames) {
    CMString db_path = test_dir_ + "/short_restore";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e;
    e.object_name_ = db_path + ":simple_name";
    e.file_name_ = "test.dat";
    e.offset_ = 0;
    e.size_ = 10;
    e.is_large_ = false;
    e.block_count_ = 0;
    entries.push_back(e);

    ds_->restore_entries(db_path, entries);
    EXPECT_TRUE(ds_->has_local_object(db_path + ":simple_name"));
}

TEST_F(DataServiceTest, OnWriteStartedAndCompletedCycle) {
    CMString db_path = db32("cycle_db");
    CMString full = db_path + ":cycle/obj";

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 10;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    CMVector<IndexEntry> entries = {entry};
    ds_->on_write_completed(db_path, full, entries);
    ds_->on_object_flushed(full);

    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteCompletedForMissingDbIsNoop) {
    CMString db_path = db32("missing_db_wc");
    CMString full = db_path + ":missing/obj";
    CMVector<IndexEntry> entries;
    EXPECT_NO_THROW(ds_->on_write_completed(db_path, full, entries));
}

TEST_F(DataServiceTest, OnWriteFailedForMissingDbIsNoop) {
    CMString db_path = db32("missing_db_wf");
    CMString full = db_path + ":missing/obj";
    EXPECT_NO_THROW(ds_->on_write_failed(db_path, full, "error"));
}

TEST_F(DataServiceTest, OnFlushForMissingDbIsNoop) {
    EXPECT_NO_THROW(ds_->on_flush(db32("nonexistent_flush")));
}

TEST_F(DataServiceTest, FindLocalEntriesReturnsData) {
    CMString db_path = db32("find_db");
    CMString full = db_path + ":find/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 10;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);

    auto found = ds_->find_local_entries(full);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->size(), 1u);
    EXPECT_EQ((*found)[0].object_name_, full);
}

TEST_F(DataServiceTest, SetRemoteCompressedReadHandler) {
    bool called = false;
    ds_->set_remote_compressed_read_handler([&called](const CMString& name) -> std::tuple<bool, bool> {
        called = true;
        return std::make_tuple(false, false);
    });
    EXPECT_NO_THROW(ds_->set_remote_compressed_read_handler(nullptr));
}

TEST_F(DataServiceTest, SetDirectCompressedReadHandler) {
    ds_->set_direct_compressed_read_handler(
        [](const CMString& host, int32_t port, const CMString& name) -> std::tuple<bool, FlyBufferPtr, CMString, CMString, fly::ReadError> {
            return std::make_tuple(false, nullptr, CMString{}, CMString{}, fly::ReadError::NETWORK);
        });
    EXPECT_NO_THROW(ds_->set_direct_compressed_read_handler(nullptr));
}

// TIER2 multi-replica failover: when the first replica returns a typed error,
// read_raw_compressed must move on to the next replica instead of giving up.
TEST_F(DataServiceTest, Tier2FailoverToNextReplicaOnObjectNotFound) {
    CMString full = db32("tier2") + ":obj";
    // Two replicas: W1 (will return OBJECT_NOT_FOUND) and W2 (has data).
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    // Build a tiny valid compressed payload so W2 "returns data".
    std::string payload = "hello";
    fly::CMSharedPtr<FlyBuffer> data = fly::CMMakeShared<FlyBuffer>();
    data->write(payload.data(), payload.size());

    int call_count = 0;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t port, const CMString& name)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            call_count++;
            if (host == "host_a") {
                // W1 no longer holds the object → permanent for this replica.
                return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
            }
            // W2 returns the data.
            return {true, data, CMString("bytes"), {}, fly::ReadError::NONE};
        });

    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_TRUE(found);
    ASSERT_TRUE(raw && !raw->empty());
    EXPECT_EQ(call_count, 2);  // tried W1, then W2
}

// TIER2 must remove a replica that returned OBJECT_NOT_FOUND, so a subsequent
// read goes straight to the surviving replica.
TEST_F(DataServiceTest, Tier2RemovesReplicaOnObjectNotFound) {
    ProcessInfo::instance()->set_worker_mode(true);  // 踢副本为 worker 进程自愈行为
    CMString full = db32("tier2rm") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    fly::CMSharedPtr<FlyBuffer> data = fly::CMMakeShared<FlyBuffer>();
    data->write("x", 1);

    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t port, const CMString& name)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            if (host == "host_a") {
                return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
            }
            return {true, data, CMString("bytes"), {}, fly::ReadError::NONE};
        });

    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_TRUE(found);

    // W1 must have been pruned: only W2 remains.
    auto workers = ds_->get_remote_workers(full);
    ASSERT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 2u);
    ProcessInfo::instance()->set_worker_mode(false);
}

// TIER2↔TIER3 回环（此前无专测）：本地无副本 → TIER3 纯位置查询刷新 remote_idx
// → 重进 TIER2 命中。回环是 read_raw_compressed 的核心容错路径。
TEST_F(DataServiceTest, Tier3RefreshReentersTier2AndHits) {
    CMString full = db32("tier3loop") + ":obj";
    // 初始无本地副本：TIER2 首轮 replicas 空 → 进 TIER3。

    fly::CMSharedPtr<FlyBuffer> data = fly::CMMakeShared<FlyBuffer>();
    data->write("tier3_data", 10);

    int tier2_calls = 0;
    int tier3_calls = 0;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t port, const CMString& name)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            tier2_calls++;
            return {true, data, CMString("bytes"), {}, fly::ReadError::NONE};
        });
    ds_->set_remote_compressed_read_handler(
        [&](const CMString& name) -> std::tuple<bool, bool> {
            tier3_calls++;
            // master 位置查询：登记新副本（刷新本地 remote_idx），报告 refreshed。
            ds_->update_remote_idx(name, 7, "host_t3", 7000);
            return {true, true};
        });

    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_TRUE(found);
    ASSERT_TRUE(raw && !raw->empty());
    EXPECT_EQ(tier3_calls, 1);
    EXPECT_EQ(tier2_calls, 1);  // 刷新后重进 TIER2 首副本即命中
    ds_->remove_remote_index(full);
}

// 回环防抖（tier3_queried）：TIER3 刷新后 TIER2 仍读不到 → 二次耗尽直接终败，
// 不得再次回 TIER3（防 TIER2↔TIER3 无限弹跳）。用 OBJECT_NOT_FOUND 快速清空
// 副本表（避免 NETWORK 期限 30s 拖慢测试）。
TEST_F(DataServiceTest, Tier3QueriedGuardsAgainstBouncing) {
    ProcessInfo::instance()->set_worker_mode(true);  // 踢副本为 worker 进程自愈行为
    CMString full = db32("tier3bounce") + ":obj";

    int tier3_calls = 0;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t port, const CMString& name)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            // TIER3 登记的副本一律「已不持有」→ 本轮被踢空。
            return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
        });
    ds_->set_remote_compressed_read_handler(
        [&](const CMString& name) -> std::tuple<bool, bool> {
            tier3_calls++;
            ds_->update_remote_idx(name, 8, "host_t3b", 7000);
            return {true, true};  // 刷新成功 → 重进 TIER2
        });

    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_FALSE(found);
    EXPECT_EQ(tier3_calls, 1);  // 防抖生效：TIER3 只查一次
    ds_->remove_remote_index(full);
    ProcessInfo::instance()->set_worker_mode(false);
}

TEST_F(DataServiceTest, DataServerStartStop) {
    ds_->start_data_server("127.0.0.1", 0, 1);
    EXPECT_GT(ds_->get_data_port(), 0);
    ds_->stop_data_server();
}

TEST_F(DataServiceTest, ResetClearsAllState) {
    CMString db_path = db32("reset_db");
    ds_->register_database(db_path, test_dir_, "");
    ds_->register_worker(1, "host", 8000);

    ds_->reset();

    EXPECT_FALSE(ds_->has_database(db_path));
}

// ============================================================
// Auto-Backup Access Tracking (inline in remote_idx_)
// ============================================================

TEST_F(DataServiceTest, RemoteObjectMetaTracksReadCount) {
    CMString full = db32("meta_test") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);
    
    // Access tracking is now on remote_idx_ directly
    ds_->record_remote_access(full);
    ds_->record_remote_access(full);
    
    EXPECT_EQ(ds_->get_access_read_count(full), 2u);
}

TEST_F(DataServiceTest, RemoteObjectMetaResetClearsAccess) {
    CMString full = db32("reset_test") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);
    ds_->record_remote_access(full);
    EXPECT_EQ(ds_->get_access_read_count(full), 1u);

    ds_->reset();
    EXPECT_EQ(ds_->get_access_read_count(full), 0u);
}

// ── maybe_suggest_backup（worker TIER2 读累积 → suggest → reset）──

// 捕获 suggest_backup 调用的辅助结构。
struct SuggestCapture {
    int call_count = 0;
    CMString last_obj;
    uint64_t last_delta_count = 0;
    uint64_t last_delta_bytes = 0;
    int64_t last_size_bytes = 0;
};

TEST_F(DataServiceTest, MaybeSuggestBackupTriggersAndResetsOnCountThreshold) {
    Config::instance()->set_int("auto_backup_enabled", 1);
    Config::instance()->set_int("worker_suggest_count_threshold", 5);
    Config::instance()->set_int("worker_suggest_bytes_threshold", 1073741824);
    Config::instance()->set_int("worker_suggest_cooldown", 60);

    CMString full = db32("suggest_count") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);

    SuggestCapture cap;
    fly::WorkerAgentContext::set_suggest_backup_func(
        [&cap](const CMString& obj, uint64_t dc, uint64_t db, int64_t sz) {
            cap.call_count++;
            cap.last_obj = obj;
            cap.last_delta_count = dc;
            cap.last_delta_bytes = db;
            cap.last_size_bytes = sz;
        });

    // 累积 4 次（< threshold=5）→ 不 suggest
    for (int i = 0; i < 4; i++) {
        ds_->record_remote_access(full, 1024);
        ds_->maybe_suggest_backup(full);
    }
    EXPECT_EQ(cap.call_count, 0);
    EXPECT_EQ(ds_->get_access_read_count(full), 4u);

    // 第 5 次（>= threshold）→ suggest + reset（read_count 归零）
    ds_->record_remote_access(full, 1024);
    ds_->maybe_suggest_backup(full);
    EXPECT_EQ(cap.call_count, 1);
    EXPECT_EQ(cap.last_obj, full);
    EXPECT_EQ(cap.last_delta_count, 5u);
    EXPECT_EQ(cap.last_delta_bytes, 5 * 1024ull);
    EXPECT_EQ(cap.last_size_bytes, 1024);
    // reset 后 read_count 归零
    EXPECT_EQ(ds_->get_access_read_count(full), 0u);

    fly::WorkerAgentContext::clear();
    Config::instance()->set_int("auto_backup_enabled", 0);
}

TEST_F(DataServiceTest, MaybeSuggestBackupRespectsCooldown) {
    Config::instance()->set_int("auto_backup_enabled", 1);
    Config::instance()->set_int("worker_suggest_count_threshold", 3);
    Config::instance()->set_int("worker_suggest_bytes_threshold", 1073741824);
    Config::instance()->set_int("worker_suggest_cooldown", 3600);  // 长 cooldown 防跨测时间干扰

    CMString full = db32("suggest_cooldown") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);

    SuggestCapture cap;
    fly::WorkerAgentContext::set_suggest_backup_func(
        [&cap](const CMString&, uint64_t, uint64_t, int64_t) { cap.call_count++; });

    // 首次达阈值 → suggest + reset
    for (int i = 0; i < 3; i++) ds_->record_remote_access(full, 100);
    ds_->maybe_suggest_backup(full);
    EXPECT_EQ(cap.call_count, 1);

    // cooldown 内再次累积达阈值 → 不 suggest（cooldown 未过）
    for (int i = 0; i < 3; i++) ds_->record_remote_access(full, 100);
    ds_->maybe_suggest_backup(full);
    EXPECT_EQ(cap.call_count, 1);  // 仍只 1 次

    fly::WorkerAgentContext::clear();
    Config::instance()->set_int("auto_backup_enabled", 0);
}

TEST_F(DataServiceTest, MaybeSuggestBackupDisabledWhenAutoBackupOff) {
    // auto_backup_enabled=0（默认）→ 即使累积达阈值也不 suggest
    Config::instance()->set_int("auto_backup_enabled", 0);
    Config::instance()->set_int("worker_suggest_count_threshold", 1);

    CMString full = db32("suggest_disabled") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);

    SuggestCapture cap;
    fly::WorkerAgentContext::set_suggest_backup_func(
        [&cap](const CMString&, uint64_t, uint64_t, int64_t) { cap.call_count++; });

    ds_->record_remote_access(full, 1024);
    ds_->maybe_suggest_backup(full);
    EXPECT_EQ(cap.call_count, 0);  // 禁用 → 不 suggest

    fly::WorkerAgentContext::clear();
}

// ─── Read API coverage (raw / wait / remote) ───

// try_read_local_raw returns (found, compressed_bytes) for a flushed object.
TEST_F(DataServiceTest, TryReadLocalRawReturnsCompressedData) {
    CMString db_path = test_dir_ + "/raw_read";
    Database db(db_path);
    write_raw(db, "raw/obj", "payload", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("raw/obj");
    auto [found, comp] = ds_->try_read_local_raw(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!comp || comp->empty());
}

// try_read_local_raw for unknown object returns false.
TEST_F(DataServiceTest, TryReadLocalRawReturnsFalseForUnknown) {
    auto [found, comp] = ds_->try_read_local_raw("unknown_obj");
    EXPECT_FALSE(found);
    EXPECT_TRUE(!comp || comp->empty());
}

// try_read_local_raw short-circuits via ObjectCache low tier: after a prior
// read_object_compressed populates the low tier, a subsequent try_read_local_raw
// §4.7 low-tier cache 取消（2026-08-29）：原两例（缓存服务 + 读填充）改写为
// 取消语义锚定——读恒走盘（删盘文件即失败），读后不 populate。

// 删除盘文件后读必须失败（无缓存兜底——取消语义的正确性证明）。
TEST_F(DataServiceTest, TryReadLocalRawFailsAfterDiskRemoval) {
    CMString db_path = test_dir_ + "/serve_cache";
    Database db(db_path);
    write_raw(db, "serve/obj", "payload", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("serve/obj");
    fly::ObjectCache::instance().clear();

    // 盘在时读成功（走盘）。
    auto [comp, py_name] = db.read_object_compressed("serve/obj", false);
    ASSERT_FALSE(!comp || comp->empty());
    EXPECT_EQ(fly::ObjectCache::instance().low_size(), 0u)
        << "read must not populate low tier (cancelled)";

    // 删盘 → 读失败（无缓存短路）。
    for (auto& p : std::filesystem::directory_iterator(db_path)) {
        if (p.path().extension() == ".dat") {
            std::filesystem::remove(p.path());
        }
    }
    auto [found, raw] = ds_->try_read_local_raw(full);
    EXPECT_FALSE(found) << "disk removed + no cache = read must fail (§4.7)";

    fly::ObjectCache::instance().clear();
}

// try_read_local_raw 读后不 populate（取消语义）。
TEST_F(DataServiceTest, TryReadLocalRawDoesNotPopulateLowCache) {
    CMString db_path = test_dir_ + "/populate_cache";
    Database db(db_path);
    write_raw(db, "pop/obj", "data", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("pop/obj");
    fly::ObjectCache::instance().clear();
    EXPECT_EQ(fly::ObjectCache::instance().low_size(), 0u);

    auto [found, raw] = ds_->try_read_local_raw(full);
    ASSERT_TRUE(found);
    ASSERT_FALSE(!raw || raw->empty());
    EXPECT_EQ(fly::ObjectCache::instance().low_size(), 0u)
        << "try_read_local_raw must not populate low tier (cancelled)";

    fly::ObjectCache::instance().clear();
}

// try_read_remote invokes the configured remote read handler. Under the new
// model the handler is a pure location query (returns refreshed signal, no
// data); without a direct handler the read cannot fetch, so it fails — but the
// handler must still have been invoked.
TEST_F(DataServiceTest, TryReadRemoteInvokesHandler) {
    CMString full = db32("remote_db") + ":rmt_obj";

    bool handler_called = false;
    ds_->set_remote_compressed_read_handler(
        [&full, &handler_called](const CMString& name) -> std::tuple<bool, bool> {
            EXPECT_EQ(name, full);
            handler_called = true;
            // Report "master knows of no location" to terminate cleanly.
            return {false, false};
        });

    auto [found, result] = ds_->try_read_remote(full);
    EXPECT_FALSE(found);
    EXPECT_TRUE(handler_called);
}

// is_write_in_progress reflects on_write_started / on_write_completed lifecycle.
TEST_F(DataServiceTest, IsWriteInProgressReflectsLifecycle) {
    CMString db_path = db32("wip_db");
    CMString full = db_path + ":wip_obj";
    ds_->register_database(test_dir_ + "/wip", "", "writer_w");
    EXPECT_FALSE(ds_->is_write_in_progress(full));

    ds_->on_write_started(db_path, full);
    EXPECT_TRUE(ds_->is_write_in_progress(full));
}

// TIER1 快速唤醒：对象处于 INCOMPLETE 时，try_read_local_raw(wait=true) 阻塞等待
// 本地写完成，被 on_write_completed 唤醒后返回数据。验证 per-db cv + notify_all
// 的核心读路径——替代旧的 per-object cv + _or_wait 死代码路径。
// 用 future + 超时断言防死锁卡测试（wait 是无限等待，若 notify 丢失会永久阻塞）。
TEST_F(DataServiceTest, TIER1WaitsForLocalWriteComplete) {
    CMString db_path = db32("tier1wait_db");
    CMString full = db_path + ":pending_obj";
    ds_->register_database(test_dir_ + "/tier1wait", "", "writer_t1");

    // 先标记 INCOMPLETE（模拟异步写开始）。
    ds_->on_write_started(db_path, full);
    EXPECT_TRUE(ds_->is_write_in_progress(full));

    // reader 线程：调 try_read_local_raw(wait=true)，应阻塞直到写完成。
    auto reader = std::async(std::launch::async, [&]() {
        return ds_->try_read_local_raw(full, /*wait_local_write=*/true);
    });

    // 确认 reader 已进入 wait（尚未完成）。短暂 sleep 让 reader 抢到锁进入 wait。
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(reader.wait_for(std::chrono::seconds(0)), std::future_status::timeout)
        << "reader should be blocked waiting for write completion";

    // 写完成：唤醒 reader。
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 4;
    entry.is_large_ = false;
    entry.block_count_ = 1;
    CMVector<IndexEntry> entries = {entry};
    ds_->on_write_completed(db_path, full, entries);

    // reader 应被唤醒并返回（但数据因无真实 .dat 文件会读失败 → found=false）。
    // 关键断言：reader 不再阻塞（被唤醒），而非返回值（无真实数据）。
    ASSERT_EQ(reader.wait_for(std::chrono::seconds(5)), std::future_status::ready)
        << "reader was not woken within 5s — notify lost?";
}

// TIER1 遇 FAILED：try_read_local_raw(wait=true) 被 on_write_failed 唤醒后返回 false，
// 让上层 read_raw_compressed 走 TIER2 兜底。
TEST_F(DataServiceTest, TIER1ReturnsFalseOnFailedAfterWait) {
    CMString db_path = db32("tier1fail_db");
    CMString full = db_path + ":fail_obj";
    ds_->register_database(test_dir_ + "/tier1fail", "", "writer_t2");

    ds_->on_write_started(db_path, full);

    auto reader = std::async(std::launch::async, [&]() {
        return ds_->try_read_local_raw(full, /*wait_local_write=*/true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(reader.wait_for(std::chrono::seconds(0)), std::future_status::timeout);

    ds_->on_write_failed(db_path, full, "test failure");

    auto [found, raw] = reader.get();
    EXPECT_FALSE(found) << "FAILED object should return false (fallback to TIER2)";
}

// 多 waiter 并发等待同一 db 的写完成：notify_all 应唤醒所有 waiter。
// 验证 per-db cv 的 notify_all 策略不会丢失唤醒（notify_one 会随机唤醒导致丢失）。
TEST_F(DataServiceTest, TIER1MultipleWaitersAllWoken) {
    CMString db_path = db32("tier1conc_db");
    CMString full = db_path + ":conc_obj";
    ds_->register_database(test_dir_ + "/tier1conc", "", "writer_t3");

    ds_->on_write_started(db_path, full);

    constexpr int kWaiters = 5;
    std::vector<std::future<std::pair<bool, FlyBufferPtr>>> readers;
    for (int i = 0; i < kWaiters; ++i) {
        readers.emplace_back(std::async(std::launch::async, [&]() {
            return ds_->try_read_local_raw(full, /*wait_local_write=*/true);
        }));
    }

    // 确认所有 reader 已进入 wait。
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    for (auto& r : readers) {
        EXPECT_EQ(r.wait_for(std::chrono::seconds(0)), std::future_status::timeout)
            << "all readers should be blocked before write completes";
    }

    // 写完成：notify_all 应唤醒所有 reader。
    IndexEntry entry;
    entry.object_name_ = full;
    CMVector<IndexEntry> entries = {entry};
    ds_->on_write_completed(db_path, full, entries);

    for (auto& r : readers) {
        ASSERT_EQ(r.wait_for(std::chrono::seconds(5)), std::future_status::ready)
            << "a reader was not woken within 5s — notify_all lost a waiter?";
        r.get();  // 取结果（无真实数据，found=false，但不阻塞即通过）
    }
}

// TIER2 must try replicas in net-quality order: the better-scored host is
// contacted first. Two replicas registered as host_a then host_b; we score
// host_b higher, so the contact order must flip to b, a. Each replica returns
// OBJECT_NOT_FOUND so the full round is observed (no early success shortcut).
TEST_F(DataServiceTest, Tier2PrefersHigherScoredReplica) {
    ProcessInfo::instance()->set_worker_mode(true);  // 全轮失败会走踢副本路径（worker 行为）
    auto& mon = fly::NetQualityMonitor::instance();
    mon.clear();
    CMString full = db32("tiersort") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);
    mon.update_rtt("host_b", 1.0);   // host_b: low RTT → high score
    mon.update_rtt("host_a", 200.0); // host_a: high RTT → low score

    std::vector<CMString> order;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t /*port*/, const CMString& /*name*/)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            order.push_back(host);
            return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
        });

    ds_->read_raw_compressed(full);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "host_b");  // higher score first
    EXPECT_EQ(order[1], "host_a");
    mon.clear();
    ProcessInfo::instance()->set_worker_mode(false);
}

// With equal (or no) scores, stable_sort must preserve registration order, so
// cold-start behavior is identical to before the feature.
TEST_F(DataServiceTest, Tier2KeepsRegistrationOrderWhenScoresEqual) {
    ProcessInfo::instance()->set_worker_mode(true);  // 全轮失败会走踢副本路径（worker 行为）
    auto& mon = fly::NetQualityMonitor::instance();
    mon.clear();
    CMString full = db32("tierstable") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    std::vector<CMString> order;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t /*port*/, const CMString& /*name*/)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            order.push_back(host);
            return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
        });

    ds_->read_raw_compressed(full);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "host_a");  // registration order preserved
    EXPECT_EQ(order[1], "host_b");
    mon.clear();
    ProcessInfo::instance()->set_worker_mode(false);
}

// ── DB Migration Redirect (resolve_migrated_path) ──

TEST_F(DataServiceTest, ResolveMigratedPath_NoMigration_ReturnsOriginal) {
    CMString path_a = test_dir_ + "/db_no_migrate";
    std::filesystem::create_directories(path_a);

    // 无 _MIGRATED_TO 文件 → 返回原 path
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_a);
}

TEST_F(DataServiceTest, ResolveMigratedPath_SingleHop_ReturnsTarget) {
    CMString path_a = test_dir_ + "/db_source";
    CMString path_b = test_dir_ + "/db_target";
    std::filesystem::create_directories(path_a);
    std::filesystem::create_directories(path_b);

    // 写 _MIGRATED_TO: A → B
    fly::DataService::write_migration_marker(path_a, path_b, path_b + "/data");

    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);
}

TEST_F(DataServiceTest, ResolveMigratedPath_ChainedHop_Flattened) {
    // 链式迁移 A → B → C，resolve(A) 应返回 C（展平）
    CMString path_a = test_dir_ + "/chain_a";
    CMString path_b = test_dir_ + "/chain_b";
    CMString path_c = test_dir_ + "/chain_c";
    for (const auto& p : {path_a, path_b, path_c}) {
        std::filesystem::create_directories(p);
    }

    fly::DataService::write_migration_marker(path_a, path_b, path_b + "/data");
    fly::DataService::write_migration_marker(path_b, path_c, path_c + "/data");

    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_c);
}

TEST_F(DataServiceTest, ResolveMigratedPath_CachedOnSecondCall) {
    CMString path_a = test_dir_ + "/db_cached_src";
    CMString path_b = test_dir_ + "/db_cached_tgt";
    std::filesystem::create_directories(path_a);
    std::filesystem::create_directories(path_b);

    fly::DataService::write_migration_marker(path_a, path_b, path_b + "/data");

    // 第一次：stat 文件解析
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);
    // 删除 _MIGRATED_TO 文件，第二次应仍返回缓存值（证明走了缓存）
    std::filesystem::remove(path_a + "/_MIGRATED_TO");
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);
}

TEST_F(DataServiceTest, SetMigratedPath_UpdatesCache) {
    CMString path_a = test_dir_ + "/db_set_src";
    CMString path_b = test_dir_ + "/db_set_tgt";
    std::filesystem::create_directories(path_a);
    std::filesystem::create_directories(path_b);

    // 主动设置缓存（merge 后 master 调用，不写文件）
    ds_->set_migrated_path(path_a, path_b);
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);

    // 清除缓存
    ds_->set_migrated_path(path_a, "");
    // 清除后 resolve 会 stat 文件（无 _MIGRATED_TO）→ 返回原 path
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_a);
}

// issue 007 — Problem 3（中）：on_write_started 在重复检测前覆盖 COMPLETE 条目。
// 重复写路径上，对象已存在的 COMPLETE local_idx 条目（含 entries_）被新的 INCOMPLETE
// 无条件覆盖（丢弃 entries_），随后 on_write_failed 又 erase → 等待该对象的本地读取者
// 掉落到 TIER2/远程（数据其实已 COMPLETE 在盘上）。修复：on_write_started 拒绝覆盖
// 已 COMPLETE 的条目（仅 absent/INCOMPLETE/FAILED 时写入）。
TEST(DataServiceWriteLifecycleTest, OnWriteStartedDoesNotClobberCompleteEntry) {
    auto ds = fly::DataService::instance();
    CMString db = db32("p3_clobber");
    CMString full = db + ":obj_p3";
    ds->remove_local_index(full);

    // 1) 首次 write_started → INCOMPLETE（无 entries）
    ds->on_write_started(db, full);
    auto e0 = ds->find_local_entries(full);
    ASSERT_TRUE(e0.has_value());
    EXPECT_TRUE(e0->empty());

    // 2) write_completed → COMPLETE，写入 2 个 entries
    fly::CMVector<IndexEntry> entries;
    {
        IndexEntry e1; e1.object_name_ = "obj_p3"; e1.file_name_ = "f1"; e1.offset_ = 0;  e1.size_ = 10;
        IndexEntry e2; e2.object_name_ = "obj_p3"; e2.file_name_ = "f2"; e2.offset_ = 10; e2.size_ = 20;
        entries.push_back(e1); entries.push_back(e2);
    }
    ds->on_write_completed(db, full, entries);
    auto e_done = ds->find_local_entries(full);
    ASSERT_TRUE(e_done.has_value());
    EXPECT_EQ(e_done->size(), 2u);

    // 3) 重复写路径：再次 write_started。修复前用 INCOMPLETE 覆盖 COMPLETE（entries 丢失）；
    //    修复后保留 COMPLETE 条目。这是 Problem 3 的核心断言。
    ds->on_write_started(db, full);
    auto e_after = ds->find_local_entries(full);
    ASSERT_TRUE(e_after.has_value()) << "条目仍应存在";
    EXPECT_EQ(e_after->size(), 2u)
        << "on_write_started 不应覆盖已 COMPLETE 条目的 entries（Problem 3：重复写覆盖导致读取者掉落 TIER2）";

    ds->remove_local_index(full);
}

// ── 权威 remote_idx 保护（断连重连 G2，用户确认语义）────────────────────
// master 进程的 remote_idx 是全集群唯一位置权威源——读失败踢副本是 worker 本地
// 视图的自愈行为，master 进程豁免（worker 断连/挂掉不代表数据消失，权威视图
// 不被读失败污染，否则 worker 重连后 master 再也找不到数据）。
TEST_F(DataServiceTest, MasterProcessKeepsAuthoritativeLocationOnReadFailure) {
    auto ds = fly::DataService::instance();
    ProcessInfo::instance()->set_worker_mode(false);  // master 进程语义（测试进程默认即此）
    CMString obj = "/auth_db:obj";
    ds->update_remote_idx(obj, 7, "127.0.0.1", 9000);

    // master 进程：读失败踢副本 → no-op（位置保留）。
    ds->remove_remote_location(obj, 7);
    auto holders = ds->get_remote_workers(obj);
    ASSERT_EQ(holders.size(), 1u);
    EXPECT_EQ(holders[0], 7u);

    // worker 进程：踢副本保留（本地视图自愈）。
    ProcessInfo::instance()->set_worker_mode(true);
    ds->remove_remote_location(obj, 7);
    holders = ds->get_remote_workers(obj);
    EXPECT_TRUE(holders.empty()) << "worker-process local view eviction must keep working";
    ProcessInfo::instance()->set_worker_mode(false);

    ds->remove_remote_index(obj);
}

// 反查接口：get_objects_of_worker 返回该 worker 持有的全部对象全名。
TEST_F(DataServiceTest, GetObjectsOfWorkerReverseLookup) {
    auto ds = fly::DataService::instance();
    CMString a = "/rev:a", b = "/rev:b";
    ds->update_remote_idx(a, 11, "127.0.0.1", 1);
    ds->update_remote_idx(b, 11, "127.0.0.1", 1);
    ds->update_remote_idx(b, 12, "127.0.0.1", 1);  // b 双持有

    auto objs = ds->get_objects_of_worker(11);
    EXPECT_EQ(objs.size(), 2u);
    auto objs12 = ds->get_objects_of_worker(12);
    ASSERT_EQ(objs12.size(), 1u);
    EXPECT_EQ(objs12[0], b);

    ds->remove_remote_index(a);
    ds->remove_remote_index(b);
}

}
