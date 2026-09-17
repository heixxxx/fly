#pragma once

// 设计单测共用的借用观察辅助（评审 B-12：业务层借用指针集
// `CMVector<const T*>&` 全部清退为 `CMVector<CMSharedPtr<const T>>` 后，
// 测试侧栈对象入集的统一形态——aliasing 空 owning shared_ptr 不管理
// 生命周期，与旧裸指针借用语义等价，仅限同步调用期使用）。

#include <common/types/cpp/pointer_aliases.h>

namespace fly::test {

// 只读借用（入 CMVector<CMSharedPtr<const T>> / 只读环境注入）
template <typename T>
CMSharedPtr<const T> borrow(const T& obj) {
    return CMSharedPtr<const T>{CMSharedPtr<T>(), &obj};
}

// 可变借用（入产物容器等可变环境注入——CMSharedPtr<T>）
template <typename T>
CMSharedPtr<T> borrow(T& obj) {
    return CMSharedPtr<T>{CMSharedPtr<T>(), &obj};
}

}  // namespace fly::test
