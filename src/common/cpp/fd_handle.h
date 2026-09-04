#pragma once

#include <common/cpp/common_types.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <sys/socket.h>
#include <unistd.h>

namespace fly {

// ── FdHandle：文件描述符的两层关闭语义所有权原语（issue 011 M1）──
//
// 设计动机（详见 docs/issues/011-tsan-datasever-stop-races.md 第四章）：
// 裸 int fd 跨线程在途使用存在「close 后编号复用 → 操作打到错误连接」
// 的结构性风险。FdHandle 用 shared_ptr 引用计数统一所有权：
//   - 在途使用者持 shared_ptr → fd 保证存活（引用计数语义）；
//   - shutdown() 是决策层关闭（协议层决定断开）：幂等、立即生效——对端
//     立刻收到关闭指示停止发新请求；滞留持有者的后续读写拿到
//     EPIPE/ECONNRESET，错误方向正确；
//   - 析构是引用计数层关闭：未 shutdown 先补 shutdown，然后经 closer
//     关闭（默认 ::close；池化场景由属主注入归还闭包）。
//
// 使用不变量：
//   1. 裸 fd 数字绝不允许跨越「无引用」边界——对 fd 的每次使用都必须
//      持有本对象的 shared_ptr，经 get() 取值；
//   2. 所有对 fd 的关闭必须经由本对象（shutdown/close_now/析构），
//      禁止绕过句柄直接 ::close（否则编号复用后 shutdown 会打错目标）；
//   3. get() 返回 -1 表示已关闭，调用方按「连接已断」处理。
class FdHandle {
public:
    using CloseFn = std::function<void(int)>;
    using Ptr = CMSharedPtr<FdHandle>;

    // 收编一个已打开的 fd（accept/connect/池借出产物）。closer 供池化
    // 场景注入归还闭包，默认真实 close。
    static Ptr adopt(int fd, CloseFn closer = {}) {
        return Ptr(new FdHandle(fd, std::move(closer)));
    }

    FdHandle(const FdHandle&) = delete;
    FdHandle& operator=(const FdHandle&) = delete;

    ~FdHandle() {
        close_now();
    }

    // 决策层关闭：幂等。对 socket 执行 ::shutdown(SHUT_RDWR)——对端立刻
    // 收到关闭指示；非 socket 描述符（::shutdown 返 ENOTSOCK）按已通知
    // 处理返回 false，无副作用。fd 已关闭返回 false。
    bool shutdown() {
        int fd;
        {
            // 与 close_now 串行：防止「读到旧 fd → close_now 关闭且编号被
            // 复用 → ::shutdown 打到复用者」的窗口。
            std::lock_guard<std::mutex> lk(state_mutex_);
            if (shutdown_done_.exchange(true)) return false;
            fd = fd_.load(std::memory_order_acquire);
            if (fd < 0) return false;
            ::shutdown(fd, SHUT_RDWR);
        }
        return true;
    }

    // 属主强制提前关闭（如连接池停机排空）。原子 exchange 保证与析构
    // 并发时也恰好关闭一次。
    void close_now() {
        int fd = fd_.exchange(-1, std::memory_order_acq_rel);
        if (fd >= 0) closer_(fd);
    }

    // 裸 fd 访问：仅限持有 shared_ptr 引用期间；-1 = 已关闭。
    int get() const { return fd_.load(std::memory_order_relaxed); }

    bool is_shutdown() const { return shutdown_done_.load(std::memory_order_relaxed); }

private:
    FdHandle(int fd, CloseFn closer)
        : fd_(fd), closer_(closer ? std::move(closer) : [](int f) { ::close(f); }) {}

    std::atomic<int> fd_;
    CloseFn closer_;
    std::mutex state_mutex_;               // shutdown 与 close_now 互斥（见 shutdown 注释）
    std::atomic<bool> shutdown_done_{false};
};

using FdHandlePtr = FdHandle::Ptr;

}  // namespace fly
