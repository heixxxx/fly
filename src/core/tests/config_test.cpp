#include <gtest/gtest.h>
#include "../cpp/config.h"

TEST(ConfigTest, SingletonReturnsSameInstance) {
    Config& c1 = Config::instance();
    Config& c2 = Config::instance();
    EXPECT_EQ(&c1, &c2);
}

TEST(ConfigTest, DefaultValues) {
    Config& config = Config::instance();
    config.reset();
    
    EXPECT_EQ(config.get_int("master_port"), 8000);
    EXPECT_EQ(config.get_int("heartbeat_timeout"), 120);
    EXPECT_EQ(config.get_int("heartbeat_interval"), 5);
    EXPECT_EQ(config.get_str("transport_type"), "tcp");
}

TEST(ConfigTest, SetAndGetInt) {
    Config& config = Config::instance();
    config.reset();
    
    config.set_int("test_key", 42);
    EXPECT_EQ(config.get_int("test_key"), 42);
}

TEST(ConfigTest, SetAndGetStr) {
    Config& config = Config::instance();
    config.reset();
    
    config.set_str("test_key2", "hello");
    EXPECT_EQ(config.get_str("test_key2"), "hello");
}

TEST(ConfigTest, ThrowsAfterWorkersLaunched) {
    Config& config = Config::instance();
    config.reset();
    
    config.mark_workers_launched();
    
    EXPECT_THROW(config.set_int("any_key", 1), std::runtime_error);
    EXPECT_THROW(config.set_str("any_key", "value"), std::runtime_error);
}

TEST(ConfigTest, WorkersLaunchedState) {
    Config& config = Config::instance();
    config.reset();
    
    EXPECT_FALSE(config.is_workers_launched());
    config.mark_workers_launched();
    EXPECT_TRUE(config.is_workers_launched());
}

TEST(ConfigTest, ResetRestoresDefaults) {
    Config& config = Config::instance();
    config.reset();
    
    config.set_int("master_port", 9000);
    EXPECT_EQ(config.get_int("master_port"), 9000);
    
    config.reset();
    EXPECT_EQ(config.get_int("master_port"), 8000);
}