#include <gtest/gtest.h>
#include <agent/cpp/worker_agent.h>
#include <core/cpp/config.h>
#include <common/cpp/test_helpers.h>
#include "test_log_isolation.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <memory>

using namespace fly::test;

// db_path 废弃：db_path 现在是 db_path 别名（不含 ':'）。db32 生成不含 ':' 的测试 db_path。
static CMString db32(const CMString& hint) {
    return "/test/" + hint;
}

// 预占一个当前空闲的回环端口（bind(0) 读回后立即释放）。固定端口在并行
// 运行（bazel --runs_per_test / 多 target 并行）下必然互撞：后来者 bind
// 失败、worker 连上前一实例的 master 被判 duplicate（稳定性验证实测）。
// 用于需要「master 离开后回到同一地址」的测试——预占端口由 MasterAgent
// 构造函数携带，重启按构造端口 rebind；释放到再 bind 的窗口极短，竞争
// 概率是单次连接量级而非固定端口的确定性互撞。
static uint16_t allocate_free_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    uint16_t port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        socklen_t len = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            port = ntohs(addr.sin_port);
        }
    }
    ::close(fd);
    return port;
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
    // 默认保活 300s 会重试很久：显式设短窗口保持本用例"快速失败"语义。
    Config::instance()->set_int("worker_register_timeout", 1);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 10);
    WorkerAgent worker(1, "127.0.0.1", 0);  // port 0 = no master
    worker.start();
    Config::instance()->set_int("worker_register_timeout", 300);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);

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
        [&calls](const CMString& db_path, const CMString& name, int64_t size) {
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
    EXPECT_EQ(writes[0].full_name_, db_abc + ":output/result");
    EXPECT_EQ(writes[1].full_name_, db_abc + ":output/intermediate");
    // size 必须随 WriteRecord 正确携带（回归保护：原并行 map 在 end_task 中被
    // 清空，导致 TaskComplete 上报的 size 恒为 0）。
    EXPECT_EQ(writes[0].size_bytes_, 200);
    EXPECT_EQ(writes[1].size_bytes_, 300);
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
    EXPECT_EQ(writes2[0].full_name_, db2 + ":b");
}

