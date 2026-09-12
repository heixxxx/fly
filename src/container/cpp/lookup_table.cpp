#include <container/cpp/lookup_table.h>

#include <log/cpp/logger.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fly {

namespace {

// 行优先线性索引：offset(i0, i1, ...) = ((i0*n1 + i1)*n2 + i2)
size_t row_major_offset(const CMVector<size_t>& axis_sizes,
                        const CMVector<size_t>& axis_pos) {
    size_t off = 0;
    for (size_t d = 0; d < axis_sizes.size(); ++d) {
        off = off * axis_sizes[d] + axis_pos[d];
    }
    return off;
}

}  // namespace

bool CMLookupTable::resolve_template(const CMLookupTableTemplate& tmpl) {
    // 错误处理语义（2026-09-12 裁定，原 invalid_argument 改 bool）：补全失败
    // = 表格数据与模板不一致（上游数据入口防线）——WARN 一条（带表名与原因，
    // 防刷屏不逐轴打），调用方应抛弃该表并经 message 提醒。
    if (tmpl.variable_names_.size() != tmpl.index_sets_.size()) {
        WARN("CMLookupTable::resolve_template: template '{}' axis count mismatch (names={}, indexes={})",
             name_, tmpl.variable_names_.size(), tmpl.index_sets_.size());
        return false;
    }
    if (static_cast<size_t>(dim_) != tmpl.variable_names_.size()) {
        WARN("CMLookupTable::resolve_template: table '{}' dim {} != template dim {}",
             name_, dim_, tmpl.variable_names_.size());
        return false;
    }

    variable_names_ = tmpl.variable_names_;
    index_sets_.resize(dim_);
    size_t expected = 1;
    for (size_t d = 0; d < dim_; ++d) {
        if (d < index_sets_.size() && !index_sets_[d].empty()) {
            // 表级覆盖轴：保留表的索引
        } else {
            index_sets_[d] = tmpl.index_sets_[d];
        }
        if (index_sets_[d].empty()) {
            WARN("CMLookupTable::resolve_template: table '{}' axis {} empty after resolve",
                 name_, d);
            return false;
        }
        expected *= index_sets_[d].size();
    }

    if (values_.size() != expected) {
        WARN("CMLookupTable::resolve_template: table '{}' values size {} != expected {}",
             name_, values_.size(), expected);
        return false;
    }
    return true;
}

bool CMLookupTable::is_ready() const {
    if (dim_ <= 0 || static_cast<size_t>(dim_) != variable_names_.size() ||
        static_cast<size_t>(dim_) != index_sets_.size()) {
        return false;
    }
    size_t expected = 1;
    for (const auto& axis : index_sets_) {
        if (axis.empty()) {
            return false;  // 单点轴合法（常数表，插值退化到端点权重 1.0）
        }
        if (!std::is_sorted(axis.begin(), axis.end())) {
            return false;
        }
        expected *= axis.size();
    }
    return values_.size() == expected;
}

bool CMLookupTable::interpolate(const CMVector<double>& coords, double& result) const {
    // 最后防线，正常流程不触达（is_ready 通过后才插值）：前置条件不满足直接
    // return false（不打日志），result 不写，调用方兜底处理。
    if (!is_ready()) {
        return false;
    }
    if (coords.size() != static_cast<size_t>(dim_)) {
        return false;
    }

    // 每轴：clamp 坐标 → 相邻格点对 (i, i+1) + 归一化权重 t。
    CMVector<size_t> axis_sizes(dim_);
    CMVector<size_t> pos_lo(dim_);
    CMVector<double> weight_lo(dim_);   // 下格点权重；上格点权重 = 1 - weight_lo
    for (size_t d = 0; d < dim_; ++d) {
        const auto& axis = index_sets_[d];
        const double c = std::clamp(coords[d], axis.front(), axis.back());
        // 上界二分（axis 升序）：第一个 >= c 的点
        auto up = std::lower_bound(axis.begin(), axis.end(), c);
        if (up == axis.begin()) {
            axis_sizes[d] = axis.size();
            pos_lo[d] = 0;
            weight_lo[d] = 1.0;
            continue;
        }
        auto lo = std::prev(up);
        const double span = *up - *lo;
        axis_sizes[d] = axis.size();
        pos_lo[d] = static_cast<size_t>(lo - axis.begin());
        weight_lo[d] = span > 0.0 ? (*up - c) / span : 0.0;
        // c == *up 时 weight_lo = 0（退化到上格点），span=0 时同样取上格点
    }

    // 2^dim 角点加权（dim ≤ 3 → 最多 8 角点）
    const size_t n_corners = size_t{1} << dim_;
    result = 0.0;
    CMVector<size_t> axis_pos(dim_);
    for (size_t corner = 0; corner < n_corners; ++corner) {
        double w = 1.0;
        bool weight_zero = false;
        for (size_t d = 0; d < dim_; ++d) {
            const bool take_lo = (corner >> d) & 1ULL;
            w *= take_lo ? weight_lo[d] : (1.0 - weight_lo[d]);
            if (w == 0.0) {
                weight_zero = true;  // 权重 0 角点跳过（避免越界索引计算白做）
                break;
            }
            axis_pos[d] = take_lo ? pos_lo[d] : pos_lo[d] + 1;
        }
        if (weight_zero) {
            continue;
        }
        result += w * values_[row_major_offset(axis_sizes, axis_pos)];
    }
    return true;
}

}  // namespace fly
