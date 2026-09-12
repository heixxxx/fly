#include <emir/design/cpp/ds_partition.h>

#include <emir/design/cpp/ds_types.h>
#include <log/cpp/logger.h>
#include <message/cpp/message_macros.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fly {

namespace {

// —— 合并：单格撒入（裁定 D10 A 面积比例 + 最大余数法守恒）──────────

// 半开区间 floor 除（口径与 DSDensityGrid::accumulate_footprint 一致）
int64_t floor_div_i64(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// 局部格值 v 撒入全局通道：局部格矩形经变换后的全局矩形，按与各全局格
// 的交叠面积成比例分配。base_i = v×ov_i/area（整数除法），余额
// R = v−Σbase 按余数（v×ov_i mod area）降序逐格 +1——Σ 恒等于 v（最大
// 余数法）。中间量 int64（红线：交叠面积 int64）。
void scatter_cell(int64_t v, const GEORectT<int64_t>& rect,
                  const DSDensityGrid& global, CMVector<int64_t>& channel) {
    if (v == 0) {
        return;
    }
    const int64_t cell_area =
        static_cast<int64_t>(global.bin_width_) * global.bin_height_;
    // 交叠格下标范围（clamp 到格网；完全越出格网域 → 无格可撒，按
    // accumulate 口径的越界截断语义丢弃）
    const int64_t col_lo = std::max<int64_t>(
        0, floor_div_i64(rect.get_x_low() - global.origin_x_, global.bin_width_));
    const int64_t col_hi =
        std::min<int64_t>(global.cols_ - 1,
                          floor_div_i64(rect.get_x_high() - 1 - global.origin_x_,
                                        global.bin_width_));
    const int64_t row_lo = std::max<int64_t>(
        0, floor_div_i64(rect.get_y_low() - global.origin_y_, global.bin_height_));
    const int64_t row_hi =
        std::min<int64_t>(global.rows_ - 1,
                          floor_div_i64(rect.get_y_high() - 1 - global.origin_y_,
                                        global.bin_height_));
    if (col_lo > col_hi || row_lo > row_hi) {
        return;
    }

    // 交叠格列表（全局/局部格同尺寸正方形：一局部格至多交叠 2×2 = 4 格）
    struct Entry {
        int64_t col;
        int64_t row;
        int64_t overlap;
        int64_t remainder;  // v×ov mod area（余额排序键）
    };
    std::vector<Entry> entries;
    entries.reserve(4);
    int64_t base_sum = 0;
    for (int64_t r = row_lo; r <= row_hi; ++r) {
        for (int64_t c = col_lo; c <= col_hi; ++c) {
            const int64_t gx0 = global.origin_x_ + c * global.bin_width_;
            const int64_t gy0 = global.origin_y_ + r * global.bin_height_;
            const int64_t ox =
                std::min(rect.get_x_high(), gx0 + global.bin_width_) -
                std::max(rect.get_x_low(), gx0);
            const int64_t oy =
                std::min(rect.get_y_high(), gy0 + global.bin_height_) -
                std::max(rect.get_y_low(), gy0);
            if (ox <= 0 || oy <= 0) {
                continue;  // 共享边界线不算交叠（半开区间口径）
            }
            // v×overlap 溢出域备忘（review 2026-09-13）：需单格计数
            // v > 2^63/cell_area（bin=100µm 格约 9×10^8）——正常数据不可达
            //（单格计数上界 ≈ 4×实例总数），如达此域需升 __int128
            const int64_t overlap = ox * oy;
            base_sum += static_cast<int64_t>(v * overlap / cell_area);
            entries.push_back({c, r, overlap, (v * overlap) % cell_area});
        }
    }
    if (entries.empty()) {
        return;
    }
    for (const auto& e : entries) {
        channel[static_cast<size_t>(e.row) * global.cols_ + e.col] +=
            static_cast<int64_t>(v * e.overlap / cell_area);
    }
    // 余额：完全在界内时 Σideal = v（R < 交叠格数）；矩形部分越出全局
    // 格网覆盖域时 leftover 含越界截断丢弃量（v×越界面积占比），可远大于
    // 交叠格数——撒入 = Σfloor + min(leftover, 交叠格数)，剩余即越界丢弃，
    // 符合「格网域外不计」的截断口径（review 2026-09-13：原上界断言仅在
    // 完全在界内时成立，部分越界数据实证误触发 abort）。断言只保下界恒等式。
    int64_t leftover = v - base_sum;
    assert(leftover >= 0);
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) {
                  if (a.remainder != b.remainder) {
                      return a.remainder > b.remainder;  // 余数降序
                  }
                  if (a.row != b.row) {
                      return a.row < b.row;  // tie：行主序小者优先（确定性）
                  }
                  return a.col < b.col;
              });
    for (const auto& e : entries) {
        if (leftover <= 0) {
            break;
        }
        channel[static_cast<size_t>(e.row) * global.cols_ + e.col] += 1;
        --leftover;
    }
}

