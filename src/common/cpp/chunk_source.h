#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

namespace fly {

// ── 拉取式输入源（L3 读侧流式，chunked-transfer-design.md §8.1）──
//
// DecompressingStreamBuf 的输入抽象：内存缓冲（现行为）与网络分片流
//（接收线程 + 有界队列）统一为 pull 语义。消费线程（任务线程）通过
// pull 驱动解压/反序列化；接收线程在后台推进网络 IO——真并行。
class ChunkSource {
public:
    virtual ~ChunkSource() = default;

    // 拉取至多 n 字节到 dst（阻塞直到有数据或 EOF）。
    // 返回实际拉取数：0 = EOF；<0 = 源侧失败（连接断/校验败——消费方
    // 必须按零容忍语义处理，不得当 EOF）。
    virtual int64_t pull(char* dst, size_t n) = 0;

    // 元数据（trailer 信息）：内存源构造即得；网络源由 META 提供
    //（server 发送前 pread 尾部解析——流式下消费端无法预先读流尾）。
    virtual const CMString& py_name() const = 0;
    virtual uint64_t total_uncompressed() const = 0;
    virtual uint32_t chunk_count() const = 0;
    // 压缩类型（int 为 CompressionType 值——避免 chunk_source.h 依赖
    // compressor.h；-1 = 未知（源失败））。
    virtual int compression_type() const = 0;

    // 源侧校验状态（帧 CRC/DIGEST 根）：流结束后查询；true = 源已坏，
    // 消费结果不可信（零容忍 §5）。
    virtual bool failed() const = 0;

    // temp 标记（缓存双池路由 2026-08-30）：网络源由 META 提供（远端
    // local_idx 判定——跨进程读取方本进程查不到 temp 属性）；本地源由
    // 构造点设置。默认非 temp。
    bool is_temp = false;
};

}  // namespace fly
