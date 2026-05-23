#include <gtest/gtest.h>
#include <agent/cpp/worker_agent.h>
#include <common/cpp/worker_context.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(WorkerAgentTest, CreateWithId) {
    WorkerAgent worker(42, "127.0.0.1", 0);
    EXPECT_EQ(worker.get_worker_id(), 42);
}

TEST(WorkerAgentTest, StartWithoutMaster) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    worker.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(worker.is_running());
    EXPECT_FALSE(worker.is_registered());
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker.is_running());
}

TEST(WorkerAgentTest, SetExecutor) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    
    worker.set_executor(nullptr);
}

TEST(WorkerAgentTest, MultipleStartStop) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(worker.is_running());
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker.is_running());
    
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(worker.is_running());
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker.is_running());
}

TEST(WorkerAgentContextTest, DefaultNotActive) {
    EXPECT_FALSE(WorkerAgentContext::is_active());
}

TEST(WorkerAgentContextTest, SetAndClear) {
    int calls = 0;
    WorkerAgentContext::set(
        [](void* ctx, const CMString& db_id, const CMString& name) {
            (*static_cast<int*>(ctx))++;
        },
        &calls
    );
    EXPECT_TRUE(WorkerAgentContext::is_active());
    
    WorkerAgentContext::record_write("db1", "obj1");
    EXPECT_EQ(calls, 1);
    
    WorkerAgentContext::clear();
    EXPECT_FALSE(WorkerAgentContext::is_active());
    WorkerAgentContext::record_write("db1", "obj2");
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
    
    worker.begin_task(100);
    worker.record_write("db_abc", "output/result");
    worker.record_write("db_abc", "output/intermediate");
    auto writes = worker.end_task(100);
    
    EXPECT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0], "db_abc:output/result");
    EXPECT_EQ(writes[1], "db_abc:output/intermediate");
}

TEST(WorkerAgentTest, MultipleTasksSequential) {
    WorkerAgent worker(1, "127.0.0.1", 0);

    worker.begin_task(1);
    worker.record_write("db1", "a");
    auto writes1 = worker.end_task(1);
    EXPECT_EQ(writes1.size(), 1u);

    worker.begin_task(2);
    worker.record_write("db2", "b");
    auto writes2 = worker.end_task(2);
    EXPECT_EQ(writes2.size(), 1u);
    EXPECT_EQ(writes2[0], "db2:b");
}

TEST(WorkerAgentTest, WriteTrackingWithDatabase) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    worker.begin_task(200);

    worker.record_write("db_hash_aaa", "output/result");
    worker.record_write("db_hash_aaa", "output/log");

    auto writes = worker.end_task(200);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0], "db_hash_aaa:output/result");
    EXPECT_EQ(writes[1], "db_hash_aaa:output/log");
}

TEST(WorkerAgentTest, MultiDbSameObjectNameTracking) {
    WorkerAgent worker(1, "127.0.0.1", 0);
    worker.begin_task(300);

    worker.record_write("db_proj_a", "output/result");
    worker.record_write("db_proj_b", "output/result");

    auto writes = worker.end_task(300);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_NE(writes[0], writes[1]);
    EXPECT_EQ(writes[0], "db_proj_a:output/result");
    EXPECT_EQ(writes[1], "db_proj_b:output/result");
}

TEST(WorkerAgentTest, EndTaskClearsTracking) {
    WorkerAgent worker(1, "127.0.0.1", 0);

    worker.begin_task(1);
    worker.record_write("db1", "obj1");
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

}  // namespace fly

#include <storage/cpp/data_service.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/index_entry.h>
#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <cstdio>

