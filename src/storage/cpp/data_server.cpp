#include <storage/cpp/data_server.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <common/cpp/data_checksum.h>
#include <core/cpp/config.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/epoll_multiplexer.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <log/cpp/logger.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#ifdef DBG
#undef DBG
#endif
#define DBG(...) ((void)0)

namespace fly {

DataServer::DataServer(DataService& ds, CMSharedPtr<Transport> transport,
                       CMSharedPtr<EpollMultiplexer> epoll, int thread_count)
    : data_service_(ds)
    , transport_(std::move(transport))
    , epoll_(std::move(epoll))
    , thread_count_(thread_count > 1 ? thread_count : 2) {}

DataServer::DataServer(DataService& ds, int thread_count)
    : DataServer(ds, create_tcp_transport(), create_epoll_multiplexer(), thread_count) {}

DataServer::~DataServer() {
    stop();
}

void DataServer::start(const CMString& host, int port) {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (running_) return;

    listen_fd_ = transport_->create_listen_socket(host, port);
    if (listen_fd_ < 0) {
        ERR("DataServer: create_listen_socket() failed");
        return;
    }

    data_port_ = transport_->get_port(listen_fd_);

    epoll_fd_ = epoll_->create();
    if (epoll_fd_ < 0) {
        ERR("DataServer: epoll create() failed");
        transport_->close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (!epoll_->add(epoll_fd_, listen_fd_, EV_READ)) {
        ERR("DataServer: epoll add(listen_fd) failed");
        epoll_->destroy(epoll_fd_);
        transport_->close(listen_fd_);
        epoll_fd_ = -1;
        listen_fd_ = -1;
        return;
    }

    running_ = true;

    int n_epoll = std::max(1, thread_count_ / 2);
    int n_send = std::max(1, thread_count_ - n_epoll);

    INFO("DataServer starting: port={} epoll_threads={} send_threads={}",
         data_port_, n_epoll, n_send);

    epoll_threads_.reserve(n_epoll);
    for (int i = 0; i < n_epoll; ++i) {
        epoll_threads_.emplace_back(&DataServer::epoll_loop, this);
    }

    send_threads_.reserve(n_send);
    for (int i = 0; i < n_send; ++i) {
        send_threads_.emplace_back(&DataServer::send_loop, this);
    }
}

void DataServer::stop() {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (!running_.exchange(false)) return;

    // notify 必须在 send_mutex_ 保护下：send_loop 的 wait(check pred) + wait() 是
    // "持锁查 pred → 释放锁 wait" 的序列。若 notify 不持锁，可能在 send_loop 持锁
    // 查 pred（pred=false，即将 wait）的窗口里发出，此时无 waiter → notify 落空 →
    // send_loop 永久 wait → stop 的 join 永久 hang（lost wakeup）。持锁 notify 确保：
    // 要么 send_loop 已 wait（释放锁，notify 唤醒它），要么 send_loop 在查 pred 时
    // 看到 running_=false（pred=true，直接退出不 wait）。
    {
        std::lock_guard<std::mutex> slk(send_mutex_);
        send_cv_.notify_all();
    }

    if (listen_fd_ >= 0) {
        transport_->close(listen_fd_);
        listen_fd_ = -1;
    }

    if (epoll_fd_ >= 0) {
        epoll_->destroy(epoll_fd_);
        epoll_fd_ = -1;
    }

    for (auto& t : epoll_threads_) {
        if (t.joinable()) t.join();
    }
    epoll_threads_.clear();

    for (auto& t : send_threads_) {
        if (t.joinable()) t.join();
    }
    send_threads_.clear();

    std::lock_guard<std::mutex> clkk(conn_mutex_);
    for (auto& c : conns_) {
        if (c.fd >= 0) {
            transport_->close(c.fd);
            c.fd = -1;
        }
    }
    conns_.clear();
}

int DataServer::find_conn_index(int fd) {
    for (int i = 0; i < static_cast<int>(conns_.size()); ++i) {
        if (conns_[i].fd == fd) return i;
    }
    return -1;
}

void DataServer::cleanup_fd(int fd) {
    epoll_->del(epoll_fd_, fd);
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx >= 0) {
            conns_[idx] = std::move(conns_.back());
            conns_.pop_back();
        }
    }
    transport_->close(fd);
}

