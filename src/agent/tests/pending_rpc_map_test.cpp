// PendingRpcMap 单元测试。
//
// 重点：确定性并发测试（FLY_ENABLE_TEST_HOOKS + std::latch 强制线程交错，
// 项目先例 master_agent_test.cpp Problem1）。所有用例无 sleep-then-assert，
// 交错点由 pre_sleep_hook_ + latch 钉死。
//
// LostWakeupNoLockNotifyTimesOut（red 用例，修复后改写为
// CompleteDuringPredicateWindowWakesWaiter）：复刻 on_var_ack 旧的两阶段
// 完成序列——锁外写 completed_ + 无锁 notify_all——钉死 cv lost wakeup 窗口：
// waiter 持锁查完 pred（false）、尚未进入 wait 时 notify 落空，waiter 只能
// 卡满超时。修复方向：完成路径（字段写 + notify）必须全部持锁（complete()）。

#include <agent/cpp/pending_rpc_map.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>

namespace {

using fly::PendingRpcMap;

struct TestPending {
    bool completed_ = false;
    bool success_ = false;
    CMString payload_;
};

using TestRpcMap = PendingRpcMap<CMString, TestPending>;

constexpr auto kShortTimeout = std::chrono::milliseconds(1000);

} // namespace

TEST(PendingRpcMapTest, CompleteWakesWaiter) {
    TestRpcMap map;
    map.emplace("k", CMMakeShared<TestPending>());

    std::latch waiter_ready(1);
    map.pre_sleep_hook_ = [&] { waiter_ready.count_down(); };

    CMSharedPtr<TestPending> result;
    auto waiter = std::thread([&] {
        result = map.wait_for("k", std::chrono::milliseconds(3000),
                              [](const CMSharedPtr<TestPending>& p) { return p->completed_; });
    });
    waiter_ready.wait();  // waiter 持锁、即将进入 wait（确定性交错点）
    map.complete("k", [](TestPending& p) {
        p.completed_ = true;
        p.success_ = true;
        p.payload_ = "ok";
    });
    waiter.join();

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->completed_);
    EXPECT_TRUE(result->success_);
    EXPECT_EQ(result->payload_, "ok");
}

TEST(PendingRpcMapTest, CompleteMissIsNoop) {
    TestRpcMap map;
    map.complete("absent", [](TestPending& p) { p.completed_ = true; });
    EXPECT_EQ(map.find("absent"), nullptr);
}

TEST(PendingRpcMapTest, WaitForTimeoutErasesEntry) {
    TestRpcMap map;
    map.emplace("k", CMMakeShared<TestPending>());
    auto result = map.wait_for("k", std::chrono::milliseconds(50),
                               [](const CMSharedPtr<TestPending>& p) { return p->completed_; });
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(map.find("k"), nullptr);  // 超时路径必须 erase（防泄漏语义）
}

TEST(PendingRpcMapTest, WaitForAbsentKeyReturnsNullImmediately) {
    TestRpcMap map;
    auto result = map.wait_for("absent", kShortTimeout,
                               [](const CMSharedPtr<TestPending>& p) { return p->completed_; });
    EXPECT_EQ(result, nullptr);
}

TEST(PendingRpcMapTest, CompleteAllIfFillsOnlyMatching) {
    TestRpcMap map;
    map.emplace("a", CMMakeShared<TestPending>());
    map.emplace("b", CMMakeShared<TestPending>());
    map.complete("a", [](TestPending& p) { p.payload_ = "conn-a"; });

    map.complete_all_if(
        [](const TestPending& p) { return p.payload_ == "conn-a"; },
        [](TestPending& p) { p.completed_ = true; });

    EXPECT_TRUE(map.find("a")->completed_);
    EXPECT_FALSE(map.find("b")->completed_);
}

TEST(PendingRpcMapTest, EmplaceOverwritesExisting) {
    TestRpcMap map;
    auto first = CMMakeShared<TestPending>();
    map.emplace("k", first);
    auto second = CMMakeShared<TestPending>();
    second->payload_ = "v2";
    map.emplace("k", second);
    EXPECT_EQ(map.find("k")->payload_, "v2");
}

// ── lost wakeup 确定性用例 ──────────────────────────────────────────────
//
// 窗口：waiter 持锁查 pred（false）→ 【notifier 无锁写 + 无锁 notify 落空】→
// waiter 进入 wait → 卡满超时。pre_sleep_hook_ + latch 把 waiter 钉死在窗口内。
//
// 本用例曾以"锁外写 completed_ + 无锁 notify_all()"（旧 on_var_ack 两阶段
// 序列）复现该窗口并卡满超时（red，确定性，elapsed=1000ms）；修复删除了
// 无锁 notify 接口后改写为 complete() 持锁完成路径，作为永久防回归：
// 任何把完成路径改回锁外 notify 的改动都会让本用例重新卡满超时失败。
TEST(PendingRpcMapTest, CompleteDuringPredicateWindowWakesWaiter) {
    TestRpcMap map;
    auto pending = CMMakeShared<TestPending>();
    map.emplace("k", pending);

    std::latch in_window(1);
    std::atomic<bool> release{false};
    map.pre_sleep_hook_ = [&] {
        in_window.count_down();
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::yield();  // 持锁自旋等放行
        }
    };

    CMSharedPtr<TestPending> result;
    auto waiter = std::thread([&] {
        result = map.wait_for("k", std::chrono::milliseconds(3000),
                              [](const CMSharedPtr<TestPending>& p) { return p->completed_; });
    });

    in_window.wait();  // waiter 处于"已查 pred=false、未入 wait"窗口（持锁自旋）

    auto start = std::chrono::steady_clock::now();
    // complete 持锁：先等 waiter 释放锁（进入 wait），再写字段 + notify。
    // 若完成路径被改回无锁 notify（waiter 未 sleep 时 notify 落空），
    // waiter 将卡满 3s 超时，本用例的耗时断言失败。
    release.store(true, std::memory_order_release);
    map.complete("k", [](TestPending& p) {
        p.completed_ = true;
        p.success_ = true;
    });

    waiter.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->completed_);
    EXPECT_TRUE(result->success_);
}
