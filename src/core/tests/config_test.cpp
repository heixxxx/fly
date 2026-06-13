#include <gtest/gtest.h>
#include <core/cpp/config.h>

TEST(ConfigTest, SingletonReturnsSameInstance) {
    auto c1 = Config::instance();
    auto c2 = Config::instance();
    EXPECT_EQ(c1.get(), c2.get());
}

TEST(ConfigTest, DefaultValues) {
    auto config = Config::instance();
    config->reset();
    
    EXPECT_EQ(config->get_int("heartbeat_timeout"), 120);
    EXPECT_EQ(config->get_int("heartbeat_interval"), 5);
    EXPECT_EQ(config->get_str("transport_type"), "tcp");
}

TEST(ConfigTest, SetAndGetInt) {
    auto config = Config::instance();
    config->reset();
    
    config->set_int("test_key", 42);
    EXPECT_EQ(config->get_int("test_key"), 42);
}

TEST(ConfigTest, SetAndGetStr) {
    auto config = Config::instance();
    config->reset();
    
    config->set_str("test_key2", "hello");
    EXPECT_EQ(config->get_str("test_key2"), "hello");
}

TEST(ConfigTest, ThrowsAfterWorkersLaunched) {
    auto config = Config::instance();
    config->reset();
    
    config->mark_workers_launched();
    
    EXPECT_THROW(config->set_int("any_key", 1), std::runtime_error);
    EXPECT_THROW(config->set_str("any_key", "value"), std::runtime_error);
}

TEST(ConfigTest, WorkersLaunchedState) {
    auto config = Config::instance();
    config->reset();
    
    EXPECT_FALSE(config->is_workers_launched());
    config->mark_workers_launched();
    EXPECT_TRUE(config->is_workers_launched());
}

TEST(ConfigTest, ResetRestoresDefaults) {
    auto config = Config::instance();
    config->reset();
    
    config->set_int("heartbeat_timeout", 9000);
    EXPECT_EQ(config->get_int("heartbeat_timeout"), 9000);
    
    config->reset();
    EXPECT_EQ(config->get_int("heartbeat_timeout"), 120);
}

TEST(ConfigTest, UnknownKeyReturnsInvalidInt) {
    auto config = Config::instance();
    config->reset();
    
    EXPECT_EQ(config->get_int("nonexistent_key"), Config::INVALID_INT);
}

TEST(ConfigTest, EmptyStringKey) {
    auto config = Config::instance();
    config->reset();
    
    config->set_str("empty_key", "");
    EXPECT_EQ(config->get_str("empty_key"), "");
}

TEST(ConfigTest, OverwriteExistingKey) {
    auto config = Config::instance();
    config->reset();
    
    config->set_int("overwrite_test", 100);
    EXPECT_EQ(config->get_int("overwrite_test"), 100);
    
    config->set_int("overwrite_test", 200);
    EXPECT_EQ(config->get_int("overwrite_test"), 200);
}

TEST(ConfigTest, LargeIntValue) {
    auto config = Config::instance();
    config->reset();
    
    config->set_int("large_value", 9223372036854775807LL);
    EXPECT_EQ(config->get_int("large_value"), 9223372036854775807LL);
}

TEST(ConfigTest, NegativeIntValue) {
    auto config = Config::instance();
    config->reset();
    
    config->set_int("negative_value", -1000000);
    EXPECT_EQ(config->get_int("negative_value"), -1000000);
}

TEST(ConfigTest, LongStringKey) {
    auto config = Config::instance();
    config->reset();
    
    CMString long_key = "very_long_config_key_name_that_tests_string_handling";
    config->set_str(long_key, "value");
    EXPECT_EQ(config->get_str(long_key), "value");
}

TEST(ConfigTest, UnicodeStringValue) {
    auto config = Config::instance();
    config->reset();
    
    config->set_str("unicode_key", "你好世界");
    EXPECT_EQ(config->get_str("unicode_key"), "你好世界");
}

TEST(ConfigTest, MultipleSetBeforeLaunch) {
    auto config = Config::instance();
    config->reset();
    
    config->set_int("key1", 1);
    config->set_int("key2", 2);
    config->set_int("key3", 3);
    config->set_str("str1", "a");
    config->set_str("str2", "b");
    
    EXPECT_EQ(config->get_int("key1"), 1);
    EXPECT_EQ(config->get_int("key2"), 2);
    EXPECT_EQ(config->get_int("key3"), 3);
    EXPECT_EQ(config->get_str("str1"), "a");
    EXPECT_EQ(config->get_str("str2"), "b");
}