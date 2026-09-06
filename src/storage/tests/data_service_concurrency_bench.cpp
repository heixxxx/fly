// DataService 并发 micro-benchmark
//
// 目的：测量 DataService 索引访问的锁并发性。锁优化（单 mutex → 分片 shared_mutex）
// 的收益只在并发争用下显现，单线程测试看不出差异。
//
// 场景（模拟 DataServer epoll 线程并发服务远程读的真实争用模式）：
//   A. 纯读并发：N 线程并发查 remote_idx + worker_registry（lookup_all_remote_idx 跨域读）
//   B. 读写混合：少量写线程（update_remote_idx）+ 大量读线程，测争用下读吞吐
//   C. local+remote 混合并发：一半线程查 local_idx，一半查 remote_idx（分片锁收益核心）
//
// 指标：固定时长内完成的操作数（ops/sec），多轮取中位数降噪。
// 仅用纯内存索引操作（lookup/get_remote_workers/has_remote_location），排除磁盘 IO 噪声，
// 精确隔离锁开销。
//
// 运行：./fly.sh test //src/storage/tests:data_service_concurrency_bench
//   （独立 target，不进常规 test 套件，仅按需运行测吞吐）

#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <container/cpp/container_aliases.h>
#include <common/testing/cpp/test_helpers.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <filesystem>