void DataServer::epoll_loop() {
    while (running_) {
        IoEvent events[64];
        int n = epoll_->wait(epoll_fd_, events, 64, 100);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (!running_) return;
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].fd;

            if (fd == listen_fd_) {
                while (true) {
                    int cfd = transport_->accept_connection(listen_fd_);
                    if (cfd < 0) break;

                    transport_->set_nonblocking(cfd);

                    {
                        std::lock_guard<std::mutex> lk(conn_mutex_);
                        conns_.push_back({cfd, {}});
                    }

                    epoll_->add(epoll_fd_, cfd, EV_READ | EV_ONESHOT);

                    DBG("[DS-ACCEPT] fd={} total={}", cfd, conns_.size());
                }
            } else {
                on_readable(fd);
            }
        }
    }
}

void DataServer::on_readable(int fd) {
    char tmp[65536];
    CMString new_data;
    bool got_eof = false;

    while (true) {
        ssize_t n = transport_->recv(fd, tmp, sizeof(tmp));
        if (n > 0) {
            new_data.append(tmp, n);
            if (n < static_cast<ssize_t>(sizeof(tmp))) break;
        } else if (n == 0) {
            got_eof = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            got_eof = true;
            break;
        }
    }

    if (got_eof && new_data.empty()) {
        cleanup_fd(fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx < 0) return;
        conns_[idx].recv_buf.append(new_data);
    }

    CMString buf;
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx < 0) return;
        buf = std::move(conns_[idx].recv_buf);
        conns_[idx].recv_buf.clear();
    }

    bool pushed_response = false;

    while (buf.size() >= 9) {
        uint64_t total_len = 0;
        if (!parse_frame_header(buf.data(), total_len)) {
            // check 位失配 = 流失步/垃圾头：连接已不可信，断开（数据面请求
            // 都是 client 主动发起的小帧，残留缓冲不值得抢救）。
            ERR("[DS-FRAME] fd={} frame header check failed, dropping connection", fd);
            cleanup_fd(fd);
            return;
        }

        uint64_t frame_size = 8 + total_len;
        if (buf.size() < frame_size) break;

        CMString frame(buf.data(), frame_size);
        buf.erase(0, frame_size);

        // Dispatch by message type. The data plane now carries both read
        // requests and bandwidth probes; the type byte routes each frame.
        MessageType mtype = MessageProtocol::get_type(frame);

        if (mtype == MessageType::NET_PROBE_REQUEST) {
            NetProbeRequestMessage preq;
            if (!MessageProtocol::decode(frame, preq)) {
                ERR("[DS-DECODE] fd={} probe decode failed", fd);
                break;
            }
            NetProbeResponseMessage presp;
            presp.probe_seq_ = preq.probe_seq_;
            presp.payload_.assign(preq.payload_size_, 0);
            CMString encoded = MessageProtocol::encode(presp);
            {
                std::lock_guard<std::mutex> slk(send_mutex_);
                SendTask task;
                task.fd = fd;
                task.data = std::move(encoded);
                send_queue_.push(std::move(task));
            }
            send_cv_.notify_one();
            pushed_response = true;
            continue;
        }

        // L2 在线块重传路由（§4.5）：client 验坏某分片后请求单块重发。
        // 每连接每 seq 上限一次（conn 状态计数）；再坏由 client 升格对象级
        // CHECKSUM（零容忍 §5）。
        if (mtype == MessageType::CHUNK_RESEND) {
            ChunkResendMessage rs;
            if (!MessageProtocol::decode(frame, rs)) {
                ERR("[DS-DECODE] fd={} chunk-resend decode failed", fd);
                break;
            }
            handle_chunk_resend(fd, rs.offset_, rs.length_);
            continue;
        }

        DataRequestMessage req;
        if (!MessageProtocol::decode(frame, req)) {
            ERR("[DS-DECODE] fd={} decode failed", fd);
            break;
        }

        // ── L2 分片分流（§4.5/§7.1 #20）──
        // 本地完整落盘对象且超过阈值 → 分片路径（pread 循环，server 不整读）。
        // 缓存命中/temp/找不到定位 → 快路径（现有整帧两段式）。
        {
            int64_t threshold = Config::instance()->get_int("chunked_transfer_threshold");
            auto [loc_ok, loc] = data_service_.find_chunked_location(req.object_name_);
            if (loc_ok && loc.size > static_cast<uint64_t>(threshold)) {
                serve_chunked(fd, req.object_name_, loc.file_path, loc.offset, loc.size);
                pushed_response = true;
                continue;
            }
        }

        DataResponseMessage response;
        response.object_name_ = req.object_name_;

        // wait_local_write=false：DataServer IO 线程池（默认 4 线程）不能阻塞 wait，
        // 否则并发请求易耗尽 serve 能力。INCOMPLETE 时返回 false，上层据此返回
        // DATA_NOT_READY 让远程 reader 轮询重试。
        auto [found, raw_data] = data_service_.try_read_local_raw(req.object_name_, /*wait_local_write=*/false);
        bool serve_raw = found;  // 校验失败时降为 false（不服务坏数据）

        if (found) {
            // 尾部 trailer 解析（§4.4）拿 py_name。本地 record 校验失败
            //（trailer 坏/CRC 域坏——构造即解析）：不服务坏数据，按副本
            // 不可用回 ERROR（client TIER2 换副本；本地对象坏不等于连接坏）。
            DecompressingStreamBuf dsbuf(raw_data->data(), raw_data->size());
            if (dsbuf.checksum_failed()) {
                ERR("[DS-FATAL-DATA-CORRUPTION] local record corrupt, refusing to serve: obj={}",
                    req.object_name_);
                response.success_ = false;
                response.status_ = ResponseStatus::ERROR;
                response.error_message_ = "local record corrupt: " + req.object_name_;
                serve_raw = false;
            } else {
                response.success_ = true;
                response.py_name_ = dsbuf.py_name();
                // temp 标记随响应（缓存双池路由——远端读取方查不到本地属性）。
                response.is_temp_ = data_service_.is_temp_object(req.object_name_);

                auto write_hash = data_service_.get_write_context_hash(req.object_name_);
                if (!write_hash.empty()) {
                    response.write_context_hash_ = write_hash;
                }
                // wire 根摘要（§4.5）：server 侧锚点，client 收满 raw 后校验。
                response.payload_crc_ =
                    data_checksum(raw_data->data(), raw_data->size());
            }
        } else {
            response.success_ = false;
            response.status_ = data_service_.is_write_in_progress(req.object_name_)
                               ? ResponseStatus::NOT_READY
                               : ResponseStatus::NOT_FOUND;
        }

        // Two-segment encode: small fields via bitsery, raw payload referenced
        // by pointer (zero-copy — raw_data FlyBufferPtr shared ownership).
        auto seg = DataResponseProtocol::encode(response, serve_raw ? raw_data : nullptr);

        {
            std::lock_guard<std::mutex> slk(send_mutex_);
            SendTask task;
            task.fd = fd;
            task.data = std::move(seg.header_segment);
            task.raw_data = serve_raw ? raw_data : nullptr;  // keep alive for send thread
            send_queue_.push(std::move(task));
            DBG("[DS-Q] fd={} pushed queue_size={}", fd, send_queue_.size());
        }
        send_cv_.notify_one();
        pushed_response = true;
    }

    if (!buf.empty()) {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx >= 0) {
            conns_[idx].recv_buf = std::move(buf);
        }
    }

    if (pushed_response) {
        // send_thread will rearm after send completes
    } else if (got_eof) {
        cleanup_fd(fd);
    } else {
        epoll_->mod(epoll_fd_, fd, EV_READ | EV_ONESHOT);
    }
}

