#pragma once

#include <map>
#include <unordered_map>
#include <vector>
#include <set>
#include <unordered_set>
#include <list>
#include <deque>
#include <queue>
#include <stack>
#include <string>
#include <memory>
#include <utility>

namespace fly {

template<typename K, typename V>
using CMMap = std::map<K, V>;

template<typename K, typename V>
using CMUnorderedMap = std::unordered_map<K, V>;

template<typename T>
using CMVector = std::vector<T>;

template<typename T>
using CMSet = std::set<T>;

template<typename T>
using CMUnorderedSet = std::unordered_set<T>;

template<typename T>
using CMList = std::list<T>;

template<typename T>
using CMDeque = std::deque<T>;

template<typename T>
using CMQueue = std::queue<T>;

template<typename T>
using CMStack = std::stack<T>;

using CMString = std::string;

template<typename T>
using CMSharedPtr = std::shared_ptr<T>;

template<typename T>
using CMUniquePtr = std::unique_ptr<T>;

template<typename T>
using CMWeakPtr = std::weak_ptr<T>;

template<typename K, typename V>
using CMMapKV = std::pair<K, V>;

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

using fly::CMMap;
using fly::CMUnorderedMap;
using fly::CMVector;
using fly::CMSet;
using fly::CMUnorderedSet;
using fly::CMList;
using fly::CMDeque;
using fly::CMQueue;
using fly::CMStack;
using fly::CMString;
using fly::CMSharedPtr;
using fly::CMUniquePtr;
using fly::CMWeakPtr;
using fly::CMMapKV;
using fly::CMMakeShared;
using fly::CMMakeUnique;
using fly::CMStaticPointerCast;
using fly::CMDynamicPointerCast;
using fly::CMConstPointerCast;
using fly::CMReinterpretPointerCast;