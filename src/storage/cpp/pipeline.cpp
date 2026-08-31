#include <storage/cpp/pipeline.h>

namespace fly {

WritePipeline make_file_write_pipeline(CompressionType comp, int level,
                                       int64_t chunk_size, EmitFn sink,
                                       int ratio_floor_pct) {
    std::vector<std::unique_ptr<WriteStage>> stages;
    if (comp != CompressionType::NONE) {
        stages.push_back(std::make_unique<CompressStage>(
            CompressorFactory::create(comp, level), ratio_floor_pct));
    }
    stages.push_back(std::make_unique<CrcStage>());
    stages.push_back(std::make_unique<BlockHeaderStage>(sink));
    return WritePipeline(std::move(stages), chunk_size, std::move(sink));
}

ReadPipeline make_block_read_pipeline(CompressionType comp, PullFn pull) {
    // CRC 校验压缩态字节（写入时刻锚点）：验证在先、解压在后。
    // comp == unc 的 raw 块由 DecompressStage 内部直通（无压缩器时同样直通）。
    std::vector<std::unique_ptr<ReadStage>> stages;
    stages.push_back(std::make_unique<CrcVerifyStage>());
    if (comp != CompressionType::NONE) {
        stages.push_back(std::make_unique<DecompressStage>(
            CompressorFactory::create(comp)));
    }
    return ReadPipeline(std::move(stages), std::move(pull));
}

}  // namespace fly
