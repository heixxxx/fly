#include <storage/cpp/fly_stream.h>
#include <cstring>

FlyStream::FlyStream(CompressionType comp_type, int64_t chunk_size, const CMString& py_name,
                     int64_t compression_threshold)
    : is_write_mode_(true), py_name_(py_name) {
    // trailer 格式（§4.4）：不再前置占位 ObjectHeader——块流纯追加，
    // finish_write() 时追加 trailer（total/chunk_count 此时自然已知）。
    write_buf_ = CMMakeShared<FlyBuffer>();
    fly_buf_sb_ = CMMakeUnique<FlyBufferStreamBuf>(*write_buf_);
    counting_sb_ = CMMakeUnique<CountingStreamBuf>(*fly_buf_sb_);
    counting_os_ = CMMakeUnique<std::ostream>(counting_sb_.get());
    auto comp = comp_type != CompressionType::NONE ? CompressorFactory::create(comp_type) : nullptr;
    compress_sb_ = CMMakeUnique<CompressingStreamBuf>(*counting_os_, std::move(comp),
                                                      chunk_size, compression_threshold);
    compress_os_ = CMMakeUnique<std::ostream>(compress_sb_.get());
}

FlyStream::FlyStream(FlyBufferPtr data)
    : is_write_mode_(false), read_buf_(std::move(data)) {
    decompress_sb_ = CMMakeUnique<DecompressingStreamBuf>(
        read_buf_ ? read_buf_->data() : nullptr, read_buf_ ? read_buf_->size() : 0);
    decompress_is_ = CMMakeUnique<std::istream>(decompress_sb_.get());
}

FlyStream::FlyStream(CMSharedPtr<fly::ChunkSource> source, uint64_t block_area_len)
    : is_write_mode_(false), chunk_source_(std::move(source)) {
    // 流式读模式（L3）：源 + META 块流边界。源生命周期随 FlyStream
    //（NetworkChunkSource 析构归还 fd/slot）。
    decompress_sb_ = CMMakeUnique<DecompressingStreamBuf>(chunk_source_, block_area_len);
    decompress_is_ = CMMakeUnique<std::istream>(decompress_sb_.get());
}

FlyStream::FlyStream(CompressionType comp_type, int64_t chunk_size,
                     std::function<void(const char*, size_t)> chunk_sink,
                     const CMString& py_name, int64_t compression_threshold,
                     std::function<int64_t(int64_t, int32_t, bool, bool)> commit_fn)
    : is_write_mode_(true), py_name_(py_name), chunk_sink_(std::move(chunk_sink)),
      commit_fn_(std::move(commit_fn)) {
    // L1 sink 写模式（§9.1）：压缩流逐块回调——无内存整累积（write_buf_ 留空）。
    auto comp = comp_type != CompressionType::NONE ? CompressorFactory::create(comp_type) : nullptr;
    compress_sb_ = CMMakeUnique<CompressingStreamBuf>(std::move(comp), chunk_size,
                                                      chunk_sink_, compression_threshold);
    compress_os_ = CMMakeUnique<std::ostream>(compress_sb_.get());
}

FlyStream::~FlyStream() = default;

void FlyStream::write(const char* data, size_t size) {
    compress_os_->write(data, static_cast<std::streamsize>(size));
}
void FlyStream::flush() { compress_os_->flush(); }

FlyBufferPtr FlyStream::finish_write() {
    compress_os_->flush();
    ObjectHeader header;
    // Small payloads skip compression inside CompressingStreamBuf; record the
    // actual on-disk format so the read-side picks the matching path.
    header.compression_type_ = static_cast<uint8_t>(compress_sb_->effective_compression_type());
    header.total_size_ = static_cast<uint64_t>(compress_sb_->total_uncompressed());
    header.chunk_count_ = static_cast<uint32_t>(compress_sb_->chunk_count());
    header.py_name_ = py_name_;
    header.py_name_len_ = static_cast<uint16_t>(py_name_.size());
    header.block_comp_lens_ = compress_sb_->block_comp_lens();  // B' 块表
    // trailer 尾置追加（§4.4）：无 seek 回写，纯追加；trailer 兼作 commit marker。
    CMString trailer = header.serialize_trailer();
    write_buf_->write(trailer.data(), trailer.size());

    // Transfer ownership: release all streambufs and the buffer so that no
    // dangling references remain. WriteBackQueue's disk-write thread will
    // read from the returned FlyBufferPtr concurrently — if FlyStream still
    // held streambufs referencing write_buf_, their destructors could race
    // with the disk write, corrupting data or causing undefined behavior.
    return write_buf_;
}

void FlyStream::finish_sink() {
    compress_os_->flush();
    // trailer 构造（total/chunks 此时自然已知）并走 sink。
    ObjectHeader header;
    header.compression_type_ = static_cast<uint8_t>(compress_sb_->effective_compression_type());
    header.total_size_ = static_cast<uint64_t>(compress_sb_->total_uncompressed());
    header.chunk_count_ = static_cast<uint32_t>(compress_sb_->chunk_count());
    header.py_name_ = py_name_;
    header.py_name_len_ = static_cast<uint16_t>(py_name_.size());
    header.block_comp_lens_ = compress_sb_->block_comp_lens();  // B' 块表
    CMString trailer = header.serialize_trailer();
    chunk_sink_(trailer.data(), trailer.size());

    sink_total_ = compress_sb_->total_uncompressed();
    sink_chunks_ = compress_sb_->chunk_count();
    sink_comp_ = static_cast<uint8_t>(compress_sb_->effective_compression_type());
}

int64_t FlyStream::finish_and_commit(bool backup, bool populate_cache) {
    finish_sink();
    if (commit_fn_) {
        return commit_fn_(sink_total_, sink_chunks_, backup, populate_cache);
    }
    return 0;
}

CMString FlyStream::read(size_t n) {
    CMString out; out.resize(n);
    decompress_is_->read(out.data(), static_cast<std::streamsize>(n));
    out.resize(static_cast<size_t>(decompress_is_->gcount()));
    return out;
}
CMString FlyStream::read_all() {
    CMString out; char tmp[8192];
    while (true) {
        decompress_is_->read(tmp, sizeof(tmp));
        auto got = decompress_is_->gcount();
        if (got <= 0) break;
        out.append(tmp, static_cast<size_t>(got));
    }
    return out;
}
CMString FlyStream::readline() {
    CMString out; char ch;
    while (readinto(&ch, 1) == 1) { out.push_back(ch); if (ch == '\n') break; }
    return out;
}
size_t FlyStream::readinto(char* dst, size_t dst_size) {
    decompress_is_->read(dst, static_cast<std::streamsize>(dst_size));
    return static_cast<size_t>(decompress_is_->gcount());
}
int64_t FlyStream::total_uncompressed() const {
    return compress_sb_ ? compress_sb_->total_uncompressed()
                        : (decompress_sb_ ? static_cast<int64_t>(decompress_sb_->total_uncompressed()) : 0);
}
int32_t FlyStream::chunk_count() const {
    return compress_sb_ ? compress_sb_->chunk_count()
                        : (decompress_sb_ ? static_cast<int32_t>(decompress_sb_->chunk_count()) : 0);
}

bool FlyStream::checksum_failed() const {
    return decompress_sb_ && decompress_sb_->checksum_failed();
}
