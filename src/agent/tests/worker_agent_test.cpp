#include <gtest/gtest.h>
#include <agent/cpp/worker_agent.h>
#include <common/cpp/test_helpers.h>
#include <thread>
#include <chrono>

using namespace fly::test;

static CMString db32(const CMString& hint) {
    CMString r = hint;
    r.resize(fly::db_id_len(), '_');
    return r;
}

namespace fly {

TEST(WorkerAgentTest, CreateWithId) {
    WorkerAgent worker(42, "127.0.0.1", 0);
    EXPECT_EQ(worker.get_worker_id(), 42);
}

// New contract (post connect-non-fatal refactor): when master is unreachable,
// start() fails cleanly — worker does NOT enter a live-but-unregistered state.
// running_ stays false, no reactor/data-server created, stop() is a no-op.
TEST(WorkerAgentTest, StartWithoutMaster) {
    WorkerAgent worker(1, "127.0.0.1", 0);  // port 0 = no master
    worker.start();

    // Must not be running: connection failure aborts start().
    EXPECT_FALSE(worker.is_running());
    EXPECT_FALSE(worker.is_registered());

    // stop() on a never-started worker must be safe (no-op via guard).
    worker.stop();
    EXPECT_FALSE(worker.is_running());
}

TEST(WorkerAgentTest, SetExecutor) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    
    worker.set_executor(nullptr);
}

TEST(WorkerAgentContextTest, DefaultNotActive) {
    EXPECT_FALSE(WorkerAgentContext::is_active());
}

TEST(WorkerAgentContextTest, SetAndClear) {
    int calls = 0;
    WorkerAgentContext::set_record_write_func(
        [&calls](const CMString& db_id, const CMString& name, int64_t size) {
            calls++;
        }
    );
    EXPECT_TRUE(WorkerAgentContext::is_active());

    WorkerAgentContext::record_write(db32("db1"), "obj1", 100);
    EXPECT_EQ(calls, 1);

    WorkerAgentContext::clear();
    EXPECT_FALSE(WorkerAgentContext::is_active());
    WorkerAgentContext::record_write(db32("db1"), "obj2", 100);
    EXPECT_EQ(calls, 1);
}

TEST(WorkerAgentTest, BeginEndTaskTracking) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    
    worker.begin_task(42);
    EXPECT_TRUE(WorkerAgentContext::is_active());
    
    auto writes = worker.end_task(42);
    EXPECT_FALSE(WorkerAgentContext::is_active());
    EXPECT_TRUE(writes.empty());
}

TEST(WorkerAgentTest, RecordWrites) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_abc = db32("db_abc");
    
    worker.begin_task(100);
    worker.record_write(db_abc, "output/result", 200);
    worker.record_write(db_abc, "output/intermediate", 300);
    auto writes = worker.end_task(100);
    
    EXPECT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0], db_abc + ":output/result");
    EXPECT_EQ(writes[1], db_abc + ":output/intermediate");
}

TEST(WorkerAgentTest, MultipleTasksSequential) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db1 = db32("db1");
    CMString db2 = db32("db2");

    worker.begin_task(1);
    worker.record_write(db1, "a", 50);
    auto writes1 = worker.end_task(1);
    EXPECT_EQ(writes1.size(), 1u);

    worker.begin_task(2);
    worker.record_write(db2, "b", 60);
    auto writes2 = worker.end_task(2);
    EXPECT_EQ(writes2.size(), 1u);
    EXPECT_EQ(writes2[0], db2 + ":b");
}

TEST(WorkerAgentTest, WriteTrackingWithDatabase) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_hash_aaa = db32("db_hash_aaa");

    worker.begin_task(200);

    worker.record_write(db_hash_aaa, "output/result", 100);
    worker.record_write(db_hash_aaa, "output/log", 100);

    auto writes = worker.end_task(200);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0], db_hash_aaa + ":output/result");
    EXPECT_EQ(writes[1], db_hash_aaa + ":output/log");
}

