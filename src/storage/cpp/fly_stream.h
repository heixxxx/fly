#pragma once
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <storage/cpp/compressor.h>
#include <common/buffer/cpp/fly_buffer.h>
#include <common/io/cpp/chunk_source.h>
#include <common/serialization/cpp/object_header.h>
#include <container/cpp/container_aliases.h>
#include <functional>
#include <ostream>
#include <istream>

class FlyStream {
public:
    // compression_threshold: payloads at or below this size skip compression.
    // Defaults to 4096 to match Database; pass 0 to force compression of every
    // payload.
    FlyStream(CompressionType comp_type, int64_t chunk_size, const CMString& py_name = {},
              int64_t compression_threshold = 4096);
    explicit FlyStream(FlyBufferPtr data);
    // 流式读模式（L3 §8.1）：拉取源（网络分片流/内存源）+ 块流边界驱动
    // DecompressingStreamBuf——Unpickler 增量消费，不整缓冲。
    FlyStream(CMSharedPtr<fly::ChunkSource> source, uint64_t block_area_len);
    // L1 流式写（§9.1 #40）：sink 模式——压缩流逐块回调（"压缩一块、交付
    // 一块"，sink = 增量盘写/逐块 WBQ）。finish_sink 时 trailer 也走 sink。
    // commit_fn（可选）：finish_and_commit 时调用（Database 的完成编排）。
    FlyStream(CompressionType comp_type, int64_t chunk_size,
              std::function<void(const char*, size_t)> chunk_sink,
              const CMString& py_name = {}, int64_t compression_threshold = 4096,
              std::function<int64_t(int64_t, int32_t, bool, bool)> commit_fn = nullptr);
    ~FlyStream();
    FlyStream(const FlyStream&) = delete;
    FlyStream& operator=(const FlyStream&) = delete;

    void write(const char* data, size_t size);
    void flush();
    FlyBufferPtr finish_write();
    // sink 模式完成：flush + trailer 走 sink；返回元数据（commit 用）。
    void finish_sink();
    // sink 模式完成 + commit 回调（total/chunks 传入；backup/populate 由
    // Python 侧来）。返回 WriteErrorType 值；无 commit_fn 返回 0。
    int64_t finish_and_commit(bool backup, bool populate_cache);
    CMString read(size_t n);
    CMString read_all();
    CMString readline();
    size_t readinto(char* dst, size_t dst_size);
    int64_t total_uncompressed() const;
    int32_t chunk_count() const;
    // 读模式：源元数据 py_name（META/trailer 解析——open 返回即有效）。
    // read_object 单拉分流用（双拉修复 2026-08-30）。写模式返回写入 py_name。
    CMString py_name() const;
    // temp 标记（缓存双池路由 2026-08-30）：读模式由源携带（本地 local_idx
    // 判定 / META 告知）；写模式 false。
    bool stream_is_temp() const { return stream_is_temp_; }
    bool is_write_mode() const { return is_write_mode_; }
    // 读模式：任一校验失败（trailer/块 CRC/结构越界）为 true——Python 面
    // 读完后必须检查（零容忍语义，§4.4/§5）。
    bool checksum_failed() const;
    // 失败归类："io: ..."/"integrity: ..."（io = 源 IO/网络失败，非数据损坏；
    // 未失败为空）。Python 面据此分流 FATAL 文案。
    CMString failure_detail() const;
    // sink 写模式元数据（finish_sink 后有效；commit_incremental 消费）。
    int64_t sink_total_uncompressed() const { return sink_total_; }
    int32_t sink_chunk_count() const { return sink_chunks_; }
    uint8_t sink_effective_compression() const { return sink_comp_; }

private:
    bool is_write_mode_;
    FlyBufferPtr write_buf_;
    CMUniquePtr<FlyBufferStreamBuf> fly_buf_sb_;
    CMUniquePtr<CountingStreamBuf> counting_sb_;
    CMUniquePtr<std::ostream> counting_os_;
    CMUniquePtr<CompressingStreamBuf> compress_sb_;
    CMUniquePtr<std::ostream> compress_os_;
    std::function<void(const char*, size_t)> chunk_sink_;  // L1 sink 写模式
    std::function<int64_t(int64_t, int32_t, bool, bool)> commit_fn_;  // sink 完成回调
    int64_t sink_total_ = 0;
    int32_t sink_chunks_ = 0;
    uint8_t sink_comp_ = 0;
    CMString py_name_;
    FlyBufferPtr read_buf_;
    CMSharedPtr<fly::ChunkSource> chunk_source_;  // 流式读模式（L3）
    bool stream_is_temp_ = false;                 // 源携带的 temp 标记（读模式）
    CMUniquePtr<DecompressingStreamBuf> decompress_sb_;
    CMUniquePtr<std::istream> decompress_is_;
};
