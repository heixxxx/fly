#include <storage/cpp/network_chunk_source.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/message_protocol.h>
#include <common/cpp/data_checksum.h>
#include <log/cpp/logger.h>
#include <cstring>

namespace fly {

NetworkChunkSource::NetworkChunkSource(CMSharedPtr<Transport> transport, int fd,
                                       const DataResponseMessage& meta, ReleaseFn release,
                                       uint64_t queue_byte_limit)
    : transport_(std::move(transport))
    , fd_(fd)
    , total_len_(meta.total_compressed_len_)
    , queue_byte_limit_(queue_byte_limit)
    , meta_trailer_len_(meta.trailer_len_)
    , meta_py_name_(meta.py_name_)
    , meta_write_hash_(meta.write_context_hash_)
    , release_fn_(std::move(release)) {
    // 块流边界 = total - trailer_len（B' 后 trailer_len 覆盖块表）。块表自身
    // 经 META 预解析不可得（表在 trailer 里、trailer 在流尾）——接收线程块级
    // 校验按块头自寻址（§14.1 A'：解析 16B 头取 comp 长度），表对账由消费端
    //（DecompressingStreamBuf/MemoryChunkSource 拿到全量后）执行。
    meta_comp_type_ = meta.chunk_compression_type_ != 0
                          ? static_cast<int>(meta.chunk_compression_type_) : -1;
}

NetworkChunkSource::~NetworkChunkSource() {
    stopping_.store(true);
    {
        // 持锁 notify（lost wakeup 防御，同 DataClientPool::stop 论证）。
        std::lock_guard<std::mutex> lk(q_mutex_);
        q_space_cv_.notify_all();
        q_data_cv_.notify_all();
    }
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    if (release_fn_ && !released_) {
        release_fn_(!stream_failed_);
        released_ = true;
    }
}

void NetworkChunkSource::start() {
    recv_thread_ = std::thread([this] { recv_loop(); });
}

bool NetworkChunkSource::failed() const {
    std::lock_guard<std::mutex> lk(q_mutex_);
    return stream_failed_;
}

int64_t NetworkChunkSource::pull(char* dst, size_t n) {
    size_t got = 0;
    std::unique_lock<std::mutex> lk(q_mutex_);
    while (true) {
        if (!queue_.empty()) {
            CMString& front = queue_.front();
            size_t take = std::min(n - got, front.size() - front_offset_);
            std::memcpy(dst + got, front.data() + front_offset_, take);
            front_offset_ += take;
            got += take;
            queue_bytes_ -= take;
            if (front_offset_ >= front.size()) {
                queue_.pop_front();
                front_offset_ = 0;
            }
            if (got == n) {
                lk.unlock();
                q_space_cv_.notify_one();
                return static_cast<int64_t>(got);
            }
            continue;
        }
        // 队列空：流终止则收尾，否则等数据。
        if (stream_done_) {
            return stream_failed_ ? -1 : static_cast<int64_t>(got);
        }
        if (got > 0) {
            return static_cast<int64_t>(got);  // 手头已有的先交付
        }
        if (stopping_.load()) {
            stream_failed_ = true;
            return -1;
        }
        // 队列空：接收线程的 space 谓词含 queue_.empty()——notify 唤醒
        //（否则极端 limit < 单块大小时双方互等死锁：push 等 space / pull 等 data）。
        q_space_cv_.notify_one();
        q_data_cv_.wait(lk);
    }
}

void NetworkChunkSource::push_block(const char* data, size_t n) {
    std::unique_lock<std::mutex> lk(q_mutex_);
    q_space_cv_.wait(lk, [this, n] {
        return stopping_.load() || stream_done_ ||
               queue_bytes_ + n <= queue_byte_limit_ || queue_.empty();
    });
    if (stopping_.load() || stream_done_) return;
    queue_.emplace_back(data, n);
    queue_bytes_ += n;
    lk.unlock();
    q_data_cv_.notify_one();
}

void NetworkChunkSource::finish_stream(bool healthy, const char* reason) {
    std::lock_guard<std::mutex> lk(q_mutex_);
    if (stream_done_) return;
    stream_done_ = true;
    stream_failed_ = !healthy;
    if (!healthy && reason) fail_reason_ = reason;
    // 持锁 notify（lost wakeup 防御）。
    q_data_cv_.notify_all();
}

// ── A'2 块级校验状态机（§14.1）──
//
// 帧是传输单位（server 4MB 切片，帧自带 record 内 offset）；块是校验单位
//（磁盘压缩块，16B 头自寻址）。帧边界与块边界互不重合——解析器跨帧持续：
//   WAIT_HEADER：累积 16B → 解析 [i32 unc][i32 comp][u64 crc]
//   WAIT_DATA：累积 comp 字节 → 验块 CRC → 好块交付 / 坏块 hole+resend
// trailer 尾部字节（block_area 之后）：直接透传交付（消费端解析 trailer）。

void NetworkChunkSource::feed_frame(const char* data, size_t n, uint64_t offset) {
    // block_area 边界：其后是 trailer（非块格式）——直接透传交付（消费端
    // 解析 trailer；META 已带元信息，此处字节为完整 record 重组所需）。
    const uint64_t block_area =
        meta_trailer_len_ > 0 && meta_trailer_len_ <= total_len_
            ? total_len_ - meta_trailer_len_ : total_len_;

    while (n > 0) {
        // trailer 区透传。
        if (offset >= block_area) {
            deliver_bytes(data, n, offset);
            return;
        }
        // 跨边界的帧：块区部分走解析器，trailer 部分透传。
        if (offset + n > block_area) {
            uint64_t head_n = block_area - offset;
            feed_frame(data, static_cast<size_t>(head_n), offset);
            deliver_bytes(data + head_n, n - static_cast<size_t>(head_n), block_area);
            return;
        }

        if (parse_need_ == 16 && parse_buf_.empty() && n >= 16) {
            // 快路径：块起点且帧内剩余 ≥16B——先探块头。帧 4MB / 块 256KB
            // 网格对齐时整块几乎总在帧内，原地解析免 parse_buf_ 全量 append
            // （每字节一次多余 memcpy，perf 采样传输路径 memcpy ~25% 的主
            // 成分之一）。块尾在后续帧才落慢路径状态机。
            int32_t comp;
            int32_t unc;
            std::memcpy(&unc, data, 4);
            std::memcpy(&comp, data + 4, 4);
            if (comp < 0 || unc < 0) {
                finish_stream(false, "corrupt block header (negative sizes)");
                return;
            }
            size_t full = 16 + static_cast<size_t>(comp);
            if (n >= full) {
                if (!consume_block(data, full, offset)) return;
                data += full;
                n -= full;
                offset += full;
                continue;
            }
        }

        if (parse_buf_.empty()) {
            parse_off_ = offset;  // 新块起点
        }
        size_t take = std::min(n, parse_need_ - parse_buf_.size());
        parse_buf_.append(data, take);
        data += take;
        n -= take;
        offset += take;

        if (parse_buf_.size() < parse_need_) return;  // 帧耗尽，等下一帧

        if (parse_need_ == 16) {
            // 块头完成 → 解析 comp 长度，进入数据阶段。
            int32_t comp;
            int32_t unc;
            std::memcpy(&unc, parse_buf_.data(), 4);
            std::memcpy(&comp, parse_buf_.data() + 4, 4);
            if (comp < 0 || unc < 0) {
                finish_stream(false, "corrupt block header (negative sizes)");
                return;
            }
            parse_need_ = 16 + static_cast<size_t>(comp);
            if (parse_buf_.size() < parse_need_) continue;  // comp==0 落块完成
        }

        // 块完成（跨帧累积）：验 CRC → 交付 / 请求 resend。
        if (!consume_block(parse_buf_.data(), parse_buf_.size(), parse_off_)) return;
        parse_buf_.clear();
        parse_need_ = 16;
    }
}

// 块完成处理：验 CRC → 好块交付 / 坏块 hole + resend 请求（每区间上限一次）。
bool NetworkChunkSource::consume_block(const char* blk, size_t block_len, uint64_t offset) {
    int32_t comp;
    std::memcpy(&comp, blk + 4, 4);
    uint64_t stored;
    std::memcpy(&stored, blk + 8, 8);
    uint64_t actual = data_checksum(blk + 16, block_len - 16);
    if (actual == stored) {
        deliver_bytes(blk, block_len, offset);
        return true;
    }
    ERR("[NCS-FATAL-DATA-CORRUPTION] bad block CRC: off={} len={} — resending once",
        offset, block_len);
    if (resent_offsets_.count(offset)) {
        finish_stream(false, "resent block still corrupt");
        return false;
    }
    resent_offsets_.insert(offset);
    ChunkResendMessage rs;
    rs.offset_ = offset;
    rs.length_ = block_len;
    CMString encoded = MessageProtocol::encode(rs);
    if (!transport_->send_all(fd_, encoded.data(), encoded.size())) {
        finish_stream(false, "resend request failed");
        return false;
    }
    hole_len_[offset] = block_len;  // 等 resend 帧填补
    return true;
}

// 好字节交付（含 resend 帧补的字节）：无洞直接按序；有洞 pending 暂存，
// 前沿连续时 drain。同 offset 重复（resend 迟到 + 原块重排）幂等跳过。
void NetworkChunkSource::deliver_bytes(const char* data, size_t n, uint64_t offset) {
    if (offset < next_off_) return;  // 重复字节：丢弃
    if (offset == next_off_ && hole_len_.empty()) {
        // 快路径：按序无洞。
        root_.update(data, n);
        received_ += n;
        push_block(data, n);
        next_off_ = offset + n;
        return;
    }
    pending_[offset].assign(data, n);
    pending_bytes_ += n;
    drain_pending();
}

void NetworkChunkSource::drain_pending() {
    auto it = pending_.find(next_off_);
    while (it != pending_.end()) {
        hole_len_.erase(next_off_);  // 洞被填（resend 到达）
        root_.update(it->second.data(), it->second.size());
        received_ += it->second.size();
        push_block(it->second.data(), it->second.size());
        pending_bytes_ -= it->second.size();
        next_off_ += it->second.size();
        pending_.erase(it);
        it = pending_.find(next_off_);
    }
}

void NetworkChunkSource::recv_loop() {
    bool digest_seen = false;

    while (true) {
        if (stream_done_) return;  // feed_frame 内部已判失败

        int r = read_one_frame();
        if (r < 0) {
            finish_stream(false, "frame header check failed");
            return;
        }
        if (r == 0) {
            finish_stream(false, "connection lost during chunk stream");
            return;
        }

        if (r == 2) {  // DIGEST
            if (digest_seen) {
                finish_stream(false, "duplicate digest frame");
                return;
            }
            digest_seen = true;
            if (hole_len_.empty() && parse_buf_.size() < parse_need_) break;  // 干净完成
            continue;  // 有洞/块未齐：等重传帧
        }

        // r == 1：数据帧（帧 CRC 已过验）→ 块解析器。
        feed_frame(frame_raw_->data(), frame_raw_->size(), frame_off_);
        if (digest_seen && hole_len_.empty() && parse_buf_.size() < parse_need_) break;
    }

    // 流尾复核（计数 / 根失配）。T5（2026-08-31）：serve root_crc_ 发 0 =
    // 未计算（L0 块 CRC + trailer 已承担完整性）——expected 为 0 跳过根复核，
    // 兼容旧 serve（非 0 照验）。
    if (received_ != total_len_) {
        finish_stream(false, "byte count mismatch");
        return;
    }
    if (root_expected_ != 0 && root_.final() != root_expected_) {
        ERR("[NCS-FATAL-DATA-CORRUPTION] digest mismatch: expected={:016x} actual={:016x}",
            root_expected_, root_.final());
        finish_stream(false, "digest mismatch");
        return;
    }
    finish_stream(true, nullptr);
}

int NetworkChunkSource::read_one_frame() {
    // 返回：1=数据帧（CRC 过验，frame_raw_/frame_off_ 填充）；2=DIGEST；
    // 0=断连/失步；-1=帧头 check 失败。
    char fh[9];
    if (!recv_exact(transport_.get(), fd_, fh, 9)) return 0;
    uint64_t tl = 0;
    if (!parse_frame_header(fh, tl)) return -1;
    uint8_t type = static_cast<uint8_t>(fh[8]);

    if (type == static_cast<uint8_t>(MessageType::DATA_CHUNK)) {
        char sh[4];
        if (!recv_exact(transport_.get(), fd_, sh, 4)) return 0;
        uint32_t small_len = read_be32(sh);
        if (small_len != ChunkFrameProtocol::kSmallFieldsLen) return 0;
        char sf[16];
        if (!recv_exact(transport_.get(), fd_, sf, 16)) return 0;
        uint64_t foff = 0;
        uint64_t fcrc = 0;
        ChunkFrameProtocol::parse_small_fields(sf, small_len, foff, fcrc);
        uint64_t raw_len = ChunkFrameProtocol::raw_len_from_total(tl);
        if (raw_len == 0 || foff + raw_len > total_len_) {
            return 0;  // offset/长度越界 = 协议失步
        }
        // 帧缓冲跨帧复用：流内帧长恒定（尾帧除外），第二帧起 resize 为
        // no-op。原每帧新建 vector——4MB 构造清零后立即被 recv 覆盖，纯浪费。
        // feed_frame/deliver_bytes 均同步拷出（parse_buf_ append / 队列
        // emplace 构造），复用无异步持有风险。
        if (!frame_raw_) frame_raw_ = CMMakeShared<FlyBuffer>();
        frame_raw_->resize(raw_len);
        if (!recv_exact(transport_.get(), fd_, frame_raw_->data(), raw_len)) return 0;
        frame_off_ = foff;
        // §4.4 帧片 CRC 可选验证（2026-08-30 裁定）：0=发送端未计算，跳过
        // 帧级验证——本流式路径的完整性由 feed_frame 块级 CRC 权威校验承担
        // （坏块 → hole → 块区间 resend）；非 0（旧协议端）保留帧级快速检测
        // + 整帧 resend（offset 寻址；帧内多块统一按区间重传）。
        if (fcrc != 0 &&
            data_checksum(frame_raw_->data(), frame_raw_->size()) != fcrc) {
            // 帧坏：整帧 resend（offset 寻址）。帧内多块统一按区间重传。
            ERR("[NCS] bad frame CRC: off={} len={} — resending once", foff, raw_len);
            if (resent_offsets_.count(foff)) {
                finish_stream(false, "resent frame still corrupt");
                return 0;
            }
            resent_offsets_.insert(foff);
            ChunkResendMessage rs;
            rs.offset_ = foff;
            rs.length_ = raw_len;
            CMString encoded = MessageProtocol::encode(rs);
            if (!transport_->send_all(fd_, encoded.data(), encoded.size())) {
                finish_stream(false, "resend request failed");
                return 0;
            }
            return 1;  // 帧字节不喂解析器（丢弃）——resend 帧会补
        }
        return 1;
    }

    if (type == static_cast<uint8_t>(MessageType::DATA_DIGEST)) {
        uint64_t payload_len = tl - 1;
        CMString payload(static_cast<size_t>(payload_len), '\0');
        if (!recv_exact(transport_.get(), fd_, payload.data(),
                        static_cast<size_t>(payload_len))) {
            return 0;
        }
        CMString frame_buf;
        frame_buf.assign(fh, 9);
        frame_buf += payload;
        DataDigestMessage digest;
        if (!MessageProtocol::decode(frame_buf, digest)) return 0;
        root_expected_ = digest.root_crc_;
        digest_chunks_ = digest.chunk_count_;
        return 2;
    }

    return 0;
}

}  // namespace fly