TEST(WorkerAgentTest, MultiDbSameObjectNameTracking) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_proj_a = db32("db_proj_a");
    CMString db_proj_b = db32("db_proj_b");

    worker.begin_task(300);

    worker.record_write(db_proj_a, "output/result", 100);
    worker.record_write(db_proj_b, "output/result", 100);

    auto writes = worker.end_task(300);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_NE(writes[0], writes[1]);
    EXPECT_EQ(writes[0], db_proj_a + ":output/result");
    EXPECT_EQ(writes[1], db_proj_b + ":output/result");
}

TEST(WorkerAgentTest, EndTaskClearsTracking) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db1 = db32("db1");

    worker.begin_task(1);
    worker.record_write(db1, "obj1", 100);
    auto writes1 = worker.end_task(1);
    EXPECT_EQ(writes1.size(), 1u);

    worker.begin_task(2);
    auto writes2 = worker.end_task(2);
    EXPECT_TRUE(writes2.empty());
}

TEST(WorkerAgentTest, SetWorkerPropertySingle) {
    WorkerAgent worker(1, "127.0.0.1", 0, {});

    auto props = worker.get_worker_properties();
    EXPECT_TRUE(props.empty());

    worker.set_worker_property("gpu");
    props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0], "gpu");
}

TEST(WorkerAgentTest, SetWorkerPropertyBatch) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python"});

    worker.set_worker_property(CMVector<CMString>{"gpu", "cuda"});
    auto props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 3u);
}

TEST(WorkerAgentTest, SetWorkerPropertyDeduplicate) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python"});

    worker.set_worker_property("python");
    auto props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 1u);
}

TEST(WorkerAgentTest, RemoveWorkerPropertySingle) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python", "gpu"});

    worker.remove_worker_property("gpu");
    auto props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0], "python");
}

TEST(WorkerAgentTest, RemoveWorkerPropertyBatch) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python", "gpu", "cuda"});

    worker.remove_worker_property(CMVector<CMString>{"gpu", "cuda"});
    auto props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0], "python");
}

TEST(WorkerAgentTest, RemoveWorkerPropertyNonexistent) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python"});

    worker.remove_worker_property("nonexistent");
    auto props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 1u);
}

TEST(WorkerAgentTest, GetWorkerPropertiesReturnsCopy) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python"});

    auto props1 = worker.get_worker_properties();
    worker.set_worker_property("gpu");
    auto props2 = worker.get_worker_properties();

    EXPECT_EQ(props1.size(), 1u);
    EXPECT_EQ(props2.size(), 2u);
}

// Double-stop safety on a failed-start worker: start() aborted (no master),
// then explicit stop() + destructor stop() must not crash.
TEST(WorkerAgentTest, DoubleStopNoCrash) {
    // start() fails -> running_=false, reactor_=nullptr
    // stop() guard `if (!reactor_ && !running_) return;` makes it a no-op.
    // Destructor calls stop() again — must still be safe.
    {
        WorkerAgent worker(1, "127.0.0.1", 0);
        worker.start();
        EXPECT_FALSE(worker.is_running());

        worker.stop();
        EXPECT_FALSE(worker.is_running());
        // Destructor double-stop when scope exits — must not crash.
    }
    // If we reach here, destructor double-stop on failed-start succeeded.
}

TEST(WorkerAgentTest, StopBeforeStartNoCrash) {
    // Calling stop() without start() — should be safe
    WorkerAgent worker(1, "127.0.0.1", 0);
    EXPECT_NO_THROW(worker.stop());
}

TEST(WorkerAgentTest, GetDatabaseUnknownReturnsNull) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString unknown = db32("nonexistent_db");
    EXPECT_EQ(worker.get_database(unknown), nullptr);
}

TEST(WorkerAgentTest, RequestDatabaseFreezeNotRegistered) {
    // Worker not started → registered_ is false → request_database_freeze returns early
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_id = db32("no_reg_db");
    EXPECT_NO_THROW(worker.request_database_freeze(db_id));
}

