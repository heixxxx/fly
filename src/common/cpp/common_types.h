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