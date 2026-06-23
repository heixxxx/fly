#include <storage/cpp/fly_stream.h>
#include <cstring>

FlyStream::FlyStream(CompressionType comp_type, int64_t chunk_size, const CMString& py_name)
    : is_write_mode_(true), py_name_(py_name) {
    write_buf_ = CMMakeShared<FlyBuffer>();
    fly_buf_sb_ = CMMakeUnique<FlyBufferStreamBuf>(*write_buf_);
    counting_sb_ = CMMakeUnique<CountingStreamBuf>(*fly_buf_sb_);
    counting_os_ = CMMakeUnique<std::ostream>(counting_sb_.get());
    ObjectHeader header;
    header.compression_type_ = static_cast<uint8_t>(comp_type);
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    CMString hdr = header.serialize();
    counting_os_->write(hdr.data(), static_cast<std::streamsize>(hdr.size()));
    auto comp = comp_type != CompressionType::NONE ? CompressorFactory::create(comp_type) : nullptr;
    compress_sb_ = CMMakeUnique<CompressingStreamBuf>(*counting_os_, std::move(comp), chunk_size);
    compress_os_ = CMMakeUnique<std::ostream>(compress_sb_.get());
}

FlyStream::FlyStream(FlyBufferPtr data)
    : is_write_mode_(false), read_buf_(std::move(data)) {
    decompress_sb_ = CMMakeUnique<DecompressingStreamBuf>(
        read_buf_ ? read_buf_->data() : nullptr, read_buf_ ? read_buf_->size() : 0);
    decompress_is_ = CMMakeUnique<std::istream>(decompress_sb_.get());
}

FlyStream::~FlyStream() = default;

void FlyStream::write(const char* data, size_t size) {
    compress_os_->write(data, static_cast<std::streamsize>(size));
}
void FlyStream::flush() { compress_os_->flush(); }

FlyBufferPtr FlyStream::finish_write() {
    compress_os_->flush();
    ObjectHeader header;
    header.compression_type_ = static_cast<uint8_t>(compress_sb_->compression_type());
    header.total_size_ = static_cast<uint64_t>(compress_sb_->total_uncompressed());
    header.chunk_count_ = static_cast<uint32_t>(compress_sb_->chunk_count());
    header.py_name_ = py_name_;
    header.py_name_len_ = static_cast<uint16_t>(py_name_.size());
    CMString real = header.serialize();
    std::memcpy(write_buf_->data(), real.data(), real.size());

    // Transfer ownership: release all streambufs and the buffer so that no
    // dangling references remain. WriteBackQueue's disk-write thread will
    // read from the returned FlyBufferPtr concurrently — if FlyStream still
    // held streambufs referencing write_buf_, their destructors could race
    // with the disk write, corrupting data or causing undefined behavior.
    return write_buf_;
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
int64_t FlyStream::total_uncompressed() const { return compress_sb_ ? compress_sb_->total_uncompressed() : 0; }
int32_t FlyStream::chunk_count() const { return compress_sb_ ? compress_sb_->chunk_count() : 0; }
