#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(MasterAgentTest, CreateAndStart) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(master.is_running());
    EXPECT_GT(master.get_port(), 0);
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, CreateWithDifferentPorts) {
    MasterAgent master1("127.0.0.1", 0);
    MasterAgent master2("127.0.0.1", 0);
    
    master1.start();
    master2.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(master1.is_running());
    EXPECT_TRUE(master2.is_running());
    EXPECT_GT(master1.get_port(), 0);
    EXPECT_GT(master2.get_port(), 0);
    EXPECT_NE(master1.get_port(), master2.get_port());
    
    master1.stop();
    master2.stop();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_FALSE(master1.is_running());
    EXPECT_FALSE(master2.is_running());
}

TEST(MasterAgentTest, MultipleStartStop) {
    MasterAgent master("127.0.0.1", 0);
    
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
    
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
}

}  // namespace fly