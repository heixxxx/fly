#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include "test_log_isolation.h"

namespace fly {

TEST(AgentIntegrationTest, CreateMasterAndWorker) {
    MasterAgent master("127.0.0.1", 0);
    WorkerAgent worker(1, "127.0.0.1", 0);
    
    EXPECT_FALSE(master.is_running());
    EXPECT_FALSE(worker.is_running());
}

TEST(AgentIntegrationTest, StartBothAgents) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    
    EXPECT_TRUE(master.is_running());
    EXPECT_TRUE(worker.is_running());
    
    worker.stop();
    master.stop();
    
    EXPECT_FALSE(worker.is_running());
    EXPECT_FALSE(master.is_running());
}

TEST(AgentIntegrationTest, MultipleWorkersOneMaster) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    uint16_t port = master.get_port();
    WorkerAgent worker1(1, "127.0.0.1", port);
    WorkerAgent worker2(2, "127.0.0.1", port);
    WorkerAgent worker3(3, "127.0.0.1", port);
    
    worker1.start();
    worker2.start();
    worker3.start();
    
    EXPECT_TRUE(master.is_running());
    EXPECT_TRUE(worker1.is_running());
    EXPECT_TRUE(worker2.is_running());
    EXPECT_TRUE(worker3.is_running());
    
    EXPECT_EQ(worker1.get_worker_id(), 1);
    EXPECT_EQ(worker2.get_worker_id(), 2);
    EXPECT_EQ(worker3.get_worker_id(), 3);
    
    worker1.stop();
    worker2.stop();
    worker3.stop();
    master.stop();
    
    EXPECT_FALSE(worker1.is_running());
    EXPECT_FALSE(worker2.is_running());
    EXPECT_FALSE(worker3.is_running());
    EXPECT_FALSE(master.is_running());
}

TEST(AgentIntegrationTest, IndependentLifecycle) {
    MasterAgent master1("127.0.0.1", 0);
    MasterAgent master2("127.0.0.1", 0);
    
    master1.start();
    WorkerAgent worker1(1, "127.0.0.1", master1.get_port());
    worker1.start();
    
    EXPECT_TRUE(master1.is_running());
    EXPECT_TRUE(worker1.is_running());
    EXPECT_FALSE(master2.is_running());
    
    master2.start();
    WorkerAgent worker2(2, "127.0.0.1", master2.get_port());
    worker2.start();
    
    EXPECT_TRUE(master2.is_running());
    EXPECT_TRUE(worker2.is_running());
    
    worker1.stop();
    master1.stop();
    
    EXPECT_FALSE(master1.is_running());
    EXPECT_FALSE(worker1.is_running());
    EXPECT_TRUE(master2.is_running());
    EXPECT_TRUE(worker2.is_running());
    
    worker2.stop();
    master2.stop();
    
    EXPECT_FALSE(master2.is_running());
    EXPECT_FALSE(worker2.is_running());
}

}  // namespace fly