// submit_task on a worker whose start() failed (no reactor) must not crash.
// New contract: submit_task guards reactor_==nullptr and returns softly.
TEST(WorkerAgentTest, SubmitTaskAfterFailedStart) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    worker.start();
    EXPECT_FALSE(worker.is_running());

    // No reactor exists — submit_task must not dereference null; logs + returns.
    EXPECT_NO_THROW(worker.submit_task("test_task", "test_module", {}, {}));
    worker.stop();
}

}  // namespace fly

#include <storage/cpp/data_service.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/index_entry.h>
#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <cstdio>

namespace fly {

static void create_test_idx_file(const CMString& base_path, const CMString& writer_id,
                                  const CMVector<IndexEntry>& entries) {
    CMString idx_path = base_path + "/" + writer_id + ".idx";
    LocalIndex idx(idx_path);
    for (const auto& e : entries) {
        idx.add_entry(e);
    }
    idx.save();
}

static CMString make_temp_dir(const CMString& suffix) {
    CMString dir = "/tmp/fly_idx_test_" + std::to_string(::getpid()) + "_" + suffix;
    std::filesystem::create_directories(dir);
    return dir;
}

class IdxLoadTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<fly::DataService> ds_ = fly::DataService::instance();

    void SetUp() override {
        test_dir_ = make_temp_dir("idxload");
        Logger::shutdown();
        Logger::init("test_logs/idxload", 0);
        ds_->stop_data_server();
        WorkerAgentContext::clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void TearDown() override {
        ds_->stop_data_server();
        WorkerAgentContext::clear();
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(IdxLoadTest, WorkerProcessesSingleIdxFile) {
    CMString db_id = db32("test_db");
    IndexEntry entry;
    entry.object_name_ = db_id + ":obj_alpha";
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    create_test_idx_file(test_dir_, "w0000005", {entry});

    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_id, test_dir_, {"w0000005"});
    wait_for([&]{ return ds_->has_local_object(db_id + ":obj_alpha"); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(db_id + ":obj_alpha"));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
    ds_->remove_local_index(db_id + ":obj_alpha");
}

TEST_F(IdxLoadTest, WorkerProcessesMultipleIdxFiles) {
    CMString db_id = db32("test_db");
    CMString full_one = db_id + ":obj_one";
    CMString full_two = db_id + ":obj_two";

    IndexEntry e1;
    e1.object_name_ = full_one;
    e1.file_name_ = "data_10.bin";
    e1.offset_ = 0;
    e1.size_ = 50;

    IndexEntry e2;
    e2.object_name_ = full_two;
    e2.file_name_ = "data_20.bin";
    e2.offset_ = 0;
    e2.size_ = 75;

    create_test_idx_file(test_dir_, "w0000010", {e1});
    create_test_idx_file(test_dir_, "w0000020", {e2});

    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_id, test_dir_, {"w0000010", "w0000020"});
    wait_for([&]{ return ds_->has_local_object(full_one) && ds_->has_local_object(full_two); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(full_one));
    EXPECT_TRUE(ds_->has_local_object(full_two));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
    ds_->remove_local_index(full_one);
    ds_->remove_local_index(full_two);
}

TEST_F(IdxLoadTest, WorkerSkipsMissingIdxFiles) {
    CMString db_id = db32("test_db");
    CMString full = db_id + ":obj_exists";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "data_5.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    create_test_idx_file(test_dir_, "w0000005", {entry});

    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_id, test_dir_, {"w0000005", "w0000099"});
    wait_for([&]{ return ds_->has_local_object(full); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(full));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
    ds_->remove_local_index(full);
}

TEST_F(IdxLoadTest, WorkerHandlesEmptyOldWorkerIds) {
    CMString db_id = db32("test_db");
    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_id, test_dir_, {});
    wait_for_running(master, true);

    EXPECT_TRUE(worker.is_running());

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
}

TEST_F(IdxLoadTest, WorkerHandlesEmptyIdxFile) {
    CMString db_id = db32("test_db");
    CMString idx_path = test_dir_ + "/w0000030.idx";
    {
        std::ofstream ofs(idx_path, std::ios::binary);
    }

    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_id, test_dir_, {"w0000030"});
    wait_for_running(master, true);

    EXPECT_TRUE(worker.is_running());

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
}

TEST_F(IdxLoadTest, WorkerLoadsMultipleEntriesPerIdx) {
    CMString db_id = db32("test_db");
    CMString full_a = db_id + ":block_a";
    CMString full_b = db_id + ":block_b";

    IndexEntry e1;
    e1.object_name_ = full_a;
    e1.file_name_ = "data_40.bin";
    e1.offset_ = 0;
    e1.size_ = 50;

    IndexEntry e2;
    e2.object_name_ = full_a;
    e2.file_name_ = "data_40.bin";
    e2.offset_ = 50;
    e2.size_ = 50;

    IndexEntry e3;
    e3.object_name_ = full_b;
    e3.file_name_ = "data_40.bin";
    e3.offset_ = 100;
    e3.size_ = 30;

    create_test_idx_file(test_dir_, "w0000040", {e1, e2, e3});

    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_id, test_dir_, {"w0000040"});
    wait_for([&]{ return ds_->has_local_object(full_a) && ds_->has_local_object(full_b); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(full_a));
    EXPECT_TRUE(ds_->has_local_object(full_b));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
    ds_->remove_local_index(full_a);
    ds_->remove_local_index(full_b);
}

TEST_F(IdxLoadTest, OnRemoveCommandExtractsShortName) {
    CMString db_id = db32("remove_cmd_test");
    CMString full = db_id + ":target_obj";
    CMString base_path = test_dir_ + "/remove_cmd_db";
    std::filesystem::create_directories(base_path);

    auto db = CMMakeShared<Database>(base_path, base_path + "/data", 0, "", db_id);
    db->write_pickle_bytes("target_obj", "remove_test_data", 16, "bytes", false);
    fly::DataService::instance()->drain_write_back();

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.register_database(db_id, db);
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    entry.is_large_ = false;
    entry.block_count_ = 0;
    ds_->on_object_written(db_id, full, entry);
    ds_->on_flush(db_id);
    ASSERT_TRUE(ds_->has_local_object(full));

    ds_->update_remote_idx(full, 1, "127.0.0.1", master.get_data_server_port());

    worker.request_object_remove(db_id, "target_obj");

    EXPECT_FALSE(ds_->has_local_object(full));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
}

TEST(WorkerAgentTest, RegisterAndGetDatabase) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_id = db32("reg_db");
    CMString base_path = make_temp_dir("reg_db");
    auto db = CMMakeShared<Database>(base_path, base_path + "/data", 0, "", db_id);
    worker.register_database(db_id, db);

