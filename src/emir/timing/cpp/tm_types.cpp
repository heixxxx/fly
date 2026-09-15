#include <emir/timing/cpp/tm_types.h>

#include <algorithm>
#include <utility>

namespace fly {

void TMTimingFile::rebuild_indexes() {
    clock_index_.clear();
    entry_index_.clear();
    clock_index_.reserve(clocks_.size());
    entry_index_.reserve(entries_.size());
    for (uint32_t i = 0; i < clocks_.size(); ++i) {
        clock_index_.emplace(clocks_[i].name_, i);
    }
    for (size_t i = 0; i < entries_.size(); ++i) {
        entry_index_.emplace(entries_[i].name_, i);
    }
}

void TMTimingFile::upsert_timing(TMNameTiming&& e) {
    auto it = entry_index_.find(e.name_);
    if (it == entry_index_.end()) {
        entry_index_.emplace(e.name_, entries_.size());
        entries_.push_back(std::move(e));
        return;
    }
    TMNameTiming& dst = entries_[it->second];
    if (e.is_constant()) {
        dst.set_constant();
    }
    // 逐字段并集：单侧有值取值、双侧有值取宽
    auto union_range = [](const TMRange& src, const TMRange& dst_v,
                          bool src_has, bool dst_has, TMRange& out) {
        if (!src_has) {
            return;
        }
        if (!dst_has) {
            out = src;
            return;
        }
        out.min_ = std::min(dst_v.min_, src.min_);
        out.max_ = std::max(dst_v.max_, src.max_);
    };
    union_range(e.rise_arrival_, dst.rise_arrival_, e.is_rise_arrival(),
                dst.is_rise_arrival(), dst.rise_arrival_);
    if (e.is_rise_arrival()) {
        dst.set_rise_arrival();
    }
    union_range(e.fall_arrival_, dst.fall_arrival_, e.is_fall_arrival(),
                dst.is_fall_arrival(), dst.fall_arrival_);
    if (e.is_fall_arrival()) {
        dst.set_fall_arrival();
    }
    union_range(e.rise_slew_, dst.rise_slew_, e.is_rise_slew(),
                dst.is_rise_slew(), dst.rise_slew_);
    if (e.is_rise_slew()) {
        dst.set_rise_slew();
    }
    union_range(e.fall_slew_, dst.fall_slew_, e.is_fall_slew(),
                dst.is_fall_slew(), dst.fall_slew_);
    if (e.is_fall_slew()) {
        dst.set_fall_slew();
    }
    // 时钟源：无 → 有取有；有 ≠ 有置多源
    if (e.clock_id_ != kTMNoClock) {
        if (dst.clock_id_ == kTMNoClock) {
            dst.clock_id_ = e.clock_id_;
        } else if (dst.clock_id_ != e.clock_id_) {
            dst.set_multi_source();
        }
    }
}

const TMNameTiming* TMTimingFile::find_entry(const CMString& name) const {
    auto it = entry_index_.find(name);
    return it == entry_index_.end() ? nullptr : &entries_[it->second];
}

const TMClock* TMTimingFile::find_clock(const CMString& name) const {
    auto it = clock_index_.find(name);
    return it == clock_index_.end() ? nullptr : &clocks_[it->second];
}

}  // namespace fly
