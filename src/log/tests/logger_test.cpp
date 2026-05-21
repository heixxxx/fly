#include <gtest/gtest.h>
#include <log/cpp/logger.h>
#include <fstream>
#include <filesystem>

namespace fly {

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::shutdown();
        std::filesystem::remove_all("test_logs");
    }

    void TearDown() override {
        Logger::shutdown();
        std::filesystem::remove_all("test_logs");
    }
};

TEST_F(LoggerTest, MasterLog) {
    Logger::init("test_logs/", 0);
    INFO("Master started");
    Logger::instance().flush();

    std::ifstream file("test_logs/master.log");
    ASSERT_TRUE(file.is_open());

    CMString line;
    std::getline(file, line);
    EXPECT_TRUE(line.find("[INFO]") != CMString::npos);
    EXPECT_TRUE(line.find("Master started") != CMString::npos);
}

TEST_F(LoggerTest, WorkerLog) {
    Logger::init("test_logs/", 1);
    DBG("Worker initializing");
    Logger::instance().flush();

    std::ifstream file("test_logs/worker1.log");
    ASSERT_TRUE(file.is_open());

    CMString line;
    std::getline(file, line);
    EXPECT_TRUE(line.find("[DEBUG]") != CMString::npos);
    EXPECT_TRUE(line.find("Worker initializing") != CMString::npos);
}

TEST_F(LoggerTest, ReinitWorker) {
    Logger::init("test_logs/", 0);
    INFO("First init");
    Logger::shutdown();

    Logger::init("test_logs/", 3);
    INFO("Second init");
    Logger::instance().flush();

    ASSERT_TRUE(std::filesystem::exists("test_logs/master.log"));
    ASSERT_TRUE(std::filesystem::exists("test_logs/worker3.log"));
}

TEST_F(LoggerTest, LogLevelFilter) {
    Logger::init("test_logs/", 0);
    Logger::instance().set_level(LogLevel::INFO);

    DBG("Should not appear");
    INFO("Should appear");
    WARN("Should appear");
    Logger::instance().flush();

    std::ifstream file("test_logs/master.log");
    CMString line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
        EXPECT_TRUE(line.find("[DEBUG]") == CMString::npos);
    }
    EXPECT_EQ(count, 2);
}

TEST_F(LoggerTest, AllLogLevels) {
    Logger::init("test_logs/", 0);

    DBG("Debug message");
    INFO("Info message");
    WARN("Warn message");
    ERR("Error message");
    Logger::instance().flush();

    std::ifstream file("test_logs/master.log");
    CMString line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
    }
    EXPECT_EQ(count, 4);
}

TEST_F(LoggerTest, TimestampFormat) {
    Logger::init("test_logs/", 0);
    INFO("Check timestamp");
    Logger::instance().flush();

    std::ifstream file("test_logs/master.log");
    CMString line;
    std::getline(file, line);

    EXPECT_TRUE(line.find("2026") != CMString::npos || line.find("2025") != CMString::npos);
    EXPECT_TRUE(line.find(":") != CMString::npos);
}

}  // namespace fly
