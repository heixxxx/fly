#include <storage/cpp/database.h>
#include <storage/cpp/compressor.h>
#include <core/cpp/config.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

Database::Database(const CMString& base_path, const CMString& data_path, uint64_t writer_id)
    : base_path_(base_path)
    , data_path_(data_path)
    , writer_id_(writer_id)
    , db_id_(generate_db_id()) {

    ensure_directory_exists(base_path_);
    if (!data_path_.empty()) {
        ensure_directory_exists(data_path_);
    }

    Config& config = Config::instance();
    CMString comp_type_str = config.get_str("compression_type");
    CompressionType comp_type = CompressorFactory::type_from_name(comp_type_str);
    int64_t comp_threshold = config.get_int("compression_threshold");
    int64_t stream_chunk_size = config.get_int("compression_stream_chunk_size");

    writer_ = std::make_unique<DataWriter>(
        base_path_, data_path_, writer_id_,
        config.get_int("aggregation_threshold"),
        config.get_int("large_file_threshold"),
        config.get_int("block_size"),
        comp_type,
        comp_threshold,
        static_cast<int>(config.get_int("compression_level")),
        stream_chunk_size
    );
    reader_ = std::make_unique<DataReader>(base_path_, data_path_, writer_id_);
}

Database::~Database() = default;

CMString Database::write_object(const CMString& object_name, const CMString& data, bool backup) {
    check_frozen();
    CMString result = writer_->write_object(object_name, data, backup);
    writer_->flush();
    fly::WorkerAgentContext::record_write(db_id_, object_name);
    return result;
}

CMString Database::read_object(const CMString& object_name) {
    return find_and_read(object_name);
}

CMString Database::write_object_typed(const CMString& object_name, const CMString& data,
                                        const CMString& py_name) {
    check_frozen();
    CMString result = writer_->write_typed_object(object_name, static_cast<uint64_t>(data.size()),
                                        py_name, data.data(),
                                        static_cast<int64_t>(data.size()));
    writer_->flush();
    fly::WorkerAgentContext::record_write(db_id_, object_name);
    return result;
}

ReadResult Database::read_object_typed(const CMString& object_name) {
    return find_and_read_typed(object_name);
}

void Database::freeze() {
    if (is_frozen_) {
        return;
    }
    is_frozen_ = true;
    writer_->close();
    create_frozen_marker();
}

bool Database::is_frozen() const {
    return is_frozen_;
}

DbMeta Database::load_meta() const {
    CMString meta_path = base_path_ + "/_DB_META";
    std::ifstream ifs(meta_path, std::ios::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open meta file: " + meta_path);
    }

    CMString content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    DbMeta meta{};
    FLY_DECODE(content, DbMeta, meta);
    return meta;
}

CMString Database::get_db_id() const {
    return db_id_;
}

CMString Database::get_base_path() const {
    return base_path_;
}

CMString Database::get_data_path() const {
    return data_path_;
}

CMString Database::get_obj_name(const CMString& name) const {
    return db_id_ + ":" + name;
}

void Database::reset() {
    is_frozen_ = false;
    CMString frozen_marker = base_path_ + "/_FROZEN";
    if (fs::exists(frozen_marker)) {
        fs::remove(frozen_marker);
    }
}

void Database::check_frozen() {
    if (is_frozen_) {
        throw std::runtime_error("Database is frozen: " + base_path_);
    }
}

void Database::create_frozen_marker() {
    CMString frozen_path = base_path_ + "/_FROZEN";
    std::ofstream ofs(frozen_path);
    ofs.close();

    auto now = std::chrono::system_clock::now();
    int64_t frozen_at = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    DbMeta meta{db_id_, base_path_, frozen_at, frozen_at, {}};
    CMString encoded;
    FLY_ENCODE(meta, encoded);

    CMString meta_path = base_path_ + "/_DB_META";
    std::ofstream meta_ofs(meta_path, std::ios::binary);
    meta_ofs.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    meta_ofs.close();
}

CMString Database::generate_db_id() {
    std::size_t h = std::hash<CMString>{}(base_path_);
    std::stringstream ss;
    ss << std::hex << h;
    CMString hash_str = ss.str();
    if (hash_str.size() > 12) {
        hash_str = hash_str.substr(0, 12);
    }
    return hash_str;
}

void Database::ensure_directory_exists(const CMString& path) {
    fs::create_directories(path);
}

ReadResult Database::find_and_read_typed(const CMString& object_name) {
    CMString read_dir = data_path_.empty() ? base_path_ : data_path_;

    writer_->flush();

    for (const auto& entry : fs::directory_iterator(read_dir)) {
        if (!entry.is_regular_file()) continue;
        CMString fname = entry.path().filename().string();
        if (fname.size() >= 12 && fname.substr(0, 7) == "worker_" && fname.substr(fname.size() - 4) == ".idx") {
            try {
                std::string id_str = fname.substr(7, fname.size() - 11);
                uint64_t wid = std::stoull(id_str);
                DataReader temp_reader(base_path_, data_path_, wid);
                if (temp_reader.exists(object_name)) {
                    return temp_reader.read_object_data(object_name);
                }
            } catch (...) {}
        }
    }

    throw std::runtime_error("Object not found: " + object_name);
}

CMString Database::find_and_read(const CMString& object_name) {
    ReadResult result = find_and_read_typed(object_name);
    return CMString(result.data_buffer.begin(), result.data_buffer.end());
}