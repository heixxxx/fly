#pragma once

// ── 容器类型别名层 ── container 模块 ─────────────────────────────────
// CM* 容器别名的当前绑定（std 实现）。设计定位：**可替换别名层**——
// 随时可整体替换底层实现（自定义容器），业务代码零改动；自定义容器
// 实现（如 lookup_table.h 的 CMLookupTable）同属 container 模块。
//
// 本头聚合基础别名（include common/types 的 pointer_aliases.h）——
// 使用方 include 本头即可获得全部 CM* 符号。

#include <common/types/cpp/pointer_aliases.h>

#include <deque>
#include <functional>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fly {

// Hash combiner for std::tuple — enables CMUnorderedSet<std::tuple<...>>
template<typename Tuple, std::size_t... Is>
size_t tuple_hash_impl(const Tuple& t, std::index_sequence<Is...>) {
    size_t seed = 0;
    ((seed ^= std::hash<std::tuple_element_t<Is, Tuple>>{}(std::get<Is>(t)) + 0x9e3779b9 + (seed << 6) + (seed >> 2)), ...);
    return seed;
}

}

namespace std {
template<typename... Ts>
struct hash<std::tuple<Ts...>> {
    size_t operator()(const std::tuple<Ts...>& t) const {
        return fly::tuple_hash_impl(t, std::index_sequence_for<Ts...>{});
    }
};
}

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

template<typename K, typename V>
using CMMapKV = std::pair<K, V>;

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
using fly::CMMapKV;
