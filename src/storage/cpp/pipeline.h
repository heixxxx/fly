#pragma once

// 流式块管线（2026-08-31 流插件化定稿，性能分析文档 §流插件化方案）：
//
// 写/读方向的数据变换链以"块"为粒度组合。管线骨架持有：明文切块（写）/
// 块拉取重组（读）、Stage 序列、统计。Stage 为有状态插件（scratch 跨块
// 复用，零每块分配），可独立增删：
//
//   写方向:  明文流 ──切块──▶ [CompressStage] ─▶ [CrcStage] ─▶ [HeaderStage]
//                              (85% 规则 raw 直通)                │ emit
//   读方向:  source ◀──拉块── [CrcVerifyStage] ─▶ [DecompressStage] ─▶ 明文块
//
// 端点（sink / source）与元数据会话（文件 trailer / RPC START+END）由装配
// 方接入，不在管线内。两个装配实例：
//   文件:  make_file_write_pipeline(WBQ sink) / make_block_read_pipeline
//   RPC:   第 3 步接入（PeerFrameStage + START/END 会话）
//
// 字节格式契约（与既有 CompressingStreamBuf 输出逐字节一致，golden 锚定）：
//   块记录 = [i32 unc][i32 comp][u64 crc][payload]（LE 内存序）
//   crc 覆盖 payload；comp == unc 表示 raw 直通块（块级压缩率不达标）。

#include <common/cpp/data_checksum.h>
#include <common/cpp/common_types.h>
#include <storage/cpp/compressor.h>
#include <cstring>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace fly {

// 块变换载体：写方向 plain 入 / encoded 出；读方向 encoded 入 / plain 出。
// 视图生命周期：encoded 指向 stage 私有 scratch（下次 next_block/process
// 前有效）或调用方输入。零拷贝——raw 直通时 encoded 即 plain 视图。
struct BlockData {
    std::string_view plain;     // 明文
    std::string_view encoded;   // 当前交付/校验形态
    uint64_t crc = 0;
    uint32_t unc_size = 0;
    uint32_t comp_size = 0;
    bool raw = false;           // true: 未压缩块（comp == unc）
    bool tail = false;          // 写方向：流尾块（finish 刷出的块）
    bool failed = false;        // 读方向：CRC 失配/截断等（零容忍信号）
};

using EmitFn = std::function<void(const char* data, size_t n)>;
using PullFn = std::function<int64_t(char* dst, size_t n)>;  // <0 错误, 0 EOF

class WriteStage {
public:
    virtual ~WriteStage() = default;
    virtual void process(BlockData& b) = 0;
};

class ReadStage {
public:
    virtual ~ReadStage() = default;
    virtual void process(BlockData& b) = 0;
};

// ── 写方向 Stage ──

// 压缩插件：明文 → 压缩输出；块级压缩率不足（输出 ≥ ratio_floor_pct% ×
// 明文）时放弃压缩——encoded 切回 plain 视图（零拷贝），comp == unc 隐式
// 标记 raw，省对端解压（float64 等高熵数据画像）。
// 流级阈值（raw_threshold）：尾块且 ≤ 阈值时整块 raw 直通——复刻旧
// "首块小对象跳过压缩"语义（skip ⟺ 单块流且 ≤ 阈值）。
class CompressStage : public WriteStage {
public:
    CompressStage(CMUniquePtr<Compressor> compressor, int ratio_floor_pct = 85,
                  int64_t raw_threshold = -1)
        : compressor_(std::move(compressor)),
          ratio_floor_pct_(ratio_floor_pct),
          raw_threshold_(raw_threshold) {}

    void process(BlockData& b) override {
        if (!compressor_ || ratio_floor_pct_ <= 0 ||
            (b.tail && raw_threshold_ >= 0 &&
             static_cast<int64_t>(b.unc_size) <= raw_threshold_)) {
            b.raw = true;
            b.comp_size = b.unc_size;
            b.encoded = b.plain;
            return;
        }
        const CompressedChunk chunk = compressor_->compress(b.plain);
        if (chunk.compressed_size_ >= 0 &&
            static_cast<size_t>(chunk.compressed_size_) * 100 <
                static_cast<size_t>(b.unc_size) * static_cast<size_t>(ratio_floor_pct_)) {
            chunk_data_ = std::move(chunk.data_);  // 持有至下次 process（emit 同步消费）
            b.comp_size = static_cast<uint32_t>(chunk.compressed_size_);
            b.encoded = chunk_data_;
            b.raw = false;
        } else {
            // 压缩率不达标：raw 直通，压缩输出丢弃（仅本端 CPU 消耗，零额外拷贝）。
            b.raw = true;
            b.comp_size = b.unc_size;
            b.encoded = b.plain;
        }
    }

private:
    CMUniquePtr<Compressor> compressor_;
    int ratio_floor_pct_;
    int64_t raw_threshold_;
    CMString chunk_data_;
};

