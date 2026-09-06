#pragma once

// ── 基础别名（指针族）── common/types 子模块 ─────────────────────────
// 非容器的基础类型别名：智能指针族与其配套工具。永久稳定层。
// 容器类型别名见 container 模块的 container/container_aliases.h
// （CMVector/CMMap/CMString 等，可整体替换底层实现的可替换别名层）。

#include <memory>
#include <utility>

namespace fly {

template<typename T>
using CMSharedPtr = std::shared_ptr<T>;

template<typename T>
using CMUniquePtr = std::unique_ptr<T>;

template<typename T>
using CMWeakPtr = std::weak_ptr<T>;

template<typename T, typename... Args>
constexpr CMSharedPtr<T> CMMakeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
constexpr CMUniquePtr<T> CMMakeUnique(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T, typename U>
constexpr CMSharedPtr<T> CMStaticPointerCast(const CMSharedPtr<U>& r) noexcept {
    return std::static_pointer_cast<T>(r);
}

template<typename T, typename U>
constexpr CMSharedPtr<T> CMDynamicPointerCast(const CMSharedPtr<U>& r) noexcept {
    return std::dynamic_pointer_cast<T>(r);
}

template<typename T, typename U>
constexpr CMSharedPtr<T> CMConstPointerCast(const CMSharedPtr<U>& r) noexcept {
    return std::const_pointer_cast<T>(r);
}

template<typename T, typename U>
constexpr CMSharedPtr<T> CMReinterpretPointerCast(const CMSharedPtr<U>& r) noexcept {
    return std::reinterpret_pointer_cast<T>(r);
}

}

using fly::CMSharedPtr;
using fly::CMUniquePtr;
using fly::CMWeakPtr;
using fly::CMMakeShared;
using fly::CMMakeUnique;
using fly::CMStaticPointerCast;
using fly::CMDynamicPointerCast;
using fly::CMConstPointerCast;
using fly::CMReinterpretPointerCast;