    auto retrieved = worker.get_database(db_id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->get_db_id(), db_id);

    EXPECT_EQ(worker.get_database(db32("unknown")), nullptr);

    std::filesystem::remove_all(base_path);
}

TEST(WorkerAgentTest, RegisterDatabaseOverwritesExisting) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_id = db32("over_db");
    CMString base1 = make_temp_dir("over1");
    CMString base2 = make_temp_dir("over2");

    auto db1 = CMMakeShared<Database>(base1, base1 + "/data", 0, "", db_id);
    auto db2 = CMMakeShared<Database>(base2, base2 + "/data", 0, "", db_id);

    worker.register_database(db_id, db1);
    EXPECT_EQ(worker.get_database(db_id), db1);

    worker.register_database(db_id, db2);
    EXPECT_EQ(worker.get_database(db_id), db2);

    std::filesystem::remove_all(base1);
    std::filesystem::remove_all(base2);
}

TEST_F(IdxLoadTest, OnDbPathResponseSuccess) {
    CMString db_id = db32("pathresp_ok");
    CMString base_path = test_dir_ + "/pathresp_db";
    std::filesystem::create_directories(base_path);

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, base_path, base_path + "/data");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    bool result = false;
    std::thread t([&] { result = worker.request_db_path(db_id); });
    t.join();

    EXPECT_TRUE(result);
    EXPECT_NE(worker.get_database(db_id), nullptr);

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnDbPathResponseFailure) {
    CMString unknown_db = db32("pathresp_fail");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    bool result = true;
    std::thread t([&] { result = worker.request_db_path(unknown_db); });
    t.join();

    EXPECT_FALSE(result);
    EXPECT_EQ(worker.get_database(unknown_db), nullptr);

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnDatabaseFreezeNotification) {
    CMString db_id = db32("freeze_ntf");
    CMString base_path = test_dir_ + "/freeze_ntf_db";
    std::filesystem::create_directories(base_path);

    auto db = CMMakeShared<Database>(base_path, base_path + "/data", 0, "", db_id);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.register_database(db_id, db);
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_id);
    wait_for([&] { return db->is_frozen(); }, 50, 20);
    EXPECT_TRUE(db->is_frozen());

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnDatabaseFreezeNotificationAlreadyFrozen) {
    CMString db_id = db32("freeze_twice");
    CMString base_path = test_dir_ + "/freeze2_db";
    std::filesystem::create_directories(base_path);

    auto db = CMMakeShared<Database>(base_path, base_path + "/data", 0, "", db_id);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.register_database(db_id, db);
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_id);
    wait_for([&] { return db->is_frozen(); }, 50, 20);
    ASSERT_TRUE(db->is_frozen());

    worker.request_database_freeze(db_id);
    wait_for([&] { return true; }, 5, 20);
    EXPECT_TRUE(db->is_frozen());

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnObjectRemovedHandler) {
    CMString db_id = db32("objrm_hdl");
    CMString full = db_id + ":target_obj";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->register_database(db_id, test_dir_, test_dir_ + "/data");
    ds_->on_object_written(db_id, full, entry);
    ds_->on_flush(db_id);
    ASSERT_TRUE(ds_->has_local_object(full));

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.broadcast_object_removed(db_id, "target_obj");
    wait_for([&] { return !ds_->has_local_object(full); }, 50, 20);
    EXPECT_FALSE(ds_->has_local_object(full));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_id);
}

