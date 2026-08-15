#include <gtest/gtest.h>
#include <log/cpp/logger.h>
#include <task/cpp/task_manager.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fly {

enum class TestColor { RED, GREEN, BLUE };

struct TestPoint { double x, y; };

struct TestRect { double x1, y1, x2, y2; };

}  // namespace fly

CM_FORMAT_ENUM(fly::TestColor, RED, GREEN, BLUE);
CM_FORMAT_CLASS(fly::TestPoint, "({}, {})", v.x, v.y);
CM_FORMAT_CLASS(fly::TestRect, "[{},{}]-[{},{}]", v.x1, v.y1, v.x2, v.y2);
CM_FORMAT_ENUM_EX(fly::TaskStatus, (PENDING, "P"), (RUNNING, "R"), (COMPLETED, "C"), (FAILED, "F"), (CANCELLED, "X"));

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
    Logger::instance()->flush();

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
    Logger::instance()->flush();

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
    Logger::instance()->flush();

    ASSERT_TRUE(std::filesystem::exists("test_logs/master.log"));
    ASSERT_TRUE(std::filesystem::exists("test_logs/worker3.log"));
}

TEST_F(LoggerTest, LogLevelFilter) {
    Logger::init("test_logs/", 0);
    Logger::instance()->set_level(LogLevel::INFO);

    DBG("Should not appear");
    INFO("Should appear");
    WARN("Should appear");
    Logger::instance()->flush();

    std::ifstream file("test_logs/master.log");
    CMString line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
        EXPECT_TRUE(line.find("[DEBUG]") == CMString::npos);
    }
    EXPECT_EQ(count, 2);
}

// ── 自动 flush（积累量 / 时间间隔，用户确认增强）──────────────────────
// DEBUG/INFO 不再只依赖退出 flush：累计写入达到字节数阈值、或距上次 flush
// 超过时间间隔（下一条日志触发检查）时自动 flush——避免日志文件更新延迟
// 过长（P3-19 根因：测试在运行中读日志漏行）。WARN/ERROR 立即 flush 不变。

TEST_F(LoggerTest, InfoAutoFlushedAfterByteThreshold) {
    Logger::set_flush_params(64, 60000);  // 极小字节阈值 + 超长间隔（隔离变量）
    Logger::init("test_logs/", 0);

    // 每行 ~40+ 字节，两条累计超过 64B 阈值 → 第二条触发自动 flush。
    INFO("threshold-probe-line-1-padding-padding-padding-padding");
    INFO("threshold-probe-line-2-padding-padding-padding-padding");

    std::ifstream file("test_logs/master.log");
    CMString line;
    int count = 0;
    while (std::getline(file, line)) ++count;
    EXPECT_GE(count, 1) << "accumulated bytes past threshold must auto-flush";
}

TEST_F(LoggerTest, InfoAutoFlushedAfterInterval) {
    Logger::set_flush_params(1ULL << 30, 1);  // 超大字节阈值 + 1ms 间隔（隔离变量）
    Logger::init("test_logs/", 0);

    INFO("interval-probe-first");             // 首条：写入缓冲
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    INFO("interval-probe-second");            // 距上次 flush >1ms → 触发 flush（连带首条落盘）

    std::ifstream file("test_logs/master.log");
    CMString content((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("interval-probe-first"), CMString::npos)
        << "entries older than the flush interval must be auto-flushed";
}

TEST_F(LoggerTest, WarnStillFlushesImmediately) {
    Logger::init("test_logs/", 0);
    INFO("before-warn-entry");   // 无阈值触发（间隔默认 1s 内、字节数远低阈值）
    WARN("immediate-warn-entry");

    std::ifstream file("test_logs/master.log");
    CMString content((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("immediate-warn-entry"), CMString::npos)
        << "WARN must keep flushing immediately";
    EXPECT_NE(content.find("before-warn-entry"), CMString::npos)
        << "the WARN flush also lands earlier buffered entries";
}

TEST_F(LoggerTest, AllLogLevels) {
    Logger::init("test_logs/", 0);

    DBG("Debug message");
    INFO("Info message");
    WARN("Warn message");
    ERR("Error message");
    Logger::instance()->flush();

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
    Logger::instance()->flush();

    std::ifstream file("test_logs/master.log");
    CMString line;
    std::getline(file, line);

    EXPECT_TRUE(line.find("2026") != CMString::npos || line.find("2025") != CMString::npos);
    EXPECT_TRUE(line.find(":") != CMString::npos);
}

TEST_F(LoggerTest, FmtCustomTypes) {
    Logger::init("test_logs/", 0);

    DBG("color={}", TestColor::GREEN);
    INFO("point={}", TestPoint{3.0, 2.0});
    WARN("rect={}", TestRect{1.0, 2.0, 100.0, 200.0});
    ERR("empty args ok: {}", TestColor::RED);
    INFO("status={}", TaskStatus::RUNNING);
    Logger::instance()->flush();

    std::ifstream file("test_logs/master.log");
    CMString line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
        if (count == 1) {
            EXPECT_TRUE(line.find("GREEN") != CMString::npos) << "LINE1: " << line;
            EXPECT_TRUE(line.find("[DEBUG]") != CMString::npos);
        }
        if (count == 2) {
            EXPECT_TRUE(line.find("(3, 2)") != CMString::npos) << "LINE2: " << line;
            EXPECT_TRUE(line.find("[INFO]") != CMString::npos);
        }
        if (count == 3) {
            EXPECT_TRUE(line.find("[1,2]-[100,200]") != CMString::npos) << "LINE3: " << line;
            EXPECT_TRUE(line.find("[WARN]") != CMString::npos);
        }
        if (count == 4) {
            EXPECT_TRUE(line.find("RED") != CMString::npos) << "LINE4: " << line;
            EXPECT_TRUE(line.find("[ERROR]") != CMString::npos);
        }
        if (count == 5) {
            EXPECT_TRUE(line.find("status=R") != CMString::npos) << "LINE5: " << line;
            EXPECT_TRUE(line.find("[INFO]") != CMString::npos);
        }
    }
    EXPECT_EQ(count, 5);
}

}  // namespace fly
