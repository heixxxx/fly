#pragma once

#include <common/cpp/chunk_source.h>
#include <common/cpp/data_checksum.h>
#include <network/cpp/message_types.h>
#include <common/cpp/common_types.h>
#include <common/cpp/fly_buffer.h>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace fly {

class Transport;

// ── L3 流式网络源（chunked-transfer-design.md §8.1）──
//
// [server] chunk 循环连续发送（不停）
//    │  TCP（流控 = 平滑降速器，非断流）
//    ▼
// [接收线程]（本类内部，纯 C++ 无 GIL）：recv 块帧 → 验帧 CRC → 坏片立即
//    CHUNK_RESEND（每 seq 上限一次）→ 好片 push 有界队列 + 增量根摘要
//    ▼  有界队列（stream_buffer_chunks 上限，压缩态字节计数）
// [消费线程 = 任务线程]：pull → DecompressingStreamBuf 解压 → Unpickler
//
// 消费不及时：队列吸收差值 → 持续慢消费才 TCP 降速到消费速率（代价为零：
// 消费是瓶颈时关键路径不变）。网络是瓶颈（常态）：队列浅、发送方全速。
// 校验类失败（帧头 check / 坏片重传仍坏 / DIGEST 根失配）→ failed_，pull
// 返回 -1——消费方按零容忍 §5 处理（一次重取 → 仍败 FATAL）。
class NetworkChunkSource : public ChunkSource {
public:
    // release_fd(healthy)：流终止（完成/失败/析构）时归还 fd 与 slot。
    // queue_byte_limit：有界队列上限（压缩态字节；默认 16 片 ≈ 64MB 由
    // 调用方从 config stream_buffer_chunks 计算）。
    using ReleaseFn = std::function<void(bool healthy)>;

    NetworkChunkSource(CMSharedPtr<Transport> transport, int fd,
                       const DataResponseMessage& meta, ReleaseFn release,
                       uint64_t queue_byte_limit);
    ~NetworkChunkSource() override;

    NetworkChunkSource(const NetworkChunkSource&) = delete;
    NetworkChunkSource& operator=(const NetworkChunkSource&) = delete;

    // 启动接收线程（构造后调用一次；析构自动 join）。
    void start();

    // ── ChunkSource 接口 ──
    int64_t pull(char* dst, size_t n) override;
    const CMString& py_name() const override { return meta_py_name_; }
    uint64_t total_uncompressed() const override { return meta_total_uncompressed_; }
    uint32_t chunk_count() const override { return meta_chunk_count_; }
    int compression_type() const override { return meta_comp_type_; }
    bool failed() const override;

    // 流失败原因（诊断；流成功为空）。
    const CMString& fail_reason() const {
        std::lock_guard<std::mutex> lk(q_mutex_);
        return fail_reason_;
    }

    // 失败归类（chunk_source.h 契约）：reason 串自带 "io:"/"integrity:" 前缀；
    // failed 但 reason 空（理论不可达）按 io 兜底。
    CMString failure_detail() const override {
        std::lock_guard<std::mutex> lk(q_mutex_);
        return stream_failed_ && fail_reason_.empty() ? CMString("io: stream failed")
                                                      : fail_reason_;
    }

    // 流式元数据（消费端组装 DecompressingStreamBuf 用）
    uint64_t block_area_len() const {
        return meta_trailer_len_ > 0 && meta_trailer_len_ <= total_len_
                   ? total_len_ - meta_trailer_len_ : 0;
    }
    const CMString& write_context_hash() const { return meta_write_hash_; }

private:
    void recv_loop();          // 接收线程主体
    int read_one_frame();      // 读一帧（frame_off_/frame_raw_ 填充；CRC 过验）
    void feed_frame(const char* data, size_t n, uint64_t offset);  // 块解析器
    // 块完成：验 CRC → 交付 / 请求 resend。false = 流终止（二次损坏或
    // resend 请求发送失败）。blk 指向完整块（16B 头 + comp 数据）。
    bool consume_block(const char* blk, size_t block_len, uint64_t offset);
    void deliver_bytes(const char* data, size_t n, uint64_t offset);  // 好字节交付
    void drain_pending();     // 按序前沿 drain
    void push_block(const char* data, size_t n);   // 有界入队（满则阻塞）
    void finish_stream(bool healthy, const char* reason);  // 终止 + 唤醒消费

    // 配置
    CMSharedPtr<Transport> transport_;
    int fd_;
    uint64_t total_len_;
    uint64_t queue_byte_limit_;
    uint64_t meta_trailer_len_;
    CMString meta_py_name_;
    uint64_t meta_total_uncompressed_ = 0;
    uint32_t meta_chunk_count_ = 0;
    int meta_comp_type_ = -1;
    CMString meta_write_hash_;
    ReleaseFn release_fn_;
    bool released_ = false;

    // 接收线程
    std::thread recv_thread_;
    std::atomic<bool> stopping_{false};

    // 有界队列（字节流）+ 终止状态（mutable：failed() const 持锁查询）
    mutable std::mutex q_mutex_;
    std::condition_variable q_space_cv_;   // 队列不满（接收线程等）
    std::condition_variable q_data_cv_;    // 队列非空/终止（消费线程等）
    std::deque<CMString> queue_;
    size_t front_offset_ = 0;              // 队头元素已消费偏移（部分交付）
    size_t queue_bytes_ = 0;
    bool stream_done_ = false;             // 接收线程结束（成功或失败）
    bool stream_failed_ = false;           // 校验/连接失败
    CMString fail_reason_;

    // 帧读取工作区（仅接收线程访问）
    uint64_t frame_off_ = 0;               // 帧首字节在 record 内偏移
    FlyBufferPtr frame_raw_;

    // A'2 块解析器状态（仅接收线程）
    CMString parse_buf_;                   // 当前块累积字节（跨帧）
    size_t parse_need_ = 16;               // 当前阶段需要的字节数（16 或 16+comp）
    uint64_t parse_off_ = 0;               // 当前块起点（record 内偏移）

    // 交付状态（仅接收线程）
    uint64_t received_ = 0;                // 已按序交付字节
    uint64_t next_off_ = 0;                // 按序推进锚点（下一期望字节 offset）
    CMUnorderedMap<uint64_t, CMString> pending_;      // 乱序/重传字节暂存
    CMUnorderedMap<uint64_t, uint64_t> hole_len_;     // 待补洞（resend 中）
    size_t pending_bytes_ = 0;
    CMUnorderedSet<uint64_t> resent_offsets_;  // 每区间重传上限一次
};

}  // namespace fly
