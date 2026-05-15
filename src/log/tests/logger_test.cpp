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

TEST_F(LoggerTest, CreateMasterLogger) {
    Logger::init_master("test_logs/");
    
    Logger* master = Logger::get_master();
    ASSERT_NE(master, nullptr);
    
    master->info("MasterAgent", "Master started");
    master->flush();
    
    std::ifstream file("test_logs/master.log");
    ASSERT_TRUE(file.is_open());
    
    CMString line;
    std::getline(file, line);
    EXPECT_TRUE(line.find("[INFO]") != CMString::npos);
    EXPECT_TRUE(line.find("[MasterAgent]") != CMString::npos);
    EXPECT_TRUE(line.find("Master started") != CMString::npos);
}

TEST_F(LoggerTest, CreateWorkerLogger) {
    Logger::init_worker(1, "test_logs/");
    
    Logger* worker = Logger::get_worker(1);
    ASSERT_NE(worker, nullptr);
    
    worker->debug("WorkerAgent", "Worker initializing");
    worker->flush();
    
    std::ifstream file("test_logs/worker1.log");
    ASSERT_TRUE(file.is_open());
    
    CMString line;
    std::getline(file, line);
    EXPECT_TRUE(line.find("[DEBUG]") != CMString::npos);
    EXPECT_TRUE(line.find("[WorkerAgent]") != CMString::npos);
}

TEST_F(LoggerTest, MultipleWorkers) {
    Logger::init_worker(1, "test_logs/");
    Logger::init_worker(2, "test_logs/");
    
    Logger* worker1 = Logger::get_worker(1);
    Logger* worker2 = Logger::get_worker(2);
    
    ASSERT_NE(worker1, nullptr);
    ASSERT_NE(worker2, nullptr);
    
    worker1->info("Worker1", "Task assigned");
    worker2->info("Worker2", "Task assigned");
    
    worker1->flush();
    worker2->flush();
    
    EXPECT_TRUE(std::filesystem::exists("test_logs/worker1.log"));
    EXPECT_TRUE(std::filesystem::exists("test_logs/worker2.log"));
}

TEST_F(LoggerTest, LogLevelFilter) {
    Logger::init_master("test_logs/");
    
    Logger* master = Logger::get_master();
    master->set_level(LogLevel::INFO);
    
    master->debug("Test", "Should not appear");
    master->info("Test", "Should appear");
    master->warn("Test", "Should appear");
    master->flush();
    
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
    Logger::init_master("test_logs/");
    
    Logger* master = Logger::get_master();
    
    master->debug("Test", "Debug message");
    master->info("Test", "Info message");
    master->warn("Test", "Warn message");
    master->error("Test", "Error message");
    master->flush();
    
    std::ifstream file("test_logs/master.log");
    CMString line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
    }
    EXPECT_EQ(count, 4);
}

TEST_F(LoggerTest, TimestampFormat) {
    Logger::init_master("test_logs/");
    
    Logger* master = Logger::get_master();
    master->info("Test", "Check timestamp");
    master->flush();
    
    std::ifstream file("test_logs/master.log");
    CMString line;
    std::getline(file, line);
    
    EXPECT_TRUE(line.find("2026") != CMString::npos || line.find("2025") != CMString::npos);
    EXPECT_TRUE(line.find(":") != CMString::npos);
}

}  // namespace fly