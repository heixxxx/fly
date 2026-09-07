#include <emir/lib/cpp/lib_types.h>

#include <message/cpp/message_macros.h>

namespace fly {

size_t LIBLibrary::merge_from(const LIBLibrary& src) {
    // cell 冲突 = 库版本混用（业务异常场景，非解析错误）：保留当前
    // （首次出现）、抛弃后续重复，LIBR::0001 逐次提醒并携带两处来源
    // 路径；header_attrs 保留首个文件的（后续文件不再覆盖）。
    size_t dropped = 0;
    for (const auto& cell : src.cells_) {
        if (find_cell(cell.name_) != nullptr) {
            ++dropped;
            const LIBCell* kept = find_cell(cell.name_);
            MSG("LIBR::0001", 0,
                "duplicate cell '{}' from '{}' dropped (already defined in "
                "'{}'); keeping first", cell.name_, cell.source_file_,
                kept ? kept->source_file_ : "");
            continue;
        }
        cells_.push_back(cell);
    }

    // 模板集并入（重名先入优先），skipped_group 统计累加。
    for (size_t i = 0; i < src.template_names_.size(); ++i) {
        if (find_template(src.template_names_[i]) == nullptr) {
            template_names_.push_back(src.template_names_[i]);
            templates_.push_back(src.templates_[i]);
        }
    }
    for (size_t i = 0; i < src.skipped_group_names_.size(); ++i) {
        for (int j = 0; j < src.skipped_group_counts_[i]; ++j) {
            record_skipped_group(src.skipped_group_names_[i]);
        }
    }

    build_cell_index();
    return dropped;
}

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