TEST_F(IdxLoadTest, OnShutdownViaMasterStop) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.stop();
    wait_for([&] { return !worker.is_running(); }, 50, 20);
    EXPECT_FALSE(worker.is_running());
}

TEST_F(IdxLoadTest, OnWriteRegisterAckSuccess) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString db_id = db32("writereg_ok");
    auto [msg, err_type] = worker.register_write_with_master(db_id, "obj1", 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnWriteRegisterAckFailure) {
    CMString db_id = db32("writereg_fail");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_id);
    wait_for([&] { return true; }, 5, 20);

    auto [msg, err_type] = worker.register_write_with_master(db_id, "obj_fail", 100);
    EXPECT_NE(err_type, TaskErrorType::UNKNOWN);
    EXPECT_FALSE(msg.empty());

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, InitiateShutdownFromOnDisconnect_ThenStop_CleansUp) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));
    EXPECT_TRUE(worker.is_running());

    master.stop();
    wait_for_running(master, false);

    wait_for([&] { return !worker.is_running(); }, 50, 20);

    worker.stop();

    EXPECT_FALSE(worker.is_running());

    fly::DataService::instance()->stop_data_server();
}

TEST(WorkerAgentTest, BeginTaskWithWriteContextHash) {
    WorkerAgent worker(1, "127.0.0.1", 0);

    worker.begin_task(42, "hash_abc");
    EXPECT_TRUE(WorkerAgentContext::is_active());

    auto writes = worker.end_task(42);
    EXPECT_TRUE(writes.empty());
    EXPECT_FALSE(WorkerAgentContext::is_active());
}

TEST(WorkerAgentTest, RecordWriteWithoutBeginEnd) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_id = db32("no_begin");

    worker.begin_task(1);
    worker.record_write(db_id, "output/data", 100);
    auto writes = worker.end_task(1);

    EXPECT_EQ(writes.size(), 1u);
    EXPECT_EQ(writes[0], db_id + ":output/data");
}

