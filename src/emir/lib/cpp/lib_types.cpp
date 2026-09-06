#include <emir/lib/cpp/lib_types.h>

#include <stdexcept>

namespace fly {

void LIBLibrary::build_cell_index() {
    cell_index_.clear();
    cell_index_.reserve(cells_.size() * 2);
    for (size_t i = 0; i < cells_.size(); ++i) {
        cell_index_[cells_[i].name_] = i;
    }
}

const LIBCell* LIBLibrary::find_cell(const CMString& name) const {
    // 惰性建索引：反序列化回来的容器 index 为空。首次调用建索引——
    // 建成后只读，多线程并发 find 安全；但「首次调用」本身须单线程
    // （编排层保证：解析/组装任务单线程完成，find 在其后的任务中发生）。
    auto* self = const_cast<LIBLibrary*>(this);
    if (cell_index_.empty() && !cells_.empty()) {
        self->build_cell_index();
    }
    auto it = cell_index_.find(name);
    return it == cell_index_.end() ? nullptr : &cells_[it->second];
}

const CMLookupTableTemplate* LIBLibrary::find_template(const CMString& name) const {
    for (size_t i = 0; i < template_names_.size(); ++i) {
        if (template_names_[i] == name) {
            return &templates_[i];
        }
    }
    return nullptr;
}

void LIBLibrary::record_skipped_group(const CMString& type_name) {
    for (size_t i = 0; i < skipped_group_names_.size(); ++i) {
        if (skipped_group_names_[i] == type_name) {
            ++skipped_group_counts_[i];
            return;
        }
    }
    skipped_group_names_.push_back(type_name);
    skipped_group_counts_.push_back(1);
}

}  // namespace fly
