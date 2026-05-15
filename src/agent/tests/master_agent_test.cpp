#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(MasterAgentTest, CreateAndStart) {
    MasterAgent master("127.0.0.1", 18080);
    master.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(master.is_running());
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, CreateWithDifferentPorts) {
    MasterAgent master1("127.0.0.1", 18081);
    MasterAgent master2("127.0.0.1", 18082);
    
    master1.start();
    master2.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(master1.is_running());
    EXPECT_TRUE(master2.is_running());
    
    master1.stop();
    master2.stop();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_FALSE(master1.is_running());
    EXPECT_FALSE(master2.is_running());
}

TEST(MasterAgentTest, MultipleStartStop) {
    MasterAgent master("127.0.0.1", 18083);
    
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