#pragma once

#include <atomic>
#include <cstdint>

namespace fly {

// 本进程 TCP 收发字节累计计数器（cluster monitor 网络负载口径）。
//
// 插桩点是 TCPSocketTransport 的 send/send_all/sendv/recv 四个方法——
// 全部 TCP 流量（Reactor 消息面 / DataServer 数据面 / DataClientPool /
// metadata_client / 带宽探测）的唯一咽喉点，按 syscall 实际返回的正数值
// 累计。monitor 采样线程读取快照，样本间差分即速率。
//
// 计数器跨进程不共享（每进程独立），进程重启归零；GUI 侧对相邻样本做
// 单调性检查兜底（后值 < 前值视为计数器重启，速率记 0）。
class NetStats {
public:
    static NetStats& instance() {
        static NetStats s;
        return s;
    }

    void add_read(uint64_t n) { read_bytes_.fetch_add(n, std::memory_order_relaxed); }
    void add_write(uint64_t n) { write_bytes_.fetch_add(n, std::memory_order_relaxed); }

    uint64_t read_bytes() const { return read_bytes_.load(std::memory_order_relaxed); }
    uint64_t write_bytes() const { return write_bytes_.load(std::memory_order_relaxed); }

    NetStats(const NetStats&) = delete;
    NetStats& operator=(const NetStats&) = delete;

private:
    NetStats() = default;

    std::atomic<uint64_t> read_bytes_{0};
    std::atomic<uint64_t> write_bytes_{0};
};

}  // namespace fly
