#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>

namespace fly {

TEST(MasterAgentTest, CreateAndStart) {
    MasterAgent master("127.0.0.1", 18080);
    master.start();
    
    EXPECT_TRUE(master.is_running());
    master.stop();
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, CreateWithDifferentPorts) {
    MasterAgent master1("127.0.0.1", 18081);
    MasterAgent master2("127.0.0.1", 18082);
    
    master1.start();
    master2.start();
    
    EXPECT_TRUE(master1.is_running());
    EXPECT_TRUE(master2.is_running());
    
    master1.stop();
    master2.stop();
    
    EXPECT_FALSE(master1.is_running());
    EXPECT_FALSE(master2.is_running());
}

TEST(MasterAgentTest, MultipleStartStop) {
    MasterAgent master("127.0.0.1", 18083);
    
    master.start();
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    EXPECT_FALSE(master.is_running());
    
    master.start();
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    EXPECT_FALSE(master.is_running());
}

}  // namespace fly