namespace {

constexpr int kBenchDurationMs = 500;       // 每轮测量时长
constexpr int kBenchRounds = 5;             // 轮次，取中位数
constexpr int kNumObjects = 200;            // 预填充对象数
constexpr int kNumWorkers = 8;              // 预注册 worker 数

// 中位数
template <typename T>
T median(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct BenchResult {
    double ops_per_sec;
    int threads;
};

void print_result(const char* scenario, const std::vector<BenchResult>& results) {
    std::vector<double> ops;
    for (auto& r : results) ops.push_back(r.ops_per_sec);
    double med = median(ops);
    double sum = std::accumulate(ops.begin(), ops.end(), 0.0);
    double mean = sum / ops.size();
    printf("\n=== BENCH: %s ===\n", scenario);
    for (auto& r : results) {
        printf("  threads=%-3d  ops/sec=%.0f\n", r.threads, r.ops_per_sec);
    }
    printf("  median=%.0f ops/sec  mean=%.0f ops/sec  (rounds=%d, dur=%dms)\n",
           med, mean, kBenchRounds, kBenchDurationMs);
    printf("=== END %s ===\n\n", scenario);
    fflush(stdout);
}

class DataServiceConcurrencyBench : public ::testing::Test {
protected:
    fly::CMSharedPtr<fly::DataService> ds_ = fly::DataService::instance();
    std::vector<CMString> remote_objects_;   // "db:obj_0" .. "db:obj_199"
    std::vector<CMString> local_objects_;    // "db:local_0" ..
    CMString db_path_ = fly::test::qa_tmp_dir("fly_bench_ds");

    void SetUp() override {
        std::filesystem::create_directories(db_path_);
        ds_->reset();
        ds_->register_database(db_path_, "", "bench_writer");

        // 预填充 remote_idx_：每个对象登记到多个 worker（多副本）。
        remote_objects_.clear();
        for (int i = 0; i < kNumObjects; i++) {
            CMString full = db_path_ + ":obj_" + std::to_string(i);
            remote_objects_.push_back(full);
            // 登记到 3 个 worker（模拟多副本），带 size。
            for (int w = 0; w < 3; w++) {
                uint64_t wid = static_cast<uint64_t>(w + 1);
                ds_->register_worker(wid, "10.0.0." + std::to_string(w + 1),
                                     8000 + static_cast<int32_t>(wid));
            }
            ds_->update_remote_idx(full, 1, "10.0.0.1", 8001, 1024 * 100);
            ds_->update_remote_idx(full, 2, "10.0.0.2", 8002, 1024 * 100);
            ds_->update_remote_idx(full, 3, "10.0.0.3", 8003, 1024 * 100);
        }

        // 预填充 local_idx_：COMPLETE 状态的本地对象（用 on_write_completed 登记 entries）。
        // 注：try_read_local_raw 命中 ObjectCache 会短路；这里不预填 cache，让 lookup 走索引。
        // 为避免磁盘读，benchmark 读路径用 has_local_object/is_write_in_progress（纯索引），
        // 不用 try_read_local_raw（会触发磁盘 IO，引入噪声）。
        local_objects_.clear();
        for (int i = 0; i < kNumObjects; i++) {
            CMString short_name = "local_" + std::to_string(i);
            CMString full = db_path_ + ":" + short_name;
            local_objects_.push_back(full);
            // 构造 entries 并登记为 COMPLETE。
            CMVector<IndexEntry> idx_entries;
            IndexEntry entry;
            entry.object_name_ = short_name;
            entry.file_name_ = "bench.dat";
            entry.offset_ = 0;
            entry.size_ = 100;
            entry.is_large_ = false;
            entry.block_count_ = 0;
            idx_entries.push_back(entry);
            ds_->on_write_started(db_path_, full);
            ds_->on_write_completed(db_path_, full, idx_entries);
        }
    }

    void TearDown() override {
        std::filesystem::remove_all(db_path_);
    }

    // 跑一个场景：N 线程并发执行 workload，持续 duration_ms，返回每线程 ops。
    std::vector<uint64_t> run_concurrent(int num_threads, int duration_ms,
                                          std::function<void(unsigned)> workload) {
        std::vector<std::thread> threads;
        std::vector<uint64_t> ops(num_threads, 0);
        std::atomic<bool> stop{false};

        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&, t]() {
                uint64_t local_ops = 0;
                unsigned seed = static_cast<unsigned>(t);
                while (!stop.load(std::memory_order_relaxed)) {
                    workload(seed);
                    local_ops++;
                }
                ops[t] = local_ops;
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        stop.store(true, std::memory_order_relaxed);

        for (auto& th : threads) th.join();
        return ops;
    }

    // 测一个场景在多个线程数下的吞吐，返回每轮结果。
    std::vector<BenchResult> bench_scenario(const char* name,
                                             std::function<void(unsigned)> workload,
                                             std::vector<int> thread_counts) {
        std::vector<BenchResult> results;
        for (int nthreads : thread_counts) {
            std::vector<double> per_round;
            for (int round = 0; round < kBenchRounds; round++) {
                auto ops = run_concurrent(nthreads, kBenchDurationMs, workload);
                uint64_t total = std::accumulate(ops.begin(), ops.end(), 0ULL);
                double secs = kBenchDurationMs / 1000.0;
                per_round.push_back(static_cast<double>(total) / secs);
            }
            double med = median(per_round);
            results.push_back({med, nthreads});
        }
        print_result(name, results);
        return results;
    }
};

// 场景 A：纯读并发 — lookup_all_remote_idx（remote_idx + worker_registry 跨域读）
// 这是优化前单 mutex 串行、优化后双 shared_lock 并发的关键场景。
// 循环遍历所有预填充对象，触发跨域 lookup。
TEST_F(DataServiceConcurrencyBench, ALookupAllRemoteIdxReadOnly) {
    auto workload = [&](unsigned seed) {
        (void)seed;
        for (const auto& full : remote_objects_) {
            auto replicas = ds_->lookup_all_remote_idx(full);
            // 防止编译器优化掉
            if (replicas.empty()) { __builtin_unreachable(); }
        }
    };
    bench_scenario("A_lookup_all_remote_idx_readonly", workload, {1, 2, 4, 8});
}

// 场景 B：读写混合 — 1 写线程 + N 读线程
// 写线程持续 update_remote_idx（模拟 on_task_complete 上报位置），读线程并发 lookup。
// 测锁争用下的读吞吐。
TEST_F(DataServiceConcurrencyBench, BRemoteIdxReadWriteMixed) {
    auto run = [&](int reader_threads) {
        std::vector<double> per_round;
        for (int round = 0; round < kBenchRounds; round++) {
            std::atomic<bool> stop{false};
            std::atomic<uint64_t> read_ops{0};

            // 1 写线程：循环 update 所有对象（改 size 触发 remote_idx 写）。
            std::thread writer([&]() {
                int i = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    const auto& full = remote_objects_[i % kNumObjects];
                    ds_->update_remote_idx(full, 1, "10.0.0.1", 8001,
                                           1024 * 100 + (i % 1000));
                    i++;
                }
            });

            // N 读线程
            std::vector<std::thread> readers;
            for (int t = 0; t < reader_threads; t++) {
                readers.emplace_back([&]() {
                    uint64_t local = 0;
                    while (!stop.load(std::memory_order_relaxed)) {
                        for (const auto& full : remote_objects_) {
                            ds_->lookup_all_remote_idx(full);
                        }
                        local++;
                    }
                    read_ops.fetch_add(local, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(kBenchDurationMs));
            stop.store(true);

            writer.join();
            for (auto& r : readers) r.join();

            double secs = kBenchDurationMs / 1000.0;
            per_round.push_back(static_cast<double>(read_ops.load()) / secs);
        }
        double med = median(per_round);
        char name[64];
        snprintf(name, sizeof(name), "B_readwrite_mixed(readers=%d,+1writer)", reader_threads);
        print_result(name, {{med, reader_threads}});
    };
    run(1); run(3); run(7);
}

// 场景 C：local + remote 混合并发
// 一半线程查 local_idx（has_local_object），一半查 remote_idx（lookup_all_remote_idx）。
// 这是分片锁收益核心：优化前单 mutex 把两类读串行化；优化后 local/remote 各自并发。
TEST_F(DataServiceConcurrencyBench, CLocalRemoteMixedConcurrent) {
    auto run = [&](int local_threads, int remote_threads) {
        std::vector<double> per_round;
        for (int round = 0; round < kBenchRounds; round++) {
            std::atomic<bool> stop{false};
            std::atomic<uint64_t> total_ops{0};

            auto local_work = [&]() {
                uint64_t local = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    for (const auto& full : local_objects_) {
                        // 纯索引查询，不触发磁盘 IO
                        if (ds_->has_local_object(full)) { local++; }
                    }
                }
                total_ops.fetch_add(local, std::memory_order_relaxed);
            };
            auto remote_work = [&]() {
                uint64_t local = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    for (const auto& full : remote_objects_) {
                        auto r = ds_->lookup_all_remote_idx(full);
                        if (!r.empty()) { local++; }
                    }
                }
                total_ops.fetch_add(local, std::memory_order_relaxed);
            };

            std::vector<std::thread> threads;
            for (int t = 0; t < local_threads; t++) threads.emplace_back(local_work);
            for (int t = 0; t < remote_threads; t++) threads.emplace_back(remote_work);

            std::this_thread::sleep_for(std::chrono::milliseconds(kBenchDurationMs));
            stop.store(true);
            for (auto& th : threads) th.join();

            double secs = kBenchDurationMs / 1000.0;
            per_round.push_back(static_cast<double>(total_ops.load()) / secs);
        }
        double med = median(per_round);
        char name[80];
        snprintf(name, sizeof(name), "C_local+remote_mixed(local=%d,remote=%d)",
                 local_threads, remote_threads);
        print_result(name, {{med, local_threads + remote_threads}});
    };
    run(1, 1); run(2, 2); run(4, 4);
}

// 场景 D：get_remote_workers + has_remote_location 并发（纯 remote_idx 单域读）
// 对照组：单域读优化前后差异主要来自 shared_lock vs mutex（读共享 vs 互斥）。
TEST_F(DataServiceConcurrencyBench, DRemoteIdxReadOnly) {
    auto workload = [&](unsigned seed) {
        (void)seed;
        for (const auto& full : remote_objects_) {
            auto workers = ds_->get_remote_workers(full);
            if (workers.empty()) { __builtin_unreachable(); }
            if (!ds_->has_remote_location(full)) { __builtin_unreachable(); }
        }
    };
    bench_scenario("D_get_remote_workers+has_remote_readonly", workload, {1, 2, 4, 8});
}

}  // namespace