TEST(WorkerAgentTest, SetWorkerPropertyMultiple) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python"});

    worker.set_worker_property("gpu");
    worker.set_worker_property("cuda");
    worker.set_worker_property("python");

    auto props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 3);
}

TEST(WorkerAgentTest, RemoveAndSetWorkerProperty) {
    WorkerAgent worker(1, "127.0.0.1", 0, {"python", "gpu"});

    worker.remove_worker_property("gpu");
    worker.set_worker_property("cuda");

    auto props = worker.get_worker_properties();
    EXPECT_EQ(props.size(), 2);

    bool has_python = false, has_cuda = false;
    for (const auto& p : props) {
        if (p == "python") has_python = true;
        if (p == "cuda") has_cuda = true;
    }
    EXPECT_TRUE(has_python);
    EXPECT_TRUE(has_cuda);
}

TEST(WorkerAgentTest, HasPendingTaskEmpty) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    EXPECT_FALSE(worker.has_pending_task());
}

TEST(WorkerAgentTest, RequestBackupNotRegisteredNoop) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_id = db32("backup_db");
    EXPECT_NO_THROW(worker.request_backup(db_id, "obj"));
}

// ── DB Merge 集成测试 ───────────────────────────────────────────────
// 验证 fly.merge_db 的核心原语：master 派发 __merge_object task → target worker
// 跨机拉源对象 → 落到 target_data_path → master wait → send_delete_data 删源。
// 详见 docs/db-merge-design.md。
TEST_F(IdxLoadTest, MergeObjectEndToEnd) {
    CMString db_id = db32("merge_db_1");

    // 源 db：worker1 持有，data 落到 source_data_path（模拟源 host 本地盘）。
    CMString source_base = test_dir_ + "/source_db";
    CMString source_data = source_base + "/data";
    std::filesystem::create_directories(source_base);
    auto source_db = CMMakeShared<Database>(source_base, source_data, 0, "127.0.0.1", db_id);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker1(1, "127.0.0.1", master.get_port());
    worker1.register_database(db_id, source_db);
    worker1.start();
    ASSERT_TRUE(wait_until_registered(worker1));

    // 在 worker1 上写一个对象（落到 source_data_path）。
    const char* payload = "merge_payload_data_12345";
    ASSERT_EQ(source_db->write_pickle_bytes("merge_obj", payload, 22, "bytes", false),
              fly::WriteErrorType::OK);
    fly::DataService::instance()->drain_write_back();

    CMString full = db_id + ":merge_obj";
    ASSERT_TRUE(fly::DataService::instance()->has_local_object(full));

    // 手动登记 master remote_idx（让 target worker 的 read_raw_compressed 能找到源）。
    fly::DataService::instance()->update_remote_idx(
        full, 1, "127.0.0.1", master.get_data_server_port());

    // target worker（master host）：merge 落盘目标。
    CMString target_data_path = test_dir_ + "/merged_data";
    std::filesystem::create_directories(target_data_path);

    WorkerAgent worker2(2, "127.0.0.1", master.get_port());
    worker2.start();
    ASSERT_TRUE(wait_until_registered(worker2));

    // worker 的 task 执行依赖主循环 poll_task_blocking（真实环境由 fly/main.py 驱动）。
    // C++ 测试里手动起一个 poll 线程模拟。
    std::atomic<bool> poll_running{true};
    std::thread poll_thread([&]() {
        while (poll_running.load() && worker2.is_running()) {
            worker2.poll_task_blocking(100);
        }
    });

    // 派发 __merge_object task 给 worker2。
    uint64_t task_id = master.send_merge_task(
        /*target_worker_id=*/2, "merge_obj", db_id, source_base, target_data_path, "127.0.0.1");

    // 等待 merge task 完成。
    CMVector<CMString> completed;
    CMVector<CMString> failed;
    bool ok = master.wait_merge_tasks_complete({task_id}, 30, &completed, &failed);
    ASSERT_TRUE(ok) << "merge task failed: "
                    << (failed.empty() ? "<no detail>" : failed.front());
    ASSERT_EQ(completed.size(), 1u);
    EXPECT_EQ(completed[0], full);

    // 校验：target_data_path 下应有 .dat 文件，且 base_path 下 merge writer 的 idx 有 entry。
    bool has_dat = false;
    for (const auto& entry : std::filesystem::directory_iterator(target_data_path)) {
        if (entry.path().filename().string().substr(0, 5) == "data_") {
            has_dat = true;
            break;
        }
    }
    EXPECT_TRUE(has_dat) << "merge 未在 target_data_path 落盘 .dat";

    // master remote_idx 应已更新：对象现在 worker2 (id=2) 也有副本。
    auto holders = fly::DataService::instance()->get_remote_workers(full);
    bool worker2_has = false;
    for (auto w : holders) {
        if (w == 2) { worker2_has = true; break; }
    }
    EXPECT_TRUE(worker2_has) << "merge 完成后 master remote_idx 未登记 target worker";

    // 验证 target worker 能本地读到 merge 后的对象（local_idx 应有 entry）。
    // 注意：worker2 的 DataServer 读需要对象在 worker2 的 local_idx。
    // __merge_object 通过 register_write_with_master 登记给 master，但 worker2 本地
    // local_idx 的登记依赖 write_record 后的 DataService 状态。此处校验落盘文件即可
    // （remote_idx 已验证），local_idx 一致性留给 QA 测试。

    // ── 删源：master 命令 worker1 删除 source_data_path 下的 .dat ──
    // 先取 worker1 的 writer_id（用于 DeleteData 的 writer_ids 参数）。
    CMString src_writer_id = source_db->get_writer_id();
    master.send_delete_data(/*source_worker_id=*/1, db_id, source_base, {src_writer_id});

    // 等待 DeleteDataAck（轮询 pending 状态由 master 内部管理，这里通过校验文件删除间接确认）。
    bool source_deleted = false;
    for (int i = 0; i < 100 && !source_deleted; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        source_deleted = true;  // 假设删完，发现 .dat 则置 false
        for (const auto& entry : std::filesystem::directory_iterator(source_data)) {
            CMString fname = entry.path().filename().string();
            if (fname.substr(0, 5) == "data_" &&
                fname.size() >= 4 &&
                fname.substr(fname.size() - 4) == ".dat") {
                source_deleted = false;
                break;
            }
        }
    }
    EXPECT_TRUE(source_deleted) << "DeleteData 未删除源 .dat 文件";

    // ── 状态清理：master 广播 MergeCleanup + 清自身旧索引 + 精确重建 remote_idx ──
    master.cleanup_after_merge(
        db_id, {full}, /*source_worker_ids=*/{1}, /*merge_target_worker_ids=*/{2},
        source_base, target_data_path);
    // 给 worker 处理 MergeCleanup 一点时间。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 验证：master remote_idx 重建后，对象应只指向 merge target worker（worker_id=2），
    // 不再含源 worker（worker_id=1）。
    auto holders_after = fly::DataService::instance()->get_remote_workers(full);
    bool worker1_gone = true;
    bool worker2_present = false;
    for (auto w : holders_after) {
        if (w == 1) worker1_gone = false;
        if (w == 2) worker2_present = true;
    }
    EXPECT_TRUE(worker1_gone) << "cleanup 后 master remote_idx 仍残留源 worker replica";
    EXPECT_TRUE(worker2_present) << "cleanup 后 master remote_idx 未保留 merge target";

    worker2.stop();
    poll_running.store(false);
    if (poll_thread.joinable()) poll_thread.join();
    worker1.stop();
    master.stop();
    wait_for_running(master, false);

    fly::DataService::instance()->unregister_database(db_id);
    fly::DataService::instance()->remove_remote_index(full);
}

}  // namespace fly
