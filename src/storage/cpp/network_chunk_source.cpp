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
    , frame_bytes_(meta.chunk_frame_bytes_)
    , queue_byte_limit_(queue_byte_limit)
    , meta_trailer_len_(meta.trailer_len_)
    , meta_py_name_(meta.py_name_)
    , meta_write_hash_(meta.write_context_hash_)
    , release_fn_(std::move(release)) {
    // META 不直接携带 total_uncompressed/chunk_count（磁盘块数）——trailer
    // 元数据里的这两个字段 server 侧没有解析回填（py_name/trailer_len/comp_type
    // 已填）。total_uncompressed 留 0（消费端 Unpickler 不依赖预告值）；
    // chunk_count 同理。block_area 边界由 trailer_len 提供。
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

void NetworkChunkSource::recv_loop() {
    // 坏片语义（§8.1 + TCP 保序）：
    //   - server 顺序发送 → 正常路径 seq 严格递增无洞；
    //   - 坏片发生 → resend（每 seq 一次）+ 后续好片 pending_ 暂存（按 seq
    //     重排——字节流顺序不能乱）；TCP 流控自动压住发送方（接收线程照常
    //     读帧，pending 内存受 TCP 窗口 + 保险上限约束）；
    //   - 重传帧到达 → 填洞 → drain pending 按序 push + 增量根推进。
    bool digest_seen = false;

    auto drain_pending = [&]() {
        auto it = pending_.find(next_seq_);
        while (it != pending_.end()) {
            root_.update(it->second->data(), it->second->size());
            received_ += it->second->size();
            push_block(it->second->data(), it->second->size());
            pending_bytes_ -= it->second->size();
            pending_.erase(it);
            next_seq_++;
            it = pending_.find(next_seq_);
        }
    };

    while (true) {
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
            if (bad_seqs_.empty()) break;   // 干净完成
            continue;                        // 有洞：等重传帧
        }

        if (r == 3) {  // 坏片
            ERR("[NCS-FATAL-DATA-CORRUPTION] bad chunk frame CRC: seq={} — resending once",
                frame_seq_);
            if (resent_seqs_.count(frame_seq_)) {
                // 重传过一次仍坏 → 零容忍：流失败（§5）
                finish_stream(false, "resent chunk still corrupt");
                return;
            }
            bad_seqs_.insert(frame_seq_);
            resent_seqs_.insert(frame_seq_);
            ChunkResendMessage rs;
            rs.seq_ = frame_seq_;
            CMString encoded = MessageProtocol::encode(rs);
            if (!transport_->send_all(fd_, encoded.data(), encoded.size())) {
                finish_stream(false, "resend request failed");
                return;
            }
            continue;
        }

        // r == 1：好片。重传补洞帧必须是待补 seq；正常帧按序。
        if (digest_seen) {
            if (!bad_seqs_.count(frame_seq_)) {
                finish_stream(false, "unexpected frame after digest");
                return;
            }
            bad_seqs_.erase(frame_seq_);
        }
        if (frame_seq_ < next_seq_) {
            continue;  // 迟到的重复帧：丢弃（幂等）
        }
        // 暂存 + 按序 drain。堆积上界天然有保障：接收线程与 drain 同线程
        // 顺序执行——push_block 在队列满时阻塞（TCP 流控压住发送方），
        // pending 最多"当前片 + 队列上限"。
        pending_[frame_seq_] = frame_raw_;
        pending_bytes_ += frame_raw_->size();
        drain_pending();
        if (digest_seen && bad_seqs_.empty()) break;  // 补清
    }

    // 流尾复核（计数 / 根失配）。
    if (received_ != total_len_) {
        finish_stream(false, "byte count mismatch");
        return;
    }
    if (root_.final() != root_expected_) {
        ERR("[NCS-FATAL-DATA-CORRUPTION] digest mismatch: expected={:016x} actual={:016x}",
            root_expected_, root_.final());
        finish_stream(false, "digest mismatch");
        return;
    }
    finish_stream(true, nullptr);
}

int NetworkChunkSource::read_one_frame() {
    // 返回：1=好片；2=DIGEST；3=坏片；0=断连/失步；-1=帧头 check 失败。
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
        char sf[12];
        if (!recv_exact(transport_.get(), fd_, sf, 12)) return 0;
        uint32_t fseq = 0;
        uint64_t fcrc = 0;
        ChunkFrameProtocol::parse_small_fields(sf, small_len, fseq, fcrc);
        uint64_t raw_len = ChunkFrameProtocol::raw_len_from_total(tl);
        uint64_t chunk_count = (total_len_ + frame_bytes_ - 1) / frame_bytes_;
        if (fseq >= chunk_count || raw_len == 0 || raw_len > frame_bytes_ ||
            static_cast<uint64_t>(fseq) * frame_bytes_ + raw_len > total_len_) {
            return 0;
        }
        frame_raw_ = CMMakeShared<FlyBuffer>();
        frame_raw_->resize(raw_len);
        if (!recv_exact(transport_.get(), fd_, frame_raw_->data(), raw_len)) return 0;
        frame_seq_ = fseq;
        if (data_checksum(frame_raw_->data(), frame_raw_->size()) != fcrc) return 3;
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
