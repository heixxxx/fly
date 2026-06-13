#include <storage/cpp/temp_store.h>
#include <core/cpp/config.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>

namespace fly {

namespace fs = std::filesystem;

static constexpr int64_t DEFAULT_TEMP_MAX_BYTES = 512LL * 1024 * 1024;

TempStore::TempStore(int64_t max_bytes) : max_bytes_(max_bytes) {
    if (max_bytes_ <= 0) {
        auto cfg = Config::instance();
        max_bytes_ = cfg->get_int("temp_store_size");
        if (max_bytes_ <= 0) max_bytes_ = DEFAULT_TEMP_MAX_BYTES;
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist;
    std::ostringstream oss;
    oss << "/tmp/fly_temp_" << std::hex << dist(gen);
    tmp_dir_ = oss.str();
    fs::create_directories(tmp_dir_);
}

TempStore::~TempStore() {
    cleanup_all();
}

void TempStore::put(const CMString& object_name, const CMString& compressed_data) {
    int64_t size = static_cast<int64_t>(compressed_data.size());

    auto old = mem_.find(object_name);
    if (old.has_value()) mem_bytes_.fetch_sub(old->size_);
    mem_.erase(object_name);

    auto old_path = disk_files_.find(object_name);
    if (old_path.has_value()) {
        std::error_code ec;
        fs::remove(*old_path, ec);
    }
    disk_files_.erase(object_name);

    if (mem_bytes_.load() + size <= max_bytes_) {
        MemEntry e;
        e.compressed_data_ = compressed_data;
        e.size_ = size;
        mem_.insert(object_name, std::move(e));
        mem_bytes_.fetch_add(size);
        return;
    }

    std::hash<CMString> hasher;
    std::ostringstream oss;
    oss << tmp_dir_ << "/" << std::hex << std::setfill('0')
        << std::setw(8) << (hasher(object_name) & 0xFFFFFFFF) << ".tmp";
    CMString file_path = oss.str();

    std::ofstream ofs(file_path, std::ios::binary);
    if (ofs) {
        ofs.write(compressed_data.data(), static_cast<std::streamsize>(size));
        ofs.close();
    }
    disk_files_.insert(object_name, file_path);
}

std::pair<bool, CMString> TempStore::get(const CMString& object_name) {
    auto entry = mem_.find(object_name);
    if (entry.has_value()) {
        return {true, entry->compressed_data_};
    }

    auto path = disk_files_.find(object_name);
    if (path.has_value() && fs::exists(*path)) {
        std::ifstream ifs(*path, std::ios::binary);
        if (ifs) {
            CMString data((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());
            return {true, std::move(data)};
        }
    }
    return {false, {}};
}

bool TempStore::has(const CMString& object_name) {
    if (mem_.contains(object_name)) return true;
    auto path = disk_files_.find(object_name);
    return path.has_value() && fs::exists(*path);
}

void TempStore::remove(const CMString& object_name) {
    auto entry = mem_.find(object_name);
    if (entry.has_value()) mem_bytes_.fetch_sub(entry->size_);
    mem_.erase(object_name);

    auto path = disk_files_.find(object_name);
    if (path.has_value()) {
        std::error_code ec;
        fs::remove(*path, ec);
    }
    disk_files_.erase(object_name);
}

void TempStore::cleanup_all() {
    mem_.clear();
    mem_bytes_.store(0);
    disk_files_.iterate([](const CMString&, const CMString& path) {
        std::error_code ec;
        fs::remove(path, ec);
    });
    disk_files_.clear();
    std::error_code ec;
    fs::remove_all(tmp_dir_, ec);
}

int64_t TempStore::mem_bytes() const {
    return mem_bytes_.load();
}

int64_t TempStore::max_bytes() const {
    return max_bytes_;
}

}  // namespace fly