void DataServer::send_loop() {
    while (running_) {
        SendTask task;
        {
            std::unique_lock<std::mutex> lk(send_mutex_);
            send_cv_.wait(lk, [this] { return !running_ || !send_queue_.empty(); });
            if (!running_ && send_queue_.empty()) return;
            if (!send_queue_.empty()) {
                task = std::move(send_queue_.front());
                send_queue_.pop();
            }
        }

        // L2 自含分片任务：闭包内完成 META → CHUNK 流 → DIGEST/rearm。
        if (task.chunked_execute) {
            task.chunked_execute();
            continue;
        }

        if (task.fd >= 0 && !task.data.empty()) {
            bool has_raw = task.raw_data && !task.raw_data->empty();
            if (has_raw) {
                // Single writev call for header + raw payload (avoids 2x send overhead).
                struct iovec iov[2];
                iov[0].iov_base = const_cast<char*>(task.data.data());
                iov[0].iov_len = task.data.size();
                iov[1].iov_base = const_cast<char*>(task.raw_data->data());
                iov[1].iov_len = task.raw_data->size();
                bool ok = transport_->sendv(task.fd, iov, 2);
                if (!ok) {
                    ERR("[DS-SEND] sendv failed: fd={}", task.fd);
                    cleanup_fd(task.fd);
                } else {
                    DBG("[DS-SEND] fd={} complete: {} + {} bytes (sendv)", task.fd, task.data.size(), task.raw_data->size());
                    epoll_->mod(epoll_fd_, task.fd, EV_READ | EV_ONESHOT);
                }
            } else {
                do_send(task.fd, task.data);
            }
        }
    }
}