TEST(WorkerAgentTest, WriteTrackingWithDatabase) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_hash_aaa = db32("db_hash_aaa");

    worker.begin_task(200);

    worker.record_write(db_hash_aaa, "output/result", 100);
    worker.record_write(db_hash_aaa, "output/log", 100);

    auto writes = worker.end_task(200);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0].full_name_, db_hash_aaa + ":output/result");
    EXPECT_EQ(writes[1].full_name_, db_hash_aaa + ":output/log");
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
    EXPECT_NE(writes[0].full_name_, writes[1].full_name_);
    EXPECT_EQ(writes[0].full_name_, db_proj_a + ":output/result");
    EXPECT_EQ(writes[1].full_name_, db_proj_b + ":output/result");
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
    Config::instance()->set_int("worker_register_timeout", 1);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 10);
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
    Config::instance()->set_int("worker_register_timeout", 300);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
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
    // 新语义（用户确认）：未注册窗口 freeze 挂起（pending 阻塞等注册确认），
    // 不再静默跳过——worker 终止时批量 fail 唤醒、联动 task 失败可见。
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_path = db32("no_reg_db");
    std::atomic<bool> done{false};
    std::thread freezer([&] {
        worker.request_database_freeze(db_path);
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(done.load()) << "freeze must block while unregistered";
    worker.fail_pending_freezes_for_testing();  // 模拟终止唤醒
    freezer.join();
    EXPECT_TRUE(done.load());
}

// submit_task on a worker whose start() failed (no reactor) must not crash.
// New contract: submit_task guards reactor_==nullptr and returns softly.
TEST(WorkerAgentTest, SubmitTaskAfterFailedStart) {
    Config::instance()->set_int("worker_register_timeout", 1);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 10);
    WorkerAgent worker(1, "127.0.0.1", 0);
    worker.start();
    EXPECT_FALSE(worker.is_running());

    // No reactor exists — submit_task must not dereference null; logs + returns.
    EXPECT_NO_THROW(worker.submit_task("test_task", "test_module", {}, {}));
    worker.stop();
    Config::instance()->set_int("worker_register_timeout", 300);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
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

static void create_test_idx_file(const CMString& db_path, const CMString& writer_id,
                                  const CMVector<IndexEntry>& entries) {
    CMString idx_path = db_path + "/" + writer_id + ".idx";
    LocalIndex idx(idx_path);
    for (const auto& e : entries) {
        idx.add_entry(e);
    }
    idx.save();
}

// 写侧恒流式（T2c 2026-08-31）：write_pickle_bytes 已删（仅测试调用的过期
// API）——造数原语统一 open_write_stream → write → finish_and_commit。
static void write_object_bytes(Database& db, const CMString& name,
                               const char* data, int64_t size,
                               bool backup = false) {
    std::unique_ptr<FlyStream> s(db.open_write_stream(name, "bytes"));
    ASSERT_NE(s, nullptr);
    s->write(data, static_cast<size_t>(size));
    ASSERT_EQ(static_cast<int>(s->finish_and_commit(backup, false)),
              static_cast<int>(fly::WriteErrorType::OK));
}

static CMString make_temp_dir(const CMString& suffix) {
    CMString dir = fly::test::qa_tmp_dir("fly_idx_test_" + suffix);
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
    CMString db_path = test_dir_;
    IndexEntry entry;
    entry.object_name_ = db_path + ":obj_alpha";
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    create_test_idx_file(test_dir_, "w0000005", {entry});

    ds_->register_database(db_path, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_path, {"w0000005"});
    wait_for([&]{ return ds_->has_local_object(db_path + ":obj_alpha"); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj_alpha"));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
    ds_->remove_local_index(db_path + ":obj_alpha");
}

TEST_F(IdxLoadTest, WorkerProcessesMultipleIdxFiles) {
    CMString db_path = test_dir_;
    CMString full_one = db_path + ":obj_one";
    CMString full_two = db_path + ":obj_two";

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

    ds_->register_database(db_path, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_path, {"w0000010", "w0000020"});
    wait_for([&]{ return ds_->has_local_object(full_one) && ds_->has_local_object(full_two); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(full_one));
    EXPECT_TRUE(ds_->has_local_object(full_two));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
    ds_->remove_local_index(full_one);
    ds_->remove_local_index(full_two);
}

TEST_F(IdxLoadTest, WorkerSkipsMissingIdxFiles) {
    CMString db_path = test_dir_;
    CMString full = db_path + ":obj_exists";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "data_5.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    create_test_idx_file(test_dir_, "w0000005", {entry});

    ds_->register_database(db_path, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_path, {"w0000005", "w0000099"});
    wait_for([&]{ return ds_->has_local_object(full); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(full));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
    ds_->remove_local_index(full);
}

TEST_F(IdxLoadTest, WorkerHandlesEmptyOldWorkerIds) {
    CMString db_path = test_dir_;
    ds_->register_database(db_path, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_path, {});
    wait_for_running(master, true);

    EXPECT_TRUE(worker.is_running());

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
}

TEST_F(IdxLoadTest, WorkerHandlesEmptyIdxFile) {
    CMString db_path = test_dir_;
    CMString idx_path = test_dir_ + "/w0000030.idx";
    {
        std::ofstream ofs(idx_path, std::ios::binary);
    }

    ds_->register_database(db_path, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_path, {"w0000030"});
    wait_for_running(master, true);

    EXPECT_TRUE(worker.is_running());

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
}

TEST_F(IdxLoadTest, WorkerLoadsMultipleEntriesPerIdx) {
    CMString db_path = test_dir_;
    CMString full_a = db_path + ":block_a";
    CMString full_b = db_path + ":block_b";

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

    ds_->register_database(db_path, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.send_idx_load_commands(db_path, {"w0000040"});
    wait_for([&]{ return ds_->has_local_object(full_a) && ds_->has_local_object(full_b); }, 100, 10);

    EXPECT_TRUE(ds_->has_local_object(full_a));
    EXPECT_TRUE(ds_->has_local_object(full_b));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
    ds_->remove_local_index(full_a);
    ds_->remove_local_index(full_b);
}

TEST_F(IdxLoadTest, OnRemoveCommandExtractsShortName) {
    CMString db_path = test_dir_ + "/remove_cmd_db";
    std::filesystem::create_directories(db_path);
    CMString full = db_path + ":target_obj";

    auto db = CMMakeShared<Database>(db_path, db_path + "/data", 0, "", db_path);
    write_object_bytes(*db, "target_obj", "remove_test_data", 16);
    fly::DataService::instance()->drain_write_back();

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.register_database(db_path, db);
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    entry.is_large_ = false;
    entry.block_count_ = 0;
    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);
    ASSERT_TRUE(ds_->has_local_object(full));

    ds_->update_remote_idx(full, 1, "127.0.0.1", master.get_data_server_port());

    worker.request_object_remove(db_path, "target_obj");

    EXPECT_FALSE(ds_->has_local_object(full));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
}

TEST(WorkerAgentTest, RegisterAndGetDatabase) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_path = make_temp_dir("reg_db");
    // db_path 废弃：db_path == db_path（不再外部指定），Database 构造用 db_path 注册
    auto db = CMMakeShared<Database>(db_path, db_path + "/data", 0, "", db_path);
    worker.register_database(db_path, db);

    auto retrieved = worker.get_database(db_path);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->get_db_path(), db_path);

    EXPECT_EQ(worker.get_database("/test/unknown"), nullptr);

    std::filesystem::remove_all(db_path);
}

TEST(WorkerAgentTest, RegisterDatabaseOverwritesExisting) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_path = db32("over_db");
    CMString base1 = make_temp_dir("over1");
    CMString base2 = make_temp_dir("over2");

    auto db1 = CMMakeShared<Database>(base1, base1 + "/data", 0, "", db_path);
    auto db2 = CMMakeShared<Database>(base2, base2 + "/data", 0, "", db_path);

    worker.register_database(db_path, db1);
    EXPECT_EQ(worker.get_database(db_path), db1);

    worker.register_database(db_path, db2);
    EXPECT_EQ(worker.get_database(db_path), db2);

    std::filesystem::remove_all(base1);
    std::filesystem::remove_all(base2);
}

TEST_F(IdxLoadTest, OnDbPathResponseSuccess) {
    CMString db_path = test_dir_ + "/pathresp_db";
    std::filesystem::create_directories(db_path);

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_path, db_path + "/data");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    bool result = false;
    std::thread t([&] { result = worker.request_db_path(db_path); });
    t.join();

    EXPECT_TRUE(result);
    EXPECT_NE(worker.get_database(db_path), nullptr);

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
    CMString db_path = test_dir_ + "/freeze_ntf_db";
    std::filesystem::create_directories(db_path);

    auto db = CMMakeShared<Database>(db_path, db_path + "/data", 0, "", db_path);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.register_database(db_path, db);
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_path);
    wait_for([&] { return db->is_frozen(); }, 50, 20);
    EXPECT_TRUE(db->is_frozen());

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnDatabaseFreezeNotificationAlreadyFrozen) {
    CMString db_path = test_dir_ + "/freeze2_db";
    std::filesystem::create_directories(db_path);

    auto db = CMMakeShared<Database>(db_path, db_path + "/data", 0, "", db_path);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.register_database(db_path, db);
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_path);
    wait_for([&] { return db->is_frozen(); }, 50, 20);
    ASSERT_TRUE(db->is_frozen());

    worker.request_database_freeze(db_path);
    wait_for([&] { return true; }, 5, 20);
    EXPECT_TRUE(db->is_frozen());

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnObjectRemovedHandler) {
    CMString db_path = db32("objrm_hdl");
    CMString full = db_path + ":target_obj";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 100;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->register_database(db_path, test_dir_ + "/data");
    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);
    ASSERT_TRUE(ds_->has_local_object(full));

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.broadcast_object_removed(db_path, "target_obj");
    wait_for([&] { return !ds_->has_local_object(full); }, 50, 20);
    EXPECT_FALSE(ds_->has_local_object(full));

    worker.stop();
    master.stop();
    wait_for_running(master, false);

    ds_->unregister_database(db_path);
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

    CMString db_path = db32("writereg_ok");
    auto [msg, err_type] = worker.register_write_with_master(db_path, "obj1", 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, OnWriteRegisterAckFailure) {
    CMString db_path = db32("writereg_fail");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_path);
    wait_for([&] { return true; }, 5, 20);

    auto [msg, err_type] = worker.register_write_with_master(db_path, "obj_fail", 100);
    EXPECT_NE(err_type, TaskErrorType::UNKNOWN);
    EXPECT_FALSE(msg.empty());

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST_F(IdxLoadTest, InitiateShutdownFromOnDisconnect_ThenStop_CleansUp) {
    // 逃生口语义（worker_reconnect_timeout=0）：断连即 shutdown（旧行为）。
    // 默认（>0）下断连走重连宽限（见 DisconnectReconnectsAndReports）。
    Config::instance()->set_int("worker_reconnect_timeout", 0);

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
    Config::instance()->set_int("worker_reconnect_timeout", 120);
}

