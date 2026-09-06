#pragma once

#include <storage/cpp/local_index.h>
#include <common/serialization/cpp/object_header.h>
#include <common/buffer/cpp/fly_buffer.h>
#include <container/cpp/container_aliases.h>
#include <log/cpp/logger.h>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>

struct ReadResult {
    FlySerBuf data_buffer_;
    CMString py_name_;
    bool can_still_produce_ = false;
};

class DataReader {
public:
    DataReader(
        const CMString& db_path,
        const CMString& data_path,
        const CMString& writer_id
    );

    ~DataReader();

    DataReader(const DataReader&) = delete;
    DataReader& operator=(const DataReader&) = delete;

    FlyBufferPtr read_raw_bytes(const CMString& object_name);
    FlyBufferPtr read_raw_bytes(const IndexEntry& entry);

    // 基于已知 entry + db 路径直接读取压缩字节，不构造完整 DataReader、不 load idx。
    // 用于热读路径 do_read_raw_entries：调用方（DataService）已从 local_idx_ 内存索引
    // 取得 entry，再 new DataReader 会触发 idx 文件全量解析（构建 entries_ map），
    // 而 read_raw_bytes(IndexEntry&) 只用 db_path_/data_path_ 定位文件，LocalIndex 的
    // entries_ 完全没被消费 —— 纯冗余 IO+解析开销。此方法等价但跳过 load。
    static FlyBufferPtr read_raw_from_entry(const IndexEntry& entry,
                                            const CMString& db_path,
                                            const CMString& data_path);

    bool exists(const CMString& object_name);

    CMString find_file_path(const CMString& file_name);
    // 静态文件定位（L2 分片服务位置查询用，DataService::find_chunked_location）。
    static CMString find_file_path(const CMString& file_name,
                                   const CMString& db_path,
                                   const CMString& data_path);
    std::optional<IndexEntry> find_entry(const CMString& object_name);
    std::optional<CMVector<IndexEntry>> find_all_entries(const CMString& object_name);

private:
    // 纯文件区间读取，不依赖实例状态。read_raw_bytes 与 read_raw_from_entry 共用。
    static FlyBufferPtr read_from_file(const CMString& file_path, int64_t offset, int64_t size);

    CMString db_path_;
    CMString data_path_;
    CMString writer_id_;

    CMUniquePtr<LocalIndex> index_;
};