namespace fly {

static void create_test_idx_file(const CMString& base_path, uint64_t worker_id,
                                  const CMVector<IndexEntry>& entries) {
    CMString idx_path = base_path + "/worker_" + std::to_string(worker_id) + ".idx";
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
    fly::DataService& ds_ = fly::DataService::instance();

    void SetUp() override {
        test_dir_ = make_temp_dir("idxload");
        Logger::shutdown();
        Logger::init("test_logs/idxload", 0);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(IdxLoadTest, WorkerProcessesSingleIdxFile) {
    IndexEntry entry;
    entry.object_name = "test_db:obj_alpha";
    entry.file_name = "data_0.bin";
    entry.offset = 0;
    entry.size = 100;
    create_test_idx_file(test_dir_, 5, {entry});

    ds_.register_database("test_db", test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(worker.is_registered());

    master.send_idx_load_commands("test_db", test_dir_, {5});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_TRUE(ds_.has_local_object("test_db:obj_alpha"));

    worker.stop();
    master.stop();

    ds_.unregister_database("test_db");
    ds_.remove_local_index("test_db:obj_alpha");
}

TEST_F(IdxLoadTest, WorkerProcessesMultipleIdxFiles) {
    IndexEntry e1;
    e1.object_name = "test_db:obj_one";
    e1.file_name = "data_10.bin";
    e1.offset = 0;
    e1.size = 50;

    IndexEntry e2;
    e2.object_name = "test_db:obj_two";
    e2.file_name = "data_20.bin";
    e2.offset = 0;
    e2.size = 75;

    create_test_idx_file(test_dir_, 10, {e1});
    create_test_idx_file(test_dir_, 20, {e2});

    ds_.register_database("test_db", test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(worker.is_registered());

    master.send_idx_load_commands("test_db", test_dir_, {10, 20});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_TRUE(ds_.has_local_object("test_db:obj_one"));
    EXPECT_TRUE(ds_.has_local_object("test_db:obj_two"));

    worker.stop();
    master.stop();

    ds_.unregister_database("test_db");
    ds_.remove_local_index("test_db:obj_one");
    ds_.remove_local_index("test_db:obj_two");
}

TEST_F(IdxLoadTest, WorkerSkipsMissingIdxFiles) {
    IndexEntry entry;
    entry.object_name = "test_db:obj_exists";
    entry.file_name = "data_5.bin";
    entry.offset = 0;
    entry.size = 100;
    create_test_idx_file(test_dir_, 5, {entry});

    ds_.register_database("test_db", test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(worker.is_registered());

    master.send_idx_load_commands("test_db", test_dir_, {5, 99});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_TRUE(ds_.has_local_object("test_db:obj_exists"));

    worker.stop();
    master.stop();

    ds_.unregister_database("test_db");
    ds_.remove_local_index("test_db:obj_exists");
}

TEST_F(IdxLoadTest, WorkerHandlesEmptyOldWorkerIds) {
    ds_.register_database("test_db", test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(worker.is_registered());

    master.send_idx_load_commands("test_db", test_dir_, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_TRUE(worker.is_running());

    worker.stop();
    master.stop();

    ds_.unregister_database("test_db");
}

TEST_F(IdxLoadTest, WorkerHandlesEmptyIdxFile) {
    CMString idx_path = test_dir_ + "/worker_30.idx";
    {
        std::ofstream ofs(idx_path, std::ios::binary);
    }

    ds_.register_database("test_db", test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(worker.is_registered());

    master.send_idx_load_commands("test_db", test_dir_, {30});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_TRUE(worker.is_running());

    worker.stop();
    master.stop();

    ds_.unregister_database("test_db");
}

TEST_F(IdxLoadTest, WorkerLoadsMultipleEntriesPerIdx) {
    IndexEntry e1;
    e1.object_name = "test_db:block_a";
    e1.file_name = "data_40.bin";
    e1.offset = 0;
    e1.size = 50;

    IndexEntry e2;
    e2.object_name = "test_db:block_a";
    e2.file_name = "data_40.bin";
    e2.offset = 50;
    e2.size = 50;

    IndexEntry e3;
    e3.object_name = "test_db:block_b";
    e3.file_name = "data_40.bin";
    e3.offset = 100;
    e3.size = 30;

    create_test_idx_file(test_dir_, 40, {e1, e2, e3});

    ds_.register_database("test_db", test_dir_, test_dir_ + "/data");

    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(worker.is_registered());

    master.send_idx_load_commands("test_db", test_dir_, {40});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_TRUE(ds_.has_local_object("test_db:block_a"));
    EXPECT_TRUE(ds_.has_local_object("test_db:block_b"));

    worker.stop();
    master.stop();

    ds_.unregister_database("test_db");
    ds_.remove_local_index("test_db:block_a");
    ds_.remove_local_index("test_db:block_b");
}

}  // namespace fly