// 完整性插件：对当前交付字节（压缩态或 raw）计算 CRC——写入时刻锚点，
// 覆盖 磁盘→server→网络→client→解压 全生命周期（零容忍权威校验）。
class CrcStage : public WriteStage {
public:
    void process(BlockData& b) override {
        b.crc = data_checksum(b.encoded.data(), b.encoded.size());
    }
};

// 块格式化插件（末端）：产出块记录字节并交给下游 emit。
class BlockHeaderStage : public WriteStage {
public:
    explicit BlockHeaderStage(EmitFn emit) : emit_(std::move(emit)) {}

    void process(BlockData& b) override {
        emit_(reinterpret_cast<const char*>(&b.unc_size), sizeof(b.unc_size));
        emit_(reinterpret_cast<const char*>(&b.comp_size), sizeof(b.comp_size));
        emit_(reinterpret_cast<const char*>(&b.crc), sizeof(b.crc));
        if (!b.encoded.empty()) {
            emit_(b.encoded.data(), b.encoded.size());
        }
    }

private:
    EmitFn emit_;
};

// ── 读方向 Stage ──

// 完整性插件：CRC 验证（失配置 failed——零容忍，消费方必须按损坏处理）。
class CrcVerifyStage : public ReadStage {
public:
    void process(BlockData& b) override {
        if (data_checksum(b.encoded.data(), b.encoded.size()) != b.crc) {
            b.failed = true;
        }
    }
};

// 解压插件：comp < unc 解压到私有缓冲；comp == unc raw 直通（视图零拷贝）。
class DecompressStage : public ReadStage {
public:
    explicit DecompressStage(CMUniquePtr<Compressor> compressor)
        : compressor_(std::move(compressor)) {}

    void process(BlockData& b) override {
        if (!compressor_ || b.comp_size == b.unc_size) {
            b.raw = true;        // 读方向：直通块标记（comp == unc）
            b.plain = b.encoded;
            return;
        }
        b.raw = false;
        plain_.resize(static_cast<size_t>(b.unc_size));
        const int32_t written = compressor_->decompress_to(
            {b.encoded.data(), b.comp_size}, plain_.data(), b.unc_size);
        if (written < 0 || written != b.unc_size) {
            b.failed = true;  // CRC 已过验但解压失败 = 实现层缺陷，零容忍
            return;
        }
        b.plain = std::string_view(plain_.data(), static_cast<size_t>(written));
    }

private:
    CMUniquePtr<Compressor> compressor_;
    std::vector<char> plain_;
};

// ── 管线（切块/拉块 + Stage 序列 + 统计）──

class WritePipeline {
public:
    WritePipeline(std::vector<std::unique_ptr<WriteStage>> stages,
                  int64_t chunk_size, EmitFn emit)
        : stages_(std::move(stages)), chunk_size_(chunk_size), emit_(std::move(emit)) {}

    // 追加明文字节；内部按 chunk_size 切块逐块走 Stage → emit。
    void write(const char* data, size_t n) {
        while (n > 0) {
            if (buf_.size() == static_cast<size_t>(chunk_size_)) {
                flush_block();
            }
            const size_t take = std::min<size_t>(
                static_cast<size_t>(chunk_size_) - buf_.size(), n);
            buf_.insert(buf_.end(), data, data + take);
            data += take;
            n -= take;
            if (buf_.size() == static_cast<size_t>(chunk_size_)) {
                flush_block();
            }
        }
    }

    // 刷出尾块（不足 chunk_size 的剩余）。空流无块。尾块带 tail 标记
    // （CompressStage 的流级 raw 阈值语义依赖它）。
    void finish() {
        if (!buf_.empty()) {
            ctx_.tail = true;
            flush_block();
        }
    }

    uint64_t total_uncompressed() const { return total_uncompressed_; }
    uint32_t chunk_count() const { return chunk_count_; }
    uint32_t raw_blocks() const { return raw_blocks_; }
    bool all_raw() const { return chunk_count_ > 0 && raw_blocks_ == chunk_count_; }
    const std::vector<uint32_t>& block_comp_lens() const { return block_comp_lens_; }

private:
    void flush_block() {
        ctx_.plain = std::string_view(buf_.data(), buf_.size());
        ctx_.unc_size = static_cast<uint32_t>(buf_.size());
        // 默认 raw 直通（无压缩 Stage 时块的交付形态）；CompressStage 覆盖。
        ctx_.encoded = ctx_.plain;
        ctx_.comp_size = ctx_.unc_size;
        ctx_.raw = true;
        for (auto& stage : stages_) {
            stage->process(ctx_);
        }
        ctx_.tail = false;
        total_uncompressed_ += buf_.size();
        chunk_count_++;
        if (ctx_.raw) raw_blocks_++;
        block_comp_lens_.push_back(ctx_.comp_size);
        buf_.clear();
    }