TEST(WorkerAgentTest, BeginTaskWithWriteContextHash) {
    WorkerAgent worker(1, "127.0.0.1", 0);

    worker.begin_task(42, "hash_abc");
    EXPECT_TRUE(WorkerAgentContext::is_active());

    auto writes = worker.end_task(42);
    EXPECT_TRUE(writes.empty());
    EXPECT_FALSE(WorkerAgentContext::is_active());
}

// 未注册窗口内的写注册（用户确认语义）：直接 pending、task 阻塞，直到
// 注册确认（重放 + WriteRegisterAck 唤醒）或 worker 终止（批量 fail 唤醒）。
// 本地在线 master 的重连首轮毫秒级完成，阻塞时长不可稳定观测——分两段：
// 段一无网络（永无确认，阻塞窗口确定）验证 pending + 终止唤醒；
// 段二真网络验证完整正路径（断连 → pending → 重连重放 → 确认成功）。
TEST(WorkerAgentTest, WriteRegisterPendingBlocksUntilReconnected) {
    Config::instance()->set_int("worker_reconnect_timeout", 30);

    // ── 段一：无网络，pending 阻塞 + 终止唤醒 ──
    {
        WorkerAgent worker(9, "127.0.0.1", 0);  // 不 start：永无注册确认
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        std::atomic<int> err_type{0};
        std::thread writer([&] {
            auto [err, type] = worker.register_write_with_master_for_testing(
                db32("pend_alone"), "obj", 100);
            ok = err.empty();
            err_type = static_cast<int>(type);
            done = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_FALSE(done.load()) << "write register must block while unregistered";
        worker.fail_pending_write_regs_for_testing();  // 模拟 worker 终止
        writer.join();
        EXPECT_TRUE(done.load());
        EXPECT_FALSE(ok.load()) << "terminated worker's pending register must fail";
        // 终止时「注册未确认」按字面语义归类 WRITE_REGISTRATION_FAILED（原 UNKNOWN
        // 丢语义——task 失败上报与 database.cpp 映射链无从区分拒绝原因）。
        EXPECT_EQ(err_type.load(), static_cast<int>(TaskErrorType::WRITE_REGISTRATION_FAILED));
    }

    // ── 段二：真网络，断连 → pending → 重连重放 → 确认 ──
    {
        Config::instance()->set_int("worker_connect_retry_initial_ms", 50);
        MasterAgent master("127.0.0.1", 0);
        master.start();
        wait_for_running(master, true);

        WorkerAgent worker(1, "127.0.0.1", master.get_port());
        worker.start();
        ASSERT_TRUE(wait_until_registered(worker));

        worker.simulate_master_disconnect_for_testing();
        CMString full = db32("pend_reg") + ":obj";
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        std::thread writer([&] {
            auto [err, type] = worker.register_write_with_master_for_testing(
                db32("pend_reg"), "obj", 100);
            ok = err.empty();
            done = true;
        });

        // 重连（首轮毫秒级）→ Ack → 重放缓冲注册 → master 确认 → 唤醒成功。
        EXPECT_TRUE(wait_until_registered(worker));
        for (int i = 0; i < 100 && !done.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        // 失败路径先唤醒阻塞的 writer 再 join——ASSERT 提前返回会让 joinable
        // 线程析构触发 std::terminate，吞掉 logger 缓冲里的最后 1s 日志
        //（50 轮稳定性第 16 轮实测：崩溃毁掉失败现场）。
        if (!done.load()) {
            worker.fail_pending_write_regs_for_testing();
        }
        writer.join();
        ASSERT_TRUE(done.load()) << "blocked writer must wake after replay+ack";
        EXPECT_TRUE(ok.load()) << "replayed register should confirm successfully";
        EXPECT_FALSE(DataService::instance()->get_remote_workers(full).empty())
            << "master must have the object location after replay";

        worker.stop();
        master.stop();
        wait_for_running(master, false);
    }
    Config::instance()->set_int("worker_reconnect_timeout", 120);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// ── 串行域下的重连注册顺序（P3-26 架构收口）────────────────────────────
// REGISTER/WorkerProbeAck 与断连事件统一走 Reactor 顺序敏感域（保留串行 lane，
// 跨连接 FIFO）——worker 闪断重连时，REGISTER(新 conn) 与旧 conn 的 DISCONNECT
// 不再跨 lane 并行交错（P3-26 的 deferred 注册孤儿化根除）。
// 确定性验证：旧 conn 断连处理被入口钩子阻塞期间，重连 REGISTER 必须排队
// 等待、不得越过执行；放行后按序完成，注册成功、无孤儿。
TEST(WorkerAgentTest, ReconnectRegisterBeforeDisconnectProcessed) {
    Config::instance()->set_int("worker_reconnect_timeout", 30);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 50);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // 断连处理门闩：入口钩子报「已进入」后阻塞，等测试放行。
    std::mutex gate_m;
    std::condition_variable gate_cv;
    bool disconnect_entered = false;
    bool allow_disconnect = false;
    master.on_disconnect_entry_hook_for_testing_ = [&](uint64_t) {
        {
            std::lock_guard<std::mutex> lk(gate_m);
            disconnect_entered = true;
        }
        gate_cv.notify_all();
        std::unique_lock<std::mutex> lk(gate_m);
        gate_cv.wait(lk, [&] { return allow_disconnect; });
    };

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 闪断：master transport 已 reap 旧 conn，断连事件进入串行域被钩子阻塞。
    worker.simulate_master_disconnect_for_testing();

    bool entered = false;
    {
        std::unique_lock<std::mutex> lk(gate_m);
        entered = gate_cv.wait_for(lk, std::chrono::seconds(5),
                                   [&] { return disconnect_entered; });
    }
    ASSERT_TRUE(entered) << "master must dispatch the old-conn disconnect handler";

    // worker 重连（50ms 退避），REGISTER 到达 master——串行域内必须排在被阻塞
    // 的断连处理之后：有界窗口内不得注册成功（越过即串行域失效）。
    bool jumped = false;
    for (int i = 0; i < 80 && !jumped; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        jumped = worker.is_registered();
    }
    EXPECT_FALSE(jumped)
        << "REGISTER must NOT overtake the blocked DISCONNECT (serialized domain)";

    // 放行：断连处理完成 → 排队的 REGISTER 按序执行 → 注册成功（无孤儿）。
    {
        std::lock_guard<std::mutex> lk(gate_m);
        allow_disconnect = true;
    }
    gate_cv.notify_all();
    master.on_disconnect_entry_hook_for_testing_ = nullptr;

    EXPECT_TRUE(wait_until_registered(worker, 300, 10))
        << "queued register must complete after disconnect finishes";
    EXPECT_TRUE(worker.is_registered());
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("worker_reconnect_timeout", 120);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// ── 残留 RegisterAck 不得杀死重连（P3-27）────────────────────────────
// on_register_ack 与 on_disconnect 对 {registered_, reconnecting_} 的迁移必须
// 原子且残留 ack 必须被拒：旧代码 ack 处理器在 registered_=true 之后、
// reconnecting_ 清除之前被抢占、期间发生断连时，恢复后的 exchange(false) 清掉
// 重连标志——重连线程入口即静默退出，worker 永不重连（50 轮稳定性第六轮
// 第 5 轮实测，~1/19 复现；FORENSIC 实证 loop 入口 reconnecting=false）。
// 确定性构造：入口钩子 park 重连线程 → 注入「断连后残留 ack」（旧 conn 上的
// 在途 ack）→ 放行。修复前：ack 清标志，线程退出，永不重连（必红）；
// 修复后：残留 ack 被 conn 存活检查拒绝（必绿）。
TEST(WorkerAgentTest, RegisterAckDisconnectInterleaveKeepsReconnectAlive) {
    Config::instance()->set_int("worker_reconnect_timeout", 30);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 50);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 入口钩子：park 重连线程（信号 + 闸门）。
    std::mutex hk_m;
    std::condition_variable hk_cv;
    bool reconnect_entered = false;
    bool release_reconnect = false;
    worker.reconnect_entry_hook_for_testing_ = [&] {
        {
            std::lock_guard<std::mutex> lk(hk_m);
            reconnect_entered = true;
        }
        hk_cv.notify_all();
        std::unique_lock<std::mutex> lk(hk_m);
        hk_cv.wait(lk, [&] { return release_reconnect; });
    };

    // 闪断：on_disconnect 完成（reconnecting_=true）→ 重连线程入口 park。
    worker.simulate_master_disconnect_for_testing();
    {
        std::unique_lock<std::mutex> lk(hk_m);
        ASSERT_TRUE(hk_cv.wait_for(lk, std::chrono::seconds(5),
                                   [&] { return reconnect_entered; }))
            << "reconnect thread must reach its entry hook";
    }

    // 注入断连后残留 ack（旧 conn=1 上的在途 ack 迟到到达）。
    RegisterAckMessage stale_ack;
    stale_ack.worker_id_ = 1;
    stale_ack.duplicate_ = false;
    worker.on_register_ack_for_testing(/*conn_id=*/1, stale_ack);

    // 放行重连线程：修复前它读到 reconnecting_=false 静默退出、且残留 ack 已
    // 伪造 registered_=true（ghost 注册态，仅查 registered_ 无法区分）；修复后
    // 残留 ack 被拒、标志完好，照常重连注册。
    {
        std::lock_guard<std::mutex> lk(hk_m);
        release_reconnect = true;
    }
    hk_cv.notify_all();
    // hook 不清空（避免与重连线程的 std::function 读写在无同步下竞争）：
    // release 标志已置位，后续任何触发都直接放行。

    // 功能级判定：重连必须真实发生——master 能把 task 派达 worker 并收到
    // 上报（端到端证据，ghost 注册态伪造不出）。先等重注册真正完成（修复版
    // 下残留 ack 被拒，此处只会被真实 ack 满足），再提交 task（单测 worker
    // 需手动驱动执行）。
    EXPECT_TRUE(wait_until_registered(worker, 300, 10))
        << "reconnect must complete after stale-ack rejection";
    master.submit_task(61, "bogus", "__fly_internal", {"a"}, {}, {});
    wait_for([&] { return worker.has_pending_task(); }, 50, 20);
    ASSERT_TRUE(worker.has_pending_task()) << "task must reach the reconnected worker";
    worker.poll_task();
    bool reached = false;
    wait_for([&] {
        for (uint64_t id : master.get_failed_tasks()) {
            if (id == 61) { reached = true; return true; }
        }
        return false;
    }, 100, 20);
    EXPECT_TRUE(reached) << "stale register-ack must not leave worker ghost-registered";

    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("worker_reconnect_timeout", 120);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// ── 启动中途的 duplicate 拒绝不得造成 zombie worker（P3-28）───────────
// master 的 duplicate 拒绝 ack 若在 worker start() 执行中途到达（REGISTER
// 发出后、线程 spawn 前），on_register_ack(duplicate) → initiate_shutdown 清
// 各线程标志；旧 start() 继续把 register_watchdog_running_ 置回 true 并 spawn
// （此后所有 initiate_shutdown 幂等早退，无人再清 → 永生线程），最后
// running_=true 复活已停机的 worker（is_running 永真）→ stop() join 永久挂死
// （50 轮稳定性第七轮第 29 轮实测 + gdb 栈证实）。确定性构造：post_register_send
// 钩子在 start() 线程内同步注入 duplicate ack。
TEST(WorkerAgentTest, DuplicateRejectionDuringStartupDoesNotZombify) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(7, "127.0.0.1", master.get_port());
    worker.post_register_send_hook_for_testing_ = [&worker] {
        RegisterAckMessage dup;
        dup.worker_id_ = 7;
        dup.duplicate_ = true;
        worker.on_register_ack_for_testing(/*conn_id=*/1, dup);
    };
    worker.start();

    // 停机不可复活：is_running 必须为 false（旧代码 running_=true 复活 → 永真）。
    EXPECT_FALSE(worker.is_running())
        << "shutdown during startup must not be resurrected by start() tail";
    // stop() 必须干净返回（旧代码 join 永生守望线程 → 永久挂死）。
    worker.stop();
    EXPECT_FALSE(worker.is_running());

    master.stop();
    wait_for_running(master, false);
}

TEST(WorkerAgentTest, RecordWriteWithoutBeginEnd) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    CMString db_path = db32("no_begin");

    worker.begin_task(1);
    worker.record_write(db_path, "output/data", 100);
    auto writes = worker.end_task(1);

    EXPECT_EQ(writes.size(), 1u);
    EXPECT_EQ(writes[0].full_name_, db_path + ":output/data");
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
    CMString db_path = db32("backup_db");
    EXPECT_NO_THROW(worker.request_backup(db_path, "obj"));
}

// ── DB Merge 集成测试 ───────────────────────────────────────────────
// 验证 fly.merge_db 的核心原语：master 派发 __merge_object task → target worker
// 跨机拉源对象 → 落到 target_data_path → master wait → send_delete_data 删源。
// 详见 docs/db-merge-design.md。
TEST_F(IdxLoadTest, MergeObjectEndToEnd) {
    // db_path 废弃：db_path == db_path。源 db 的 db_path 就是 source_base。
    CMString source_base = test_dir_ + "/source_db";
    CMString source_data = source_base + "/data";
    std::filesystem::create_directories(source_base);
    auto source_db = CMMakeShared<Database>(source_base, source_data, 0, "127.0.0.1", source_base);
    CMString db_path = source_db->get_db_path();  // == source_base

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker1(1, "127.0.0.1", master.get_port());
    worker1.register_database(db_path, source_db);
    worker1.start();
    ASSERT_TRUE(wait_until_registered(worker1));

    // 在 worker1 上写一个对象（落到 source_data_path）。
    const char* payload = "merge_payload_data_12345";
    write_object_bytes(*source_db, "merge_obj", payload, 22);
    fly::DataService::instance()->drain_write_back();

    CMString full = db_path + ":merge_obj";
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
        /*target_worker_id=*/2, "merge_obj", db_path, db_path, target_data_path, "127.0.0.1");

    // 等待 merge task 完成。
    CMVector<CMString> completed;
    CMVector<CMString> failed;
    bool ok = master.wait_merge_tasks_complete({task_id}, 30, &completed, &failed);
    ASSERT_TRUE(ok) << "merge task failed: "
                    << (failed.empty() ? "<no detail>" : failed.front());
    ASSERT_EQ(completed.size(), 1u);
    EXPECT_EQ(completed[0], full);

    // 校验：target_data_path 下应有 .dat 文件，且 db_path 下 merge writer 的 idx 有 entry。
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
    // __merge_object 不调 register_write_with_master（db 已 freeze 会被拒），而是通过
    // TaskComplete(is_internal_=true) 回报，master 的 on_task_complete internal 分支调
    // update_remote_idx 登记对象位置（已由上方 worker2_has 验证）。worker2 本地 local_idx
    // 的登记由 execute_merge_object 的 on_write_completed 完成（此处校验落盘文件即可，
    // local_idx 一致性留给 QA 多进程测试）。

    // ── 删源：master 命令 worker1 删除 source_data_path 下的 .dat ──
    // 先取 worker1 的 writer_id（用于 DeleteData 的 writer_ids 参数）。
    CMString src_writer_id = source_db->get_writer_id();
    master.send_delete_data(/*source_worker_id=*/1, db_path,
                            /*data_path=*/source_data, {src_writer_id});

    // 同步等待 DeleteDataAck（验证 ack 等待机制工作，且消除轮询的 flaky）。
    fly::CMVector<uint64_t> del_failed;
    bool del_ok = master.wait_delete_data_acks({1}, db_path, 10, &del_failed);
    EXPECT_TRUE(del_ok) << "DeleteData ack should succeed";
    EXPECT_TRUE(del_failed.empty()) << "no failed source workers expected";

    // 校验源 .dat 已删除（ack 成功即已删，这里二次确认文件确实没了）。
    bool source_deleted = true;
    for (const auto& entry : std::filesystem::directory_iterator(source_data)) {
        CMString fname = entry.path().filename().string();
        if (fname.substr(0, 5) == "data_" &&
            fname.size() >= 4 &&
            fname.substr(fname.size() - 4) == ".dat") {
            source_deleted = false;
            break;
        }
    }
    EXPECT_TRUE(source_deleted) << "DeleteData 未删除源 .dat 文件";

    // ── 状态清理：master 广播 MergeCleanup + 清自身旧索引 + 精确重建 remote_idx ──
    master.cleanup_after_merge(
        db_path, {full}, /*source_worker_ids=*/{1}, /*merge_target_worker_ids=*/{2},
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

    fly::DataService::instance()->unregister_database(db_path);
    fly::DataService::instance()->remove_remote_index(full);
}

// merge task 写盘失败必须走 TaskFailed（而非假 TaskComplete 导致 master
// remote_idx 指向无数据对象、merge_db 误判成功删源）。
// 注错钩子 merge_write_fail_for_testing_ 确定性驱动失败路径。
TEST_F(IdxLoadTest, MergeObjectWriteFailReportsTaskFailed) {
    CMString source_base = test_dir_ + "/fail_db";
    CMString source_data = source_base + "/data";
    std::filesystem::create_directories(source_base);
    auto source_db = CMMakeShared<Database>(source_base, source_data, 0, "127.0.0.1", source_base);
    CMString db_path = source_db->get_db_path();

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker1(1, "127.0.0.1", master.get_port());
    worker1.register_database(db_path, source_db);
    worker1.start();
    ASSERT_TRUE(wait_until_registered(worker1));

    const char* payload = "fail_payload_98765";
    write_object_bytes(*source_db, "fail_obj", payload, 18);
    fly::DataService::instance()->drain_write_back();
    CMString full = db_path + ":fail_obj";
    fly::DataService::instance()->update_remote_idx(
        full, 1, "127.0.0.1", master.get_data_server_port());

    CMString target_data_path = test_dir_ + "/fail_target_data";
    std::filesystem::create_directories(target_data_path);
    WorkerAgent worker2(2, "127.0.0.1", master.get_port());
    worker2.merge_write_fail_for_testing_.insert("fail_obj");  // 注错：模拟写盘失败
    worker2.start();
    ASSERT_TRUE(wait_until_registered(worker2));

    std::atomic<bool> poll_running{true};
    std::thread poll_thread([&]() {
        while (poll_running.load() && worker2.is_running()) {
            worker2.poll_task_blocking(100);
        }
    });

    uint64_t task_id = master.send_merge_task(2, "fail_obj", db_path, db_path,
                                              target_data_path, "127.0.0.1");
    CMVector<CMString> completed;
    CMVector<CMString> failed;
    bool ok = master.wait_merge_tasks_complete({task_id}, 10, &completed, &failed);

    EXPECT_FALSE(ok);                       // merge 必须判失败
    EXPECT_TRUE(completed.empty());         // 不允许假成功
    ASSERT_EQ(failed.size(), 1u);           // 失败原因带回对象信息
    EXPECT_NE(failed[0].find("fail_obj"), CMString::npos);

    // master 不应把对象登记到 target worker（remote_idx 无 worker2 副本）。
    for (auto w : fly::DataService::instance()->get_remote_workers(full)) {
        EXPECT_NE(w, 2u);
    }

    poll_running.store(false);
    if (poll_thread.joinable()) poll_thread.join();
    worker1.stop();
    worker2.stop();
    master.stop();
    wait_for_running(master, false);
    fly::DataService::instance()->unregister_database(db_path);
    fly::DataService::instance()->remove_remote_index(full);
}

// C3: cleanup_failed_merge 按 db 精确清理（跨 db 并发 merge 不互扰）。
// 未连接路径登记的失败状态即测试载体（completed_=true 保留至 cleanup）。
TEST_F(IdxLoadTest, CleanupFailedMergeClearsOnlyOwnDb) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    CMString db1 = db32("c3_db1");
    CMString db2 = db32("c3_db2");
    // target worker 不存在 → 未连接路径：登记 + 标失败 + 回滚 assign。
    master.send_merge_task(99, "obj_a", db1, db1, test_dir_ + "/t1", "127.0.0.1");
    master.send_merge_task(98, "obj_b", db2, db2, test_dir_ + "/t2", "127.0.0.1");
    EXPECT_EQ(master.merge_task_state_count_for_testing(db1), 1u);
    EXPECT_EQ(master.merge_task_state_count_for_testing(db2), 1u);

    master.cleanup_failed_merge(db1, db1, test_dir_ + "/t1");
    EXPECT_EQ(master.merge_task_state_count_for_testing(db1), 0u);  // 本 db 精确清
    EXPECT_EQ(master.merge_task_state_count_for_testing(db2), 1u);  // 其它 db 不动

    master.stop();
    wait_for_running(master, false);
}

// C3: purge 广播——merge target worker 收到后删除自己写的产物 .dat/.idx 并清
// target local_idx；源数据（源 worker 命名空间）不动。
// 场景：merge task 实际成功（产物已写），随后整体失败清理（模拟部分对象失败）。
TEST_F(IdxLoadTest, MergeFailedCleanupPurgesProducts) {
    CMString source_base = test_dir_ + "/purge_db";
    CMString source_data = source_base + "/data";
    std::filesystem::create_directories(source_base);
    auto source_db = CMMakeShared<Database>(source_base, source_data, 0, "127.0.0.1", source_base);
    CMString db_path = source_db->get_db_path();

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker1(1, "127.0.0.1", master.get_port());
    worker1.register_database(db_path, source_db);
    worker1.start();
    ASSERT_TRUE(wait_until_registered(worker1));

    write_object_bytes(*source_db, "purge_obj", "purge_payload_1", 15);
    fly::DataService::instance()->drain_write_back();
    CMString full = db_path + ":purge_obj";
    fly::DataService::instance()->update_remote_idx(
        full, 1, "127.0.0.1", master.get_data_server_port());

    CMString target_data_path = test_dir_ + "/purge_target_data";
    std::filesystem::create_directories(target_data_path);
    WorkerAgent worker2(2, "127.0.0.1", master.get_port());
    worker2.start();
    ASSERT_TRUE(wait_until_registered(worker2));

    std::atomic<bool> poll_running{true};
    std::thread poll_thread([&]() {
        while (poll_running.load() && worker2.is_running()) {
            worker2.poll_task_blocking(100);
        }
    });

    uint64_t task_id = master.send_merge_task(2, "purge_obj", db_path, db_path,
                                              target_data_path, "127.0.0.1");
    CMVector<CMString> completed;
    CMVector<CMString> failed;
    ASSERT_TRUE(master.wait_merge_tasks_complete({task_id}, 30, &completed, &failed));
    ASSERT_EQ(completed.size(), 1u);  // 产物已写出

    // 整体失败清理：purge 广播（异步）→ worker2 删产物。
    master.cleanup_failed_merge(db_path, db_path, target_data_path);

    // 等待 purge 生效（终态断言：产物 .dat 消失 + merge task 状态清零）。
    wait_for([&]() {
        bool has_dat = false;
        for (const auto& entry : std::filesystem::directory_iterator(target_data_path)) {
            if (entry.path().filename().string().substr(0, 5) == "data_") has_dat = true;
        }
        return !has_dat;
    }, 100, 20);
    bool has_dat = false;
    for (const auto& entry : std::filesystem::directory_iterator(target_data_path)) {
        if (entry.path().filename().string().substr(0, 5) == "data_") has_dat = true;
    }
    EXPECT_FALSE(has_dat) << "merge products should be purged from target_data_path";
    EXPECT_EQ(master.merge_task_state_count_for_testing(db_path), 0u);

    // 源数据保留（重 merge 支撑）：源 .dat 文件仍在 + master remote_idx 仍登记
    // 源 worker 位置。注：has_local_object 不适用于本单测——进程内 worker1/worker2
    // 共享同一 DataService，worker2 purge 清 target worker 的 local_idx_[db_path]
    //（真实多进程语义正确）在同进程里必然波及 worker1 的条目。
    bool source_dat = false;
    for (const auto& entry : std::filesystem::directory_iterator(source_data)) {
        CMString fname = entry.path().filename().string();
        if (fname.substr(0, 5) == "data_" && fname.size() >= 4 &&
            fname.substr(fname.size() - 4) == ".dat") {
            source_dat = true;
            break;
        }
    }
    EXPECT_TRUE(source_dat) << "source .dat must be preserved for re-merge";
    bool source_in_remote_idx = false;
    for (auto w : fly::DataService::instance()->get_remote_workers(full)) {
        if (w == 1) { source_in_remote_idx = true; break; }
    }
    EXPECT_TRUE(source_in_remote_idx) << "master remote_idx must keep source worker location";

    poll_running.store(false);
    if (poll_thread.joinable()) poll_thread.join();
    worker1.stop();
    worker2.stop();
    master.stop();
    wait_for_running(master, false);
    fly::DataService::instance()->unregister_database(db_path);
    fly::DataService::instance()->remove_remote_index(full);
}

// ── connect 指数退避重试 ──────────────────────────────────────────────
// 领域约束：master 挂=全群失败，重试只覆盖瞬时抖动与 master 短时过载；
// 总保活窗口与 master 占位符共用 worker_register_timeout（默认 300s）。

// 场景：master 短暂不可用（过载重启窗口）→ worker 退避重试 → master 回来后连上。
// 固定冷门端口保证 stop→start 重新 bind 同一端口。
TEST(WorkerAgentTest, ConnectRetrySucceedsWhenMasterReturns) {
    // 预占动态端口（曾固定 48765——并行运行必撞）：本测试要求 master
    // stop→start 回到同一地址，端口由构造函数携带、重启按构造端口 rebind。
    const uint16_t kPort = allocate_free_port();
    ASSERT_NE(kPort, 0u) << "free port allocation failed";
    Config::instance()->set_int("worker_register_timeout", 10);  // 10s 保活窗口
    Config::instance()->set_int("worker_connect_retry_initial_ms", 50);

    MasterAgent master("127.0.0.1", kPort);
    master.start();
    wait_for_running(master, true);
    master.stop();
    wait_for_running(master, false);   // 端口关闭：worker 第一轮 connect 必然失败

    WorkerAgent worker(1, "127.0.0.1", kPort);
    std::thread starter([&] { worker.start(); });
    // worker 在重试中（50ms 间隔起）—— 给它几轮失败机会后 master 回到同端口。
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    master.start();
    wait_for_running(master, true);

    starter.join();
    EXPECT_TRUE(worker.is_running());   // 10s 窗口内 master 已就绪，必然连上
    worker.stop();

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("worker_register_timeout", 300);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// 指数退避确定性验证：hooks 记录的尝试间隔递增（×2）。
TEST(WorkerAgentTest, ConnectRetryExponentialBackoff) {
    Config::instance()->set_int("worker_register_timeout", 1);      // 1s 窗口
    Config::instance()->set_int("worker_connect_retry_initial_ms", 40);

    WorkerAgent worker(1, "127.0.0.1", 0);  // port 0：全部失败
    worker.start();
    EXPECT_FALSE(worker.is_running());

    // 40ms initial ×2：间隔序列 40/80/160/320/640ms（1s 窗口内）。验证单调递增
    // 且首间隔 ≈40ms（允许调度抖动）。
    auto& attempts = worker.connect_attempts_for_testing_;
    ASSERT_GE(attempts.size(), 3u);
    std::vector<int64_t> gaps_ms;
    for (size_t i = 1; i < attempts.size(); ++i) {
        gaps_ms.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
            attempts[i] - attempts[i - 1]).count());
    }
    EXPECT_GE(gaps_ms[0], 35);                       // 首间隔 ≈ initial
    for (size_t i = 1; i < gaps_ms.size(); ++i) {
        EXPECT_GE(gaps_ms[i], gaps_ms[i - 1]);       // 单调递增（指数退避）
    }
    Config::instance()->set_int("worker_register_timeout", 300);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// 保活窗口耗尽 → 干净失败（running_ false，与 StartWithoutMaster 契约一致）。
TEST(WorkerAgentTest, ConnectRetryExhaustedCleanExit) {
    Config::instance()->set_int("worker_register_timeout", 1);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 10);
    WorkerAgent worker(1, "127.0.0.1", 0);
    worker.start();
    EXPECT_FALSE(worker.is_running());
    EXPECT_FALSE(worker.is_registered());
    worker.stop();  // no-op 安全
    Config::instance()->set_int("worker_register_timeout", 0);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// ── 断连重连（网络闪断，G3）───────────────────────────────────────────
// 模拟 worker 视角的 master 连接闪断（master 保持在线）→ worker 不退出、
// task 继续执行且上报缓冲、指数退避重连成功后缓冲送达 master。
// 不用 master.stop() 模拟闪断——stop 会广播 Shutdown（显式退出指令优先于重连）。
TEST(WorkerAgentTest, DisconnectReconnectsAndReports) {
    Config::instance()->set_int("worker_reconnect_timeout", 30);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 50);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 派一个 internal task（unknown name → 执行时 TaskFailed 上报）。
    master.submit_task(60, "bogus", "__fly_internal", {"a"}, {}, {});
    wait_for([&]{ return worker.has_pending_task(); }, 50, 20);
    ASSERT_TRUE(worker.has_pending_task()) << "internal task must reach worker";

    // park 重连线程：reconnect_loop 首次 connect 立即执行（master 在线毫秒级
    // 完成重连，initial_ms 只管失败重试间隔）——poll_task 与重连完成是毫秒级
    // 竞争，高负载下重连先完成 → TaskFailed 直发，缓冲断言失败（100 轮压测
    // 实测）。入口钩子闸门保证断连窗口确定性覆盖 poll_task；wait_for 10s 超时
    // 防测试失败路径 park 泄漏挂死进程。
    std::mutex hk_m;
    std::condition_variable hk_cv;
    bool release_reconnect = false;
    worker.reconnect_entry_hook_for_testing_ = [&] {
        std::unique_lock<std::mutex> lk(hk_m);
        hk_cv.wait_for(lk, std::chrono::seconds(10), [&] { return release_reconnect; });
    };

    // 模拟闪断：worker 进入重连（master 在线，重连会成功）。
    worker.simulate_master_disconnect_for_testing();
    EXPECT_FALSE(worker.is_registered());

    // 断连期间 task 执行产出 TaskFailed → 缓冲（reconnecting_ 期间必缓冲）。
    worker.poll_task();
    EXPECT_TRUE(worker.is_running()) << "worker must stay alive during grace";
    EXPECT_EQ(worker.pending_report_count_for_testing(), 1u)
        << "TaskFailed must be buffered while disconnected";

    // 放行重连：指数退避（initial 50ms）→ RegisterAck → 缓冲送达 master。
    {
        std::lock_guard<std::mutex> lk(hk_m);
        release_reconnect = true;
    }
    hk_cv.notify_all();
    wait_until_registered(worker);
    EXPECT_TRUE(worker.is_registered()) << "worker should reconnect within grace";
    // flush 在 on_register_ack 内注册可见之后同步执行（设计顺序：注册可见 →
    // 重放 A/B 类消息 → task 上报 flush 固定最后）——注册瞬间读计数存在窗口
    // （50 轮稳定性第 23 轮实测），有界等待 flush 完成后再断言（与下方
    // reached 检查同一 idiom）。
    wait_for([&]{ return worker.pending_report_count_for_testing() == 0u; }, 100, 10);
    EXPECT_EQ(worker.pending_report_count_for_testing(), 0u) << "buffer flushed on reconnect";

    bool reached = false;
    wait_for([&]{
        auto failed = master.get_failed_tasks();
        for (auto id : failed) { if (id == 60) { reached = true; return true; } }
        return false;
    }, 50, 20);
    EXPECT_TRUE(reached) << "buffered TaskFailed must reach master after reconnect";

    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("worker_reconnect_timeout", 120);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// 重连宽限耗尽 → 干净退出（master 挂=全群失败语义，worker 最多多活宽限期）。
TEST(WorkerAgentTest, ReconnectTimeoutExhaustedCleanExit) {
    Config::instance()->set_int("worker_reconnect_timeout", 1);    // 1s 宽限
    Config::instance()->set_int("worker_connect_retry_initial_ms", 10);

    MasterAgent master("127.0.0.1", 0);  // 动态分配（曾固定 48767——并行必撞）
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.stop();
    wait_for_running(master, false);

    // 宽限耗尽 → initiate_shutdown → is_running false（wait 覆盖 1s 退避 + 余量）。
    bool exited = false;
    wait_for([&]{ exited = !worker.is_running(); return exited; }, 50, 100);
    EXPECT_TRUE(exited) << "worker must exit cleanly after reconnect grace expires";
    worker.stop();

    Config::instance()->set_int("worker_reconnect_timeout", 120);
    Config::instance()->set_int("worker_connect_retry_initial_ms", 500);
}

// recv_request 的 compat read_all 必须在 peer_rpc_incoming_mutex_ 之外：
// read_all 阻塞等网络线程喂流，而喂流的触发方（request/disconnect handler）
// 需要同一把锁——持锁等待 = 环形死锁（另一连接的事件永久进不了队，
// server_loop 整线程冻结）。回归：流式请求读齐期间，第二连接的单帧请求
// 与断连事件必须照常处理。
TEST(WorkerAgentPeerRpcTest, RecvRequestUnlockBeforeReadAll) {
    WorkerAgent server_w(1, "127.0.0.1", 0);
    WorkerAgent client_w(2, "127.0.0.1", 0);
    const int port = server_w.start_peer_rpc_listen("127.0.0.1", 0);
    ASSERT_GT(port, 0);
    const uint64_t c1 = client_w.peer_rpc_connect("127.0.0.1", port);
    const uint64_t c2 = client_w.peer_rpc_connect("127.0.0.1", port);
    ASSERT_NE(c1, 0u);
    ASSERT_NE(c2, 0u);

    // 连接1：START-only 流（writer 构造即发 START，无数据无 END）——
    // server 侧 compat recv_request 取走后 read_all 阻塞等流。
    fly::PeerStreamWriter* w = client_w.peer_stream_writer(c1, "none", -1);
    ASSERT_TRUE(w != nullptr);

    std::atomic<bool> ta_done{false};
    std::thread ta([&] {
        // 无限等：取到 START 请求后卡在 read_all（流无 END）。修复前持锁
        // 卡死；修复后不持锁，断连时以异常返回。
        try {
            auto req = server_w.peer_rpc_recv_request(0);
            (void)req;
        } catch (const std::exception&) {
            // 断连零容忍：c1 关闭 → rx failed → read_all 抛错 → 到此收尾。
        }
        ta_done = true;
    });

    // 前置时序：ta 已取走 START 请求并进入 read_all（持有锁——修复前）。
    // 100ms 远大于取锁+取队列窗口；这是 precondition 等待而非结果断言。
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 触发：客户端整体断连（c1 → read_all 的 rx failed 唤醒；c2 →
    // disconnect handler 需要 peer_rpc_incoming_mutex_ 入 error_conns）。
    client_w.stop_peer_rpc();

    // 修复判据 1：ta 必须在有限时间完成（c1 断连唤醒 read_all，锁未被
    // ta 持有 → 断连事件能入队 → recv 抛错收尾）。
    bool done = false;
    for (int i = 0; i < 250 && !done; i++) {
        done = ta_done.load();
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(done) << "recv_request must not hold the incoming mutex while "
                         "read_all blocks (server_loop freeze otherwise)";
    // 修复判据 2：断连错误必须在有限时间可达 recv（error_conns 入队未被阻）。
    bool got_error = false;
    if (done) {
        try {
            auto req = server_w.peer_rpc_recv_request(2000);
            got_error = req.conn_id_ == 0 && req.payload_.empty() && !req.reader_;
        } catch (const std::exception&) {
            got_error = true;   // "peer connection error" 即断连语义传播
        }
    }
    EXPECT_TRUE(got_error) << "disconnect events must remain processable while "
                              "a compat read is pending";
    ta.join();
    client_w.stop_peer_rpc();
    server_w.stop_peer_rpc();
}

}  // namespace fly
