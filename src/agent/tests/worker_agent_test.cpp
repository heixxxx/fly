#include <gtest/gtest.h>
#include <agent/cpp/worker_agent.h>
#include <common/cpp/worker_context.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(WorkerAgentTest, CreateWithId) {
    WorkerAgent worker(42, "127.0.0.1", 18080);
    EXPECT_EQ(worker.get_worker_id(), 42);
}

TEST(WorkerAgentTest, StartWithoutMaster) {
    WorkerAgent worker(1, "127.0.0.1", 18090);
    worker.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(worker.is_running());
    EXPECT_FALSE(worker.is_registered());
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker.is_running());
}

TEST(WorkerAgentTest, SetExecutor) {
    WorkerAgent worker(1, "127.0.0.1", 18091);
    
    worker.set_executor(nullptr);
}

TEST(WorkerAgentTest, MultipleStartStop) {
    WorkerAgent worker(1, "127.0.0.1", 18092);
    
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
    WorkerAgent worker(1, "127.0.0.1", 18100);
    
    worker.begin_task(42);
    EXPECT_TRUE(WorkerAgentContext::is_active());
    
    auto writes = worker.end_task(42);
    EXPECT_FALSE(WorkerAgentContext::is_active());
    EXPECT_TRUE(writes.empty());
}

TEST(WorkerAgentTest, RecordWrites) {
    WorkerAgent worker(1, "127.0.0.1", 18101);
    
    worker.begin_task(100);
    worker.record_write("db_abc", "output/result");
    worker.record_write("db_abc", "output/intermediate");
    auto writes = worker.end_task(100);
    
    EXPECT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0], "db_abc:output/result");
    EXPECT_EQ(writes[1], "db_abc:output/intermediate");
}

TEST(WorkerAgentTest, MultipleTasksSequential) {
    WorkerAgent worker(1, "127.0.0.1", 18102);

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
    WorkerAgent worker(1, "127.0.0.1", 18110);
    worker.begin_task(200);

    // Simulate Database.write_object tracking (what Database does internally)
    worker.record_write("db_hash_aaa", "output/result");
    worker.record_write("db_hash_aaa", "output/log");

    auto writes = worker.end_task(200);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0], "db_hash_aaa:output/result");
    EXPECT_EQ(writes[1], "db_hash_aaa:output/log");
}

TEST(WorkerAgentTest, MultiDbSameObjectNameTracking) {
    WorkerAgent worker(1, "127.0.0.1", 18111);
    worker.begin_task(300);

    // Two different databases, same object name
    worker.record_write("db_proj_a", "output/result");
    worker.record_write("db_proj_b", "output/result");

    auto writes = worker.end_task(300);
    ASSERT_EQ(writes.size(), 2u);
    // Same object name but different db_id → different full names
    EXPECT_NE(writes[0], writes[1]);
    EXPECT_EQ(writes[0], "db_proj_a:output/result");
    EXPECT_EQ(writes[1], "db_proj_b:output/result");
}

TEST(WorkerAgentTest, EndTaskClearsTracking) {
    WorkerAgent worker(1, "127.0.0.1", 18112);

    // Task 1
    worker.begin_task(1);
    worker.record_write("db1", "obj1");
    auto writes1 = worker.end_task(1);
    EXPECT_EQ(writes1.size(), 1u);

    // Task 2 should not see Task 1's writes
    worker.begin_task(2);
    auto writes2 = worker.end_task(2);
    EXPECT_TRUE(writes2.empty());
}

}  // namespace fly