// 局部通道矩阵 src（格网参数随 local）按变换 t 撒入全局通道 dst
void scatter_channel(const CMVector<int64_t>& src, const DSDensityGrid& local,
                     const GEOTransformT<int64_t>& t,
                     const DSDensityGrid& global, CMVector<int64_t>& dst) {
    for (uint32_t r = 0; r < local.rows_; ++r) {
        for (uint32_t c = 0; c < local.cols_; ++c) {
            const int64_t v = src[static_cast<size_t>(r) * local.cols_ + c];
            if (v == 0) {
                continue;
            }
            // 局部格矩形（int64 域，杜绝大坐标平移溢出）
            const GEORectT<int64_t> cell(
                static_cast<int64_t>(local.origin_x_) +
                    static_cast<int64_t>(c) * local.bin_width_,
                static_cast<int64_t>(local.origin_y_) +
                    static_cast<int64_t>(r) * local.bin_height_,
                static_cast<int64_t>(local.origin_x_) +
                    static_cast<int64_t>(c + 1) * local.bin_width_,
                static_cast<int64_t>(local.origin_y_) +
                    static_cast<int64_t>(r + 1) * local.bin_height_);
            scatter_cell(v, t.apply_box(cell), global, dst);
        }
    }
}

// 复合变换的 int64 域形态（撒入矩形坐标运算全 int64）
GEOTransformT<int64_t> to_i64(const GEOTransform& t) {
    return GEOTransformT<int64_t>(
        GEOPointT<int64_t>(t.get_offset().get_x(), t.get_offset().get_y()),
        t.get_orient());
}

// 一份局部密度格网（实例通道或逐层通道矩阵）整体撒入
void scatter_density_channel(const DSDensityGrid& local,
                             const CMVector<int64_t>& src,
                             const GEOTransformT<int64_t>& t,
                             DSDensityGrid& global, CMVector<int64_t>& dst) {
    if (local.get_cols() == 0 || local.get_rows() == 0) {
        return;  // 局部格网未配置（DIEAREA 缺失兜底）
    }
    scatter_channel(src, local, t, global, dst);
}

}  // namespace

