// Task 调度热循环 micro-benchmark
//
// 目的：测量 schedule_next / schedule_all_available / get_ready_tasks 的热循环开销。
// H2 瓶颈（schedule_next 冗余重取 idle/ready + get_ready_tasks 每次 sort）只在
// 高 ready 积压场景下显现，单 task 测试看不出差异。
//
// 场景：
//   A. 大批次调度吞吐：N 个 ready task + M 个 idle worker，调 schedule_all_available
//      一次性消费，测 task/sec。突出 schedule_next 循环内冗余 idle/ready 重取。
//   B. get_ready_tasks sort 开销：不同 ready 集大小下反复调 get_ready_tasks，测 ops/sec。
//   C. 反复全量调度（模拟 attr-tick 200ms 周期 + 每次 task complete 触发）：
//      重建状态 + schedule_all_available 多轮，测总吞吐。
//
// 指标：固定轮次取中位数。纯内存调度（无网络/磁盘/Python），精确隔离调度逻辑开销。
//
// 运行：./fly.sh test //src/task/tests:scheduling_hotloop_bench

#include <gtest/gtest.h>
#include <task/cpp/task_scheduler.h>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <numeric>
#include <vector>

namespace fly {

namespace {

constexpr int kBenchRounds = 7;   // 轮次取中位数（奇数）

template <typename T>
T median(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void print_result(const char* scenario, const std::vector<std::pair<int, double>>& results,
                  const char* unit) {
    printf("\n=== BENCH: %s ===\n", scenario);
    for (auto& [n, val] : results) {
        printf("  size=%-5d  %s=%.0f\n", n, unit, val);
    }
    printf("=== END %s ===\n\n", scenario);
    fflush(stdout);
}

// 填充 N 个 ready task（无依赖、无 capability，纯 FIFO；可选混合优先级让 sort 真正重排）。
// task_id 从 base 起，保证跨 cycle 唯一（避免 graph 内 completed_tasks_ 冲突）。
uint64_t fill_ready_tasks(DependencyGraph& g, int n_tasks, bool mixed_priority,
                          uint64_t base = 1) {
    for (int i = 0; i < n_tasks; i++) {
        TaskRequirements r;
        if (mixed_priority) {
            r.priority_ = static_cast<int>((base + i) % 20) + 1;  // 1..20 混合
        }
        g.add_task(base + i, {}, r);  // 无依赖 → 直接 ready
    }
    return base + n_tasks;
}

void fill_idle_workers(WorkerManager& m, int n_workers) {
    for (int w = 1; w <= n_workers; w++) {
        m.register_worker(static_cast<uint64_t>(w), "10.0.0." + std::to_string(w),
                          8000 + w, {});
    }
}

// 重置所有 worker 为 IDLE（schedule_all_available 会 assign 设 BUSY，下一 cycle 需重置）。
void reset_workers_idle(WorkerManager& m, int n_workers) {
    for (int w = 1; w <= n_workers; w++) {
        m.complete_task(static_cast<uint64_t>(w));  // BUSY → IDLE
    }
}

// 场景 A：大批次调度吞吐
// N 个 ready task（混合优先级）+ M 个 idle worker，schedule_all_available 一次性消费。
// 突出 schedule_next 循环内冗余重取（每 task 触发 ~3x idle 扫描 + idle_set 重建）。
TEST(SchedulingHotLoopBench, AScheduleAllAvailableThroughput) {
    struct Case { int tasks; int workers; const char* label; };
    std::vector<Case> cases = {
        {50, 8, "50t_8w"},
        {200, 16, "200t_16w"},
        {1000, 32, "1000t_32w"},
    };
    std::vector<std::pair<int, double>> results;
    for (auto& c : cases) {
        std::vector<double> per_round;
        for (int round = 0; round < kBenchRounds; round++) {
            DependencyGraph graph;
            WorkerManager manager;
            fill_ready_tasks(graph, c.tasks, /*mixed_priority=*/true);
            fill_idle_workers(manager, c.workers);
            TaskScheduler scheduler(&graph, &manager);
            scheduler.set_locality_preference(false);

            auto t0 = std::chrono::steady_clock::now();
            auto res = scheduler.schedule_all_available();
            auto t1 = std::chrono::steady_clock::now();

            double secs = std::chrono::duration<double>(t1 - t0).count();
            size_t scheduled = res.size();  // worker 用完即止，可能 < tasks
            per_round.push_back(static_cast<double>(scheduled) / secs);
        }
        double med = median(per_round);
        printf("  [A] %s: scheduled/sec median=%.0f (tasks=%d workers=%d)\n",
               c.label, med, c.tasks, c.workers);
        fflush(stdout);
        results.push_back({c.tasks, med});
    }
    print_result("A_schedule_all_available_throughput", results, "tasks/sec");
}

// 场景 B：get_ready_tasks sort 开销
// 填充 N 个 ready task，反复调 get_ready_tasks（只读不消费），测 ops/sec。
// 突出每次 O(R log R) sort + 比较器内 task_requirements_ map find。
TEST(SchedulingHotLoopBench, BGetReadyTasksSortCost) {
    std::vector<int> sizes = {10, 100, 500, 2000};
    std::vector<std::pair<int, double>> results;
    constexpr int kOpsPerRound = 1000;
    for (int n : sizes) {
        std::vector<double> per_round;
        for (int round = 0; round < kBenchRounds; round++) {
            DependencyGraph graph;
            fill_ready_tasks(graph, n, /*mixed_priority=*/true);
            auto t0 = std::chrono::steady_clock::now();
            volatile uint64_t sink = 0;
            for (int i = 0; i < kOpsPerRound; i++) {
                auto r = graph.get_ready_tasks();
                sink += r.size();
            }
            auto t1 = std::chrono::steady_clock::now();
            (void)sink;
            double secs = std::chrono::duration<double>(t1 - t0).count();
            per_round.push_back(kOpsPerRound / secs);
        }
        double med = median(per_round);
        printf("  [B] ready_size=%d: get_ready_tasks ops/sec median=%.0f\n", n, med);
        fflush(stdout);
        results.push_back({n, med});
    }
    print_result("B_get_ready_tasks_sort_cost", results, "ops/sec");
}

// 场景 C：反复全量调度（模拟 attr-tick 200ms 周期 + 每次 task complete 触发 schedule_tasks）
// 每轮重建 ready+idle 状态 + schedule_all_available 消费，测总吞吐。
TEST(SchedulingHotLoopBench, CRepeatedScheduleCycles) {
    constexpr int kCyclesPerRound = 200;
    constexpr int kTasksPerCycle = 50;
    constexpr int kWorkers = 16;
    std::vector<double> per_round;
    for (int round = 0; round < kBenchRounds; round++) {
        WorkerManager manager;
        fill_idle_workers(manager, kWorkers);
        uint64_t base = 1;
        auto t0 = std::chrono::steady_clock::now();
        for (int cyc = 0; cyc < kCyclesPerRound; cyc++) {
            DependencyGraph graph;
            base = fill_ready_tasks(graph, kTasksPerCycle, /*mixed_priority=*/true, base);
            reset_workers_idle(manager, kWorkers);
            TaskScheduler scheduler(&graph, &manager);
            scheduler.set_locality_preference(false);
            scheduler.schedule_all_available();
        }
        auto t1 = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        per_round.push_back((kCyclesPerRound * kTasksPerCycle) / secs);
    }
    double med = median(per_round);
    printf("  [C] repeated cycles: total_tasks/sec median=%.0f "
           "(cycles=%d tasks/cycle=%d workers=%d)\n",
           med, kCyclesPerRound, kTasksPerCycle, kWorkers);
    fflush(stdout);
    print_result("C_repeated_schedule_cycles",
                 {{kCyclesPerRound * kTasksPerCycle, med}}, "tasks/sec");
}

}  // namespace
}  // namespace fly
