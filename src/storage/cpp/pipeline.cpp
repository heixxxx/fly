#include <storage/cpp/pipeline.h>

namespace fly {

WritePipeline make_file_write_pipeline(CMUniquePtr<Compressor> compressor,
                                       int64_t chunk_size, EmitFn sink,
                                       int ratio_floor_pct, int64_t raw_threshold) {
    std::vector<std::unique_ptr<WriteStage>> stages;
    if (compressor) {
        stages.push_back(std::make_unique<CompressStage>(std::move(compressor),
                                                         ratio_floor_pct,
                                                         raw_threshold));
    }
    stages.push_back(std::make_unique<CrcStage>());
    stages.push_back(std::make_unique<BlockHeaderStage>(sink));
    return WritePipeline(std::move(stages), chunk_size, std::move(sink));
}

ReadPipeline make_block_read_pipeline(CompressionType comp, PullFn pull) {
    // CRC 校验压缩态字节（写入时刻锚点）：验证在先、解压在后。
    // DecompressStage 恒在：comp == unc 的 raw 块直通（NONE 管线提供明文视图）。
    std::vector<std::unique_ptr<ReadStage>> stages;
    stages.push_back(std::make_unique<CrcVerifyStage>());
    stages.push_back(std::make_unique<DecompressStage>(
        comp == CompressionType::NONE
            ? nullptr
            : CompressorFactory::create(comp)));
    return ReadPipeline(std::move(stages), std::move(pull));
}

}  // namespace fly