    std::vector<std::unique_ptr<WriteStage>> stages_;
    int64_t chunk_size_;
    EmitFn emit_;
    std::vector<char> buf_;
    BlockData ctx_;
    uint64_t total_uncompressed_ = 0;
    uint32_t chunk_count_ = 0;
    uint32_t raw_blocks_ = 0;
    std::vector<uint32_t> block_comp_lens_;
};

// 拉取式读管线：从 source 逐块还原明文。next_block 返回 false = EOF 或
// 损坏（failed() 区分——损坏按零容忍处理，不得当作正常 EOF）。
// 块中截断（块头/数据不完整）= failed；仅拉块头起点处的干净耗尽 = EOF。
// out.plain 有效期至下次 next_block。
class ReadPipeline {
public:
    // wire 块 size 上界（磁盘位翻转/坏流 garbage 防御）：远大于写侧任何
    // 合法块（4MB 切块），远小于 2^32。pipeline.cpp 同步用。
    static constexpr uint32_t kMaxWireBlockBytes = 64u * 1024 * 1024;

    ReadPipeline(std::vector<std::unique_ptr<ReadStage>> stages, PullFn pull)
        : stages_(std::move(stages)), pull_(std::move(pull)) {}

    bool next_block(BlockData& out) {
        if (failed_ || eof_) return false;
        char hdr[16];
        if (!pull_exact(hdr, sizeof(hdr))) {
            // 恰好耗尽（一次未取到任何字节）= 干净 EOF；部分字节后截断 =
            // 结构损坏（零容忍），不静默当作 EOF。
            if (hdr_partial_) {
                failed_ = true;
            } else {
                eof_ = true;
            }
            return false;
        }
        hdr_partial_ = false;
        BlockData b;
        std::memcpy(&b.unc_size, hdr, sizeof(b.unc_size));
        std::memcpy(&b.comp_size, hdr + sizeof(b.unc_size), sizeof(b.comp_size));
        std::memcpy(&b.crc, hdr + sizeof(b.unc_size) + sizeof(b.comp_size),
                    sizeof(b.crc));
        // wire 块头上界校验（磁盘位翻转/坏流可把 size 解成 0xFFFFFFFF 级
        // garbage——resize 巨值 = 未捕获 bad_alloc。CRC 验证在 resize 之后，
        // 挡不住这一步）。上界 64MB：远大于写侧任何合法块（4MB 切块），
        // 远小于 2^32 garbage。comp_size==0 合法（空块防御的 header-only 记录）。
        if (b.unc_size > kMaxWireBlockBytes || b.comp_size > kMaxWireBlockBytes) {
            failed_ = true;
            return false;
        }
        scratch_.resize(b.comp_size);
        if (!pull_exact(scratch_.data(), b.comp_size)) {
            failed_ = true;  // 块数据截断 = 结构损坏
            return false;
        }
        b.encoded = std::string_view(scratch_.data(), scratch_.size());
        for (auto& stage : stages_) {
            stage->process(b);
            if (b.failed) {
                failed_ = true;
                return false;
            }
        }
        out = std::move(b);
        return true;
    }

    bool failed() const { return failed_; }

private:
    bool pull_exact(char* dst, size_t n) {
        size_t got = 0;
        while (got < n) {
            const int64_t r = pull_(dst + got, n - got);
            if (r <= 0) return false;
            got += static_cast<size_t>(r);
            if (got > 0 && got < n) hdr_partial_ = true;
        }
        return true;
    }

    std::vector<std::unique_ptr<ReadStage>> stages_;
    PullFn pull_;
    std::vector<char> scratch_;
    bool hdr_partial_ = false;
    bool failed_ = false;
    bool eof_ = false;
};

// ── 装配工厂 ──

// 文件模式写管线：压缩（含块级压缩率直通 + 流级 raw 阈值）→ CRC →
// 块格式化 → sink。compressor 为空 = NONE（全 raw 直通）。
// ratio_floor_pct：块级压缩率下限（输出 ≥ 该百分比 × 明文则 raw 直通）；
// INT_MAX 复刻旧"无条件采用压缩输出"行为。
// raw_threshold：流级阈值——尾块 ≤ 阈值时整块 raw（复刻旧"小对象跳过
// 压缩"语义）；<0 禁用。
WritePipeline make_file_write_pipeline(CMUniquePtr<Compressor> compressor,
                                       int64_t chunk_size, EmitFn sink,
                                       int ratio_floor_pct = 85,
                                       int64_t raw_threshold = -1);

// 压缩块流读管线：CRC 验证 → 解压（comp == unc 块自动直通）。
ReadPipeline make_block_read_pipeline(CompressionType comp, PullFn pull);

// 压缩块流读管线：CRC 验证 → 解压（comp == unc 块自动直通）。
ReadPipeline make_block_read_pipeline(CompressionType comp, PullFn pull);

}  // namespace fly