// ── L2 分片路径（chunked-transfer-design.md §4.5）──

// 单片字节数（纯字节切片，与磁盘块结构无关——client 顺序重组后与磁盘
// record 字节一致，DecompressingStreamBuf 直接消费）。
static constexpr uint64_t kChunkFrameBytes = 4ULL << 20;

void DataServer::serve_chunked(int fd, const CMString& object_name,
                               const CMString& file_path, uint64_t offset, uint64_t size) {
    // conn 状态登记（CHUNK_RESEND 路由需要对象区间 + 重传计数）。
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx < 0) return;
        conns_[idx].chunk_file = file_path;
        conns_[idx].chunk_off = offset;
        conns_[idx].chunk_size = size;
        conns_[idx].resent_offsets.clear();
    }

    // META 元数据（尾部 trailer 预解析结果，L3 §8.1）。
    CMString meta_py_name;
    uint64_t meta_trailer_len = 0;
    int meta_comp_type = -1;
    // temp 标记（缓存双池路由）：本地索引判定随 META 告知远端读取方。
    bool meta_is_temp = data_service_.is_temp_object(object_name);

    // L3（§8.1）：发送前 pread 尾部解析 trailer → META 携带 py_name/trailer_len
    //（流式消费端无法预先读流尾）。尾部预读 min(size, 4KB)；解析失败（极端长
    // py_name 或损坏）→ py_name 空 + trailer_len 0——L2 重组路径不受影响
    //（client 重组后自行尾部解析），L3 流式消费端会因 block_area 边界缺失
    // 而回退整缓冲路径（保守正确）。
    {
        uint64_t tail_n = std::min<uint64_t>(size, 4096);
        int file = ::open(file_path.c_str(), O_RDONLY);
        if (file >= 0) {
            CMVector<char> tail(static_cast<size_t>(tail_n));
            ssize_t got = ::pread(file, tail.data(), static_cast<size_t>(tail_n),
                                  static_cast<off_t>(offset + size - tail_n));
            ::close(file);
            if (got > 0 && static_cast<uint64_t>(got) == tail_n) {
                ObjectHeader hdr;
                size_t tl = 0;
                if (ObjectHeader::deserialize_trailer({tail.data(), tail.size()}, hdr, tl)) {
                    // 记住元数据供 META 使用（下面闭包外读取）。
                    meta_py_name = hdr.py_name_;
                    meta_trailer_len = tl;
                    meta_comp_type = static_cast<int>(hdr.compression_type_);
                }
            }
        }
    }

    SendTask task;
    task.fd = fd;
    task.chunked_execute = [this, fd, object_name, file_path, offset, size,
                            meta_py_name, meta_trailer_len, meta_comp_type,
                            meta_is_temp]() {
        bool ok = true;

        // META 先行（复用 DATA_RESPONSE 两段式，无 raw）。
        DataResponseMessage meta;
        meta.object_name_ = object_name;
        meta.success_ = true;
        meta.chunked_ = true;
        meta.total_compressed_len_ = size;
        meta.chunk_frame_bytes_ = kChunkFrameBytes;
        meta.py_name_ = meta_py_name;
        meta.trailer_len_ = meta_trailer_len;
        meta.is_temp_ = meta_is_temp;
        if (meta_comp_type >= 0) {
            meta.chunk_compression_type_ = static_cast<uint8_t>(meta_comp_type);
        }
        CMString meta_frame = DataResponseProtocol::encode(meta, nullptr).header_segment;
        ok = transport_->send_all(fd, meta_frame.data(), meta_frame.size());
        if (!ok) {
            ERR("[DS-CHUNK] fd={} META send failed", fd);
            cleanup_fd(fd);
            return;
        }

        // CHUNK 循环：pread 切片 → 组帧 → writev（帧头+数据一次系统调用）。
        // server 内存恒为单片缓冲（4MB）——大对象不整读（§10 内存量化）。
        int file = ::open(file_path.c_str(), O_RDONLY);
        if (file < 0) {
            ERR("[DS-CHUNK] fd={} open failed: {}", fd, file_path);
            cleanup_fd(fd);
            return;
        }
        CMVector<char> buf(static_cast<size_t>(kChunkFrameBytes));
        uint64_t off = offset;
        uint32_t frames = 0;
        while (ok && off < offset + size) {
            uint64_t n = std::min<uint64_t>(kChunkFrameBytes, offset + size - off);
            ssize_t got = ::pread(file, buf.data(), static_cast<size_t>(n),
                                  static_cast<off_t>(off));
            if (got < 0 || static_cast<uint64_t>(got) != n) {
                ERR("[DS-CHUNK] fd={} pread failed at off={} n={}", fd, off, n);
                ok = false;
                break;
            }
            // §4.4 帧片 CRC 取消计算（2026-08-30 用户裁定，性能报告 §4）：
            // 发 0 = 未计算，接收端跳过帧级验证——完整性由块级 CRC（解压/
            // 块解析器）+ DIGEST 根摘要承担。传输路径 CRC 由 3 遍降 1 遍。
            // offset = 帧首字节在 record 内偏移（A'：正常流/重传统一定位语义）。
            CMString hdr = ChunkFrameProtocol::encode_header(off - offset, 0, n);
            struct iovec iov[2];
            iov[0].iov_base = const_cast<char*>(hdr.data());
            iov[0].iov_len = hdr.size();
            iov[1].iov_base = buf.data();
            iov[1].iov_len = static_cast<size_t>(n);
            ok = transport_->sendv(fd, iov, 2);
            if (!ok) break;
            off += n;
            frames++;
        }
        ::close(file);
        if (!ok) {
            ERR("[DS-CHUNK] fd={} chunk stream send failed at frame={}", fd, frames);
            cleanup_fd(fd);
            return;
        }

        // DIGEST 尾帧（T5 2026-08-31 根摘要双侧消除：root_crc_ 发 0 = 未
        // 计算——L0 块级 CRC + trailer 已承担完整性，整 record 单遍根摘要是
        // 冗余遍历；client root_crc≠0 才验，兼容旧 serve。帧本身保留：client
        // 以 DIGEST 帧为流结束标记 + chunk_count 对账）。
        DataDigestMessage digest;
        digest.root_crc_ = 0;
        digest.chunk_count_ = frames;
        CMString digest_frame = MessageProtocol::encode(digest);
        if (!transport_->send_all(fd, digest_frame.data(), digest_frame.size())) {
            ERR("[DS-CHUNK] fd={} DIGEST send failed", fd);
            cleanup_fd(fd);
            return;
        }
        DBG("[DS-CHUNK] fd={} obj={} sent {} frames, {} bytes", fd, object_name, frames, size);
        // rearm 读端：resend 请求在发送期间到达则暂存内核缓冲，保序安全（§7.4）。
        epoll_->mod(epoll_fd_, fd, EV_READ | EV_ONESHOT);
    };

    {
        std::lock_guard<std::mutex> slk(send_mutex_);
        send_queue_.push(std::move(task));
    }
    send_cv_.notify_one();
}

