#include <gtest/gtest.h>
#include <container/cpp/container_aliases.h>

TEST(CommonTypesTest, CMMapBasicOperations) {
    CMMap<CMString, int32_t> map;
    
    map["key1"] = 100;
    map["key2"] = 200;
    
    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map["key1"], 100);
    EXPECT_EQ(map["key2"], 200);
    EXPECT_TRUE(map.contains("key1"));
    EXPECT_FALSE(map.contains("key3"));
}

TEST(CommonTypesTest, CMMapIteration) {
    CMMap<int32_t, CMString> map;
    map[1] = "one";
    map[2] = "two";
    map[3] = "three";
    
    int32_t sum_keys = 0;
    for (const auto& [k, v] : map) {
        sum_keys += k;
    }
    EXPECT_EQ(sum_keys, 6);
}

TEST(CommonTypesTest, CMUnorderedMapBasicOperations) {
    CMUnorderedMap<CMString, int32_t> map;
    
    map["alpha"] = 1;
    map["beta"] = 2;
    map["gamma"] = 3;
    
    EXPECT_EQ(map.size(), 3);
    EXPECT_EQ(map["alpha"], 1);
    EXPECT_TRUE(map.contains("beta"));
}

TEST(CommonTypesTest, CMVectorBasicOperations) {
    CMVector<int32_t> vec;
    
    vec.push_back(10);
    vec.push_back(20);
    vec.push_back(30);
    
    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[0], 10);
    EXPECT_EQ(vec[1], 20);
    EXPECT_EQ(vec[2], 30);
    
    vec.pop_back();
    EXPECT_EQ(vec.size(), 2);
}

TEST(CommonTypesTest, CMVectorResize) {
    CMVector<int32_t> vec;
    vec.resize(100, 42);
    
    EXPECT_EQ(vec.size(), 100);
    EXPECT_EQ(vec[0], 42);
    EXPECT_EQ(vec[99], 42);
}

TEST(CommonTypesTest, CMSetBasicOperations) {
    CMSet<int32_t> set;
    
    set.insert(10);
    set.insert(20);
    set.insert(10);
    
    EXPECT_EQ(set.size(), 2);
    EXPECT_TRUE(set.contains(10));
    EXPECT_TRUE(set.contains(20));
    EXPECT_FALSE(set.contains(30));
}

TEST(CommonTypesTest, CMUnorderedSetBasicOperations) {
    CMUnorderedSet<CMString> set;
    
    set.insert("apple");
    set.insert("banana");
    set.insert("apple");
    
    EXPECT_EQ(set.size(), 2);
    EXPECT_TRUE(set.contains("apple"));
}

TEST(CommonTypesTest, CMListBasicOperations) {
    CMList<int32_t> list;
    
    list.push_back(1);
    list.push_front(0);
    list.push_back(2);
    
    EXPECT_EQ(list.size(), 3);
    EXPECT_EQ(list.front(), 0);
    EXPECT_EQ(list.back(), 2);
}

TEST(CommonTypesTest, CMDequeBasicOperations) {
    CMDeque<int32_t> deque;
    
    deque.push_back(10);
    deque.push_front(5);
    deque.push_back(15);
    
    EXPECT_EQ(deque.size(), 3);
    EXPECT_EQ(deque.front(), 5);
    EXPECT_EQ(deque.back(), 15);
    
    deque.pop_front();
    EXPECT_EQ(deque.front(), 10);
}

TEST(CommonTypesTest, CMQueueBasicOperations) {
    CMQueue<int32_t> queue;
    
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    EXPECT_EQ(queue.front(), 1);
    queue.pop();
    EXPECT_EQ(queue.front(), 2);
    EXPECT_EQ(queue.size(), 2);
}

TEST(CommonTypesTest, CMStackBasicOperations) {
    CMStack<int32_t> stack;
    
    stack.push(10);
    stack.push(20);
    stack.push(30);
    
    EXPECT_EQ(stack.top(), 30);
    stack.pop();
    EXPECT_EQ(stack.top(), 20);
    EXPECT_EQ(stack.size(), 2);
}

TEST(CommonTypesTest, CMStringBasicOperations) {
    CMString str = "hello";
    
    EXPECT_EQ(str.size(), 5);
    EXPECT_EQ(str, "hello");
    
    str += " world";
    EXPECT_EQ(str, "hello world");
    EXPECT_EQ(str.size(), 11);
}

TEST(CommonTypesTest, CMStringEmpty) {
    CMString empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.size(), 0);
}

TEST(CommonTypesTest, CMMapKVPair) {
    CMMapKV<int32_t, CMString> pair{42, "answer"};
    
    EXPECT_EQ(pair.first, 42);
    EXPECT_EQ(pair.second, "answer");
    
    pair.second = "new value";
    EXPECT_EQ(pair.second, "new value");
}

TEST(CommonTypesTest, NestedContainers) {
    CMMap<CMString, CMVector<int32_t>> nested;
    
    nested["vec1"] = {1, 2, 3};
    nested["vec2"] = {4, 5, 6, 7};
    
    EXPECT_EQ(nested["vec1"].size(), 3);
    EXPECT_EQ(nested["vec2"][2], 6);
}

TEST(CommonTypesTest, ComplexKeyType) {
    CMMap<CMMapKV<int32_t, int32_t>, CMString> map;
    
    CMMapKV<int32_t, int32_t> key1{1, 2};
    CMMapKV<int32_t, int32_t> key2{3, 4};
    
    map[key1] = "pair_1_2";
    map[key2] = "pair_3_4";
    
    CMString result = map[key1];
    EXPECT_EQ(result, "pair_1_2");
}