// ── S8 密度合并 ─────────────────────────────────────────────────────
// 后序自底向上合并的线性等价形式：逐级定义「块实例 M 的全局图 = Σ 子块
// 实例全局图平移叠加 + M 自身局部图平移」，对根展开后 = Σ_节点N（N 的
// def 局部图按 N 到根的复合变换撒入一次）——变换复合结合律保证等价。
// 线性实现省去全部中间图构建，内存峰值 = 全局图单份（§4.1 尺度估算：
// 900 万格级可控）。
DSDensityGrid ds_merge_global_density(
    const DSHierTree& tree, const CMVector<const DSBlockBuildData*>& blocks,
    const CMVector<const DSNetBuildData*>& nets) {
    DSDensityGrid global;
    if (tree.node_count() == 0 || blocks.empty()) {
        return global;  // 无 DEF 建库：空图（空结果放行，dev-rules §7）
    }
    if (nets.size() != blocks.size()) {
        // 入参对齐契约与 ds_build_hier_tree 一致（对齐错误已在树构建期
        // fatal；此处防御空转不再报）
        ERR("ds_merge_global_density: nets/blocks size mismatch ({} vs {})",
            nets.size(), blocks.size());
        return global;
    }

    // block 名 → def 序（重名保留首份，与 ds_build_hier_tree 同语义）
    CMUnorderedMap<CMString, uint32_t> def_by_name;
    for (uint32_t i = 0; i < blocks.size(); ++i) {
        def_by_name.emplace(blocks[i]->get_block_name(), i);
    }

    // 全局格网参数 = 根 def 局部格网（原点 = 根 DIEAREA 左下角、bin 同
    // local、ceil 覆盖根 DIEAREA bbox——2026-09-12 裁定 7；root 到根的
    // 变换恒为恒等）
    const auto root_it = def_by_name.find(tree.node(0).get_block_cell_name());
    if (root_it == def_by_name.end()) {
        return global;
    }
    const DSDensityGrid& root_grid = blocks[root_it->second]->density_;
    if (root_grid.get_cols() == 0 || root_grid.get_rows() == 0) {
        return global;  // 根 DIEAREA 缺失（格网未配置）
    }
    global.configure(root_grid.get_origin_x(), root_grid.get_origin_y(),
                     root_grid.get_bin_width(), root_grid.get_bin_height(),
                     root_grid.get_cols(), root_grid.get_rows());

    // 逐节点撒入（nodes_ DFS 前序 = 下标序 → parent id < child id，复合
    // 变换单遍递推无需递归）
    CMVector<GEOTransformT<int64_t>> transforms(tree.node_count());
    for (uint32_t id = 0; id < tree.node_count(); ++id) {
        const DSHierNode& node = tree.node(id);
        if (id == 0) {
            transforms[0] = GEOTransformT<int64_t>();  // root 恒等
        } else {
            // child 复合变换 = 父 ∘（父块实例表中本实例的放置 transform）
            // ——local id = self_global_id − 父块 instance_start_（⑧）
            const DSHierNode& parent = tree.node(node.get_parent_id());
            const uint64_t local_id =
                node.get_self_global_id() - parent.instance_start_;
            const auto pit = def_by_name.find(parent.get_block_cell_name());
            const DSInstance* inst = nullptr;
            if (pit != def_by_name.end()) {
                const auto& instances = blocks[pit->second]->instances_;
                const auto iit = instances.find(local_id);
                if (iit != instances.end()) {
                    inst = &iit->second;
                }
            }
            // 实例缺失（不应发生——树与实例表同源）兜底恒等，不中断建库；
            // WARN 留痕防无声错位（review 2026-09-13：静默兜底难定位）
            if (inst == nullptr) {
                WARN("ds_merge_global_density: block instance transform "
                     "missing for node {} (block {}, local {}) — identity "
                     "fallback (tree/instances desync?)",
                     id, node.get_block_cell_name(), local_id);
            }
            transforms[id] = transforms[node.get_parent_id()].compose(
                inst != nullptr ? to_i64(inst->get_transform())
                                : GEOTransformT<int64_t>());
        }

        const auto dit = def_by_name.find(node.get_block_cell_name());
        if (dit == def_by_name.end()) {
            WARN("ds_merge_global_density: def product missing for tree "
                 "node {} (block {}) — node skipped (tree/products "
                 "desync?)",
                 id, node.get_block_cell_name());
            continue;  // def 未命中（防御，树节点与产物同源不应发生）
        }
        const DSBlockBuildData& block = *blocks[dit->second];
        const DSNetBuildData& net = *nets[dit->second];
        const GEOTransformT<int64_t>& t = transforms[id];

        // 实例通道（S5a per-DEF 产物；block instance footprint 已在 S5a
        // 排除——子块的贡献经子块节点撒入，无双计）
        scatter_density_channel(block.density_, block.density_.counts_, t,
                                global, global.counts_);
        // 金属/通孔逐层分列通道（S5b 网侧产物；三通道独立叠加不混淆）
        for (const auto& [layer_id, counts] : net.density_.metal_layer_counts_) {
            CMVector<int64_t>& dst = global.metal_layer_counts_[layer_id];
            if (dst.empty()) {
                dst.assign(static_cast<size_t>(global.cols_) * global.rows_, 0);
            }
            scatter_density_channel(net.density_, counts, t, global, dst);
        }
        for (const auto& [layer_id, counts] : net.density_.via_layer_counts_) {
            CMVector<int64_t>& dst = global.via_layer_counts_[layer_id];
            if (dst.empty()) {
                dst.assign(static_cast<size_t>(global.cols_) * global.rows_, 0);
            }
            scatter_density_channel(net.density_, counts, t, global, dst);
        }
    }
    return global;
}

