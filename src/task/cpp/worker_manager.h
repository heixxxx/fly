#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <functional>

namespace fly {

enum class WorkerStatus : uint8_t {
    IDLE = 0,
    BUSY = 1,
    DEAD = 2,
};

struct WorkerInfo {
    uint64_t worker_id_;
    CMString address_;
    uint16_t port_;
    WorkerStatus status_;
    CMVector<CMString> capabilities_;
    uint64_t last_heartbeat_;
    uint64_t current_task_id_;
    // hostname/ip 是 worker 的网络拓扑属性，与调度状态同属一个 worker 的固有信息。
    // 此前散落在 master 的并行 map（worker_to_hostname_/worker_to_ip_）且无锁访问，
    // 收编进 WorkerInfo 后由 WorkerManager::mutex_ 统一保护，消除数据竞争。
    CMString hostname_;
    CMString ip_address_;
};

class WorkerManager {
public:
    void register_worker(uint64_t worker_id, const CMString& address, uint16_t port,
                          const CMVector<CMString>& capabilities = {},
                          const CMString& hostname = "",
                          const CMString& ip_address = "");
    void register_worker(uint64_t worker_id, const CMString& address,
                          const CMVector<CMString>& capabilities);
    void unregister_worker(uint64_t worker_id);
    void update_worker_status(uint64_t worker_id, WorkerStatus status);
    void record_heartbeat(uint64_t worker_id);
    void set_heartbeat(uint64_t worker_id, uint64_t timestamp);
    void assign_task(uint64_t worker_id, uint64_t task_id);
    void complete_task(uint64_t worker_id);
    void update_capabilities(uint64_t worker_id,
                              const CMVector<CMString>& added,
                              const CMVector<CMString>& removed);
    // hostname 单独设置/查询（注册时未必已知，测试也用此模拟 worker 拓扑）。
    // 收编自原 master 的 worker_to_hostname_ 并行 map，统一受 mutex_ 保护。
    void set_hostname(uint64_t worker_id, const CMString& hostname, const CMString& ip_address = "");
    CMString get_hostname(uint64_t worker_id) const;
    CMString get_ip_address(uint64_t worker_id) const;

    std::optional<std::reference_wrapper<WorkerInfo>> get_worker(uint64_t worker_id);
    CMVector<uint64_t> get_idle_workers();
    CMVector<uint64_t> get_workers_with_capability(const CMString& capability);
    CMVector<WorkerInfo> get_all_workers();
    bool has_worker_with_all_capabilities(const CMVector<CMString>& capabilities) const;
    size_t get_worker_count();
    size_t get_idle_worker_count();
    // 诊断用：返回所有 worker 的 status/cap/task 简表（调度异常时定位状态不一致）。
    CMString debug_worker_status();

private:
    CMUnorderedMap<uint64_t, WorkerInfo> workers_;
    mutable std::mutex mutex_;
};

}  // namespace fly