void DataServer::handle_chunk_resend(int fd, uint64_t offset, uint64_t length) {
    CMString file;
    uint64_t base_off = 0, total = 0;
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx < 0) return;
        auto& c = conns_[idx];
        if (c.chunk_file.empty()) {
            ERR("[DS-CHUNK] fd={} resend on non-chunked conn", fd);
            return;
        }
        // 每区间上限一次（§14.1 A'3）：client 侧块级解析驱动，同一区间
        // 重复请求 = 协议异常，断开防御。
        uint64_t key = offset;
        if (c.resent_offsets.count(key)) {
            ERR("[DS-CHUNK] fd={} resend offset={} limit reached, dropping connection",
                fd, offset);
            cleanup_fd(fd);
            return;
        }
        c.resent_offsets.insert(key);
        file = c.chunk_file;
        base_off = c.chunk_off;
        total = c.chunk_size;
    }

    // byte-offset 寻址（A'3）：offset 相对 record 起点，server 零块知识。
    uint64_t start = base_off + offset;
    if (offset + length > total || length == 0) {
        ERR("[DS-CHUNK] fd={} resend range [{}, {}) out of record (size={})",
            fd, offset, offset + length, total);
        return;
    }

    SendTask task;
    task.fd = fd;
    task.chunked_execute = [this, fd, file, start, n = length, offset]() {
        int f = ::open(file.c_str(), O_RDONLY);
        if (f < 0) {
            ERR("[DS-CHUNK] fd={} resend open failed: {}", fd, file);
            cleanup_fd(fd);
            return;
        }
        CMVector<char> buf(static_cast<size_t>(n));
        ssize_t got = ::pread(f, buf.data(), static_cast<size_t>(n),
                              static_cast<off_t>(start));
        ::close(f);
        if (got < 0 || static_cast<uint64_t>(got) != n) {
            ERR("[DS-CHUNK] fd={} resend pread failed", fd);
            cleanup_fd(fd);
            return;
        }
        // 重传帧 = 纯字节区间（不带块语义——client 按字节替换 hole）。
        // 帧片 CRC 同主发送循环：发 0（未计算），§4.4 取消计算裁定。
        CMString hdr = ChunkFrameProtocol::encode_header(offset, 0, n);
        struct iovec iov[2];
        iov[0].iov_base = const_cast<char*>(hdr.data());
        iov[0].iov_len = hdr.size();
        iov[1].iov_base = buf.data();
        iov[1].iov_len = static_cast<size_t>(n);
        if (!transport_->sendv(fd, iov, 2)) {
            ERR("[DS-CHUNK] fd={} resend send failed offset={}", fd, offset);
            cleanup_fd(fd);
            return;
        }
        DBG("[DS-CHUNK] fd={} resent offset={} ({} bytes)", fd, offset, n);
        epoll_->mod(epoll_fd_, fd, EV_READ | EV_ONESHOT);
    };
    {
        std::lock_guard<std::mutex> slk(send_mutex_);
        send_queue_.push(std::move(task));
    }
    send_cv_.notify_one();
}

void DataServer::do_send(int fd, const CMString& data) {
    bool ok = transport_->send_all(fd, data.data(), data.size());

    if (!ok) {
        ERR("[DS-SEND] fd={} failed: {}/{} bytes", fd, 0, data.size());
        cleanup_fd(fd);
    } else {
        DBG("[DS-SEND] fd={} complete: {} bytes", fd, data.size());
        epoll_->mod(epoll_fd_, fd, EV_READ | EV_ONESHOT);
    }
}

}  // namespace fly