namespace {

// ── 决策：合成负载场 + 切线 + 行列分布 ──────────────────────────────

// 合成负载场（§4.1 加权折叠：w_inst×inst + w_metal×Σ层 k_l×metal_l +
// w_via×Σ层 k_l×via_l）及其列/行投影
struct CompositeLoad {
    CMVector<double> cells;     // 行主序
    CMVector<double> col_load;  // 列投影（长度 = cols）
    CMVector<double> row_load;  // 行投影（长度 = rows）
    double total = 0;
};

CompositeLoad compose_load(const DSDensityGrid& g,
                           const DSDensityWeights& w) {
    CompositeLoad out;
    const size_t cells = static_cast<size_t>(g.cols_) * g.rows_;
    out.cells.assign(cells, 0.0);
    out.col_load.assign(g.cols_, 0.0);
    out.row_load.assign(g.rows_, 0.0);
    const auto accumulate = [&](const CMVector<int64_t>& counts, double weight) {
        if (weight == 0.0) {
            return;
        }
        for (uint32_t r = 0; r < g.rows_; ++r) {
            for (uint32_t c = 0; c < g.cols_; ++c) {
                const double v = static_cast<double>(
                    counts[static_cast<size_t>(r) * g.cols_ + c]);
                if (v == 0.0) {
                    continue;
                }
                out.cells[static_cast<size_t>(r) * g.cols_ + c] += v * weight;
            }
        }
    };
    accumulate(g.counts_, w.instance_);
    for (const auto& [layer_id, counts] : g.metal_layer_counts_) {
        accumulate(counts, w.metal_ * w.layer_factor(layer_id));
    }
    for (const auto& [layer_id, counts] : g.via_layer_counts_) {
        accumulate(counts, w.via_ * w.layer_factor(layer_id));
    }
    for (uint32_t r = 0; r < g.rows_; ++r) {
        for (uint32_t c = 0; c < g.cols_; ++c) {
            const double v = out.cells[static_cast<size_t>(r) * g.cols_ + c];
            out.col_load[c] += v;
            out.row_load[r] += v;
            out.total += v;
        }
    }
    return out;
}

// 一维切线（裁定 1）：负载前缀和等分 n 段，切线取「跨过等分点的格右边
// 界」→ cuts[k] = 第 k 条切线的格边界（cuts[0] = 0、cuts[n] = 格数）；
// 负载集中于头部时多条切线同值 → 产生空段（产出侧跳过零格段）。浮点比
// 较带相对容差。
CMVector<uint32_t> prefix_cuts(const CMVector<double>& load, uint32_t n) {
    const uint32_t m = static_cast<uint32_t>(load.size());
    CMVector<uint32_t> cuts;
    cuts.push_back(0);
    double total = 0;
    for (const double v : load) {
        total += v;
    }
    const double eps = total * 1e-9;
    uint32_t k = 1;
    double acc = 0;
    for (uint32_t c = 0; c < m; ++c) {
        acc += load[c];
        while (k < n && acc + eps >= total * static_cast<double>(k) / n) {
            cuts.push_back(c + 1);  // 吸附格右边界
            ++k;
        }
    }
    while (cuts.size() < static_cast<size_t>(n)) {
        cuts.push_back(m);  // 尾部零负载段：切线钳到格网末界（空段）
    }
    cuts.push_back(m);
    return cuts;
}

// 行列分布（裁定 4，负载等效宽高比法）：Wq = 2×负载加权标准差（≈95%
// 展宽，格数单位、下限 1）；均匀负载时 Wq/Hq 恰还原几何宽高比
//（σ = L/√12 → 2σW/2σH = W/H）。nx = clamp(round(√(N·Wq/Hq)), 1, N)、
// ny = ceil(N/nx)。
std::pair<uint32_t, uint32_t> grid_shape(int64_t n,
                                         const CMVector<double>& col_load,
                                         const CMVector<double>& row_load) {
    const auto spread = [](const CMVector<double>& load) {
        double total = 0;
        for (const double v : load) {
            total += v;
        }
        if (total <= 0.0) {
            return 1.0;
        }
        double mean = 0;
        for (size_t i = 0; i < load.size(); ++i) {
            mean += static_cast<double>(i) * load[i];
        }
        mean /= total;
        double var = 0;
        for (size_t i = 0; i < load.size(); ++i) {
            const double d = static_cast<double>(i) - mean;
            var += load[i] * d * d;
        }
        var /= total;
        return std::max(2.0 * std::sqrt(var), 1.0);
    };
    const double wq = spread(col_load);
    const double hq = spread(row_load);
    const double raw = std::sqrt(static_cast<double>(n) * wq / hq);
    const long rounded = std::llround(raw);
    const uint32_t nx =
        static_cast<uint32_t>(std::min<long>(std::max<long>(rounded, 1), n));
    const uint32_t ny =
        static_cast<uint32_t>((n + nx - 1) / nx);
    return {nx, ny};
}

// '{x}x{y}' 解析（x/y ≥ 1）；失败返回 false（调用方 WARN 回退）
bool parse_target_partitions(const CMString& s, uint32_t& nx, uint32_t& ny) {
    const size_t pos = s.find('x');
    if (pos == CMString::npos || pos == 0 || pos + 1 >= s.size()) {
        return false;
    }
    size_t consumed = 0;
    int xv = 0;
    int yv = 0;
    try {
        xv = std::stoi(s, &consumed);
        if (consumed != pos) {
            return false;
        }
        yv = std::stoi(s.substr(pos + 1), &consumed);
        if (consumed != s.size() - pos - 1) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    if (xv <= 0 || yv <= 0) {
        return false;
    }
    nx = static_cast<uint32_t>(xv);
    ny = static_cast<uint32_t>(yv);
    return true;
}

}  // namespace

// ── S8 分区决策 ─────────────────────────────────────────────────────

CMVector<DSSubPartition> ds_decide_partitions(
    const DSDensityGrid& global, const DSStack& stack,
    const DSDensityWeights& weights, const CMString& target_partitions,
    int partition_count, int64_t target_density) {
    CMVector<DSSubPartition> out;
    if (global.get_cols() == 0 || global.get_rows() == 0) {
        return out;  // 格网未配置（无 DEF 建库）：空表
    }
    const CompositeLoad load = compose_load(global, weights);

    // 空负载兜底（裁定 1）：总合成负载 0 → 单分区（无负载信息可切，
    // alpha 键不再参与；core = 全包围盒、extend 全向极值）
    uint32_t nx = 1;
    uint32_t ny = 1;
    bool single = false;
    bool have_shape = false;
    if (load.total <= 0.0) {
        single = true;
    }

    // 优先级：target_partitions > partition_count > target_density（裁定 4；
    // 非法值 DSGN::0013 提醒后回退下一级，不 raise——dev-rules §7）
    if (!single && !target_partitions.empty()) {
        if (parse_target_partitions(target_partitions, nx, ny)) {
            have_shape = true;
        } else {
            MSG("DSGN::0013", 0,
                "invalid alpha target_partitions '{}' (expected '{{x}}x{{y}}' "
                "with x,y >= 1) — falling back", target_partitions);
        }
    }
    if (!single && !have_shape && partition_count > 0) {
        if (partition_count == 1) {
            single = true;
        } else {
            std::tie(nx, ny) =
                grid_shape(partition_count, load.col_load, load.row_load);
            have_shape = true;
        }
    }
    if (!single && !have_shape) {
        int64_t td = target_density;
        if (td <= 0) {
            if (target_density != 0) {
                MSG("DSGN::0013", 0,
                    "invalid alpha partition_target_density {} — falling back "
                    "to default {}",
                    target_density, kDefaultPartitionTargetDensity);
            }
            td = kDefaultPartitionTargetDensity;
        }
        int64_t n = std::max<int64_t>(
            1, static_cast<int64_t>(std::ceil(
                   load.total / static_cast<double>(td))));
        // 钳到格数上限（review 2026-09-13）：病态小 td 下 n 可达 1e9 量级
        //（实例上界 ~1e9 × 权重 6），空段循环空转秒级浪费且 nx cast uint32
        // 有 wrap 风险；分区数超过格数无意义——prefix_cuts 空段机制已保证
        // 等价产出（零格段不产出分区）
        n = std::min<int64_t>(n,
                              static_cast<int64_t>(load.col_load.size()) *
                                  static_cast<int64_t>(load.row_load.size()));
        if (n <= 1) {
            single = true;
        } else {
            std::tie(nx, ny) = grid_shape(n, load.col_load, load.row_load);
        }
    }

    // 切线（吸附格边界）
    const CMVector<uint32_t> col_cuts = prefix_cuts(load.col_load, nx);
    const CMVector<uint32_t> row_cuts = prefix_cuts(load.row_load, ny);

    // w_eff = 最高有效层 default_width（有效层 = 全局金属格值总量 > 0 的
    // ROUTING 层中，自底向上层表序内位置最高者——即物理最高有效层，非层表
    // 登记序的首个有效层；无有效层 = 0——裁定 2）
    int64_t w_eff = 0;
    for (size_t i = stack.layers_.size(); i-- > 0;) {
        const DSLayer& layer = stack.layers_[i];
        if (layer.type_ == static_cast<uint8_t>(DSLayerType::ROUTING) &&
            global.layer_total(layer.id_, false) > 0) {
            w_eff = layer.default_width_;
            break;
        }
    }
    const int64_t extend_w = 2 * w_eff;

    // 分区产出（行主序 (xp, yp)；零格段跳过）
    const int64_t ox = global.origin_x_;
    const int64_t oy = global.origin_y_;
    const int64_t bw = global.bin_width_;
    const int64_t bh = global.bin_height_;
    const int32_t int_min = std::numeric_limits<int32_t>::min();
    const int32_t int_max = std::numeric_limits<int32_t>::max();
    for (uint32_t yp = 0; yp < ny; ++yp) {
        const uint32_t r0 = row_cuts[yp];
        const uint32_t r1 = row_cuts[yp + 1];
        if (r1 <= r0) {
            continue;  // 空段
        }
        for (uint32_t xp = 0; xp < nx; ++xp) {
            const uint32_t c0 = col_cuts[xp];
            const uint32_t c1 = col_cuts[xp + 1];
            if (c1 <= c0) {
                continue;  // 空段
            }
            DSSubPartition p;
            p.partition_id_ = static_cast<uint32_t>(out.size());
            // 格边界换算（int64 中间量；格网覆盖域内不溢出 int32）
            const int32_t x_low = static_cast<int32_t>(ox + c0 * bw);
            const int32_t y_low = static_cast<int32_t>(oy + r0 * bh);
            const int32_t x_high = static_cast<int32_t>(ox + c1 * bw);
            const int32_t y_high = static_cast<int32_t>(oy + r1 * bh);
            p.core_rect_ = GEORect(x_low, y_low, x_high, y_high);
            // extend：非边缘方向 core 边界向外扩 2×w_eff；最外围方向
            // 不截断、直接开到 int32 极值（允许相邻重叠——裁定 2）
            const int32_t ex_low =
                xp == 0 ? int_min : static_cast<int32_t>(x_low - extend_w);
            const int32_t ex_high =
                xp == nx - 1 ? int_max : static_cast<int32_t>(x_high + extend_w);
            const int32_t ey_low =
                yp == 0 ? int_min : static_cast<int32_t>(y_low - extend_w);
            const int32_t ey_high =
                yp == ny - 1 ? int_max : static_cast<int32_t>(y_high + extend_w);
            p.extend_rect_ = GEORect(ex_low, ey_low, ex_high, ey_high);
            out.push_back(std::move(p));
        }
    }
    return out;
}

}  // namespace fly
