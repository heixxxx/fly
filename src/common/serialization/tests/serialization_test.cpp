#include <gtest/gtest.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>

struct TestMessage {
    int32_t id = 0;
    CMString name;
    double value = 0.0;
    FLY_SERIALIZE(id, name, value)
};

TEST(SerializationTest, EncodeDecodeMessage) {
    TestMessage original{42, "test", 3.14};
    CMString serialized; FLY_ENCODE(original, serialized);
    TestMessage decoded; FLY_DECODE(serialized, TestMessage, decoded);
    EXPECT_EQ(decoded.id, original.id);
    EXPECT_EQ(decoded.name, original.name);
    EXPECT_DOUBLE_EQ(decoded.value, original.value);
}

TEST(SerializationTest, EmptyMessage) {
    TestMessage original{0, "", 0.0};
    CMString serialized; FLY_ENCODE(original, serialized);
    TestMessage decoded; FLY_DECODE(serialized, TestMessage, decoded);
    EXPECT_EQ(decoded.id, 0); EXPECT_EQ(decoded.name, "");
    EXPECT_DOUBLE_EQ(decoded.value, 0.0);
}

TEST(SerializationTest, LargeString) {
    TestMessage original{1, CMString(1000, 'x'), 2.5};
    CMString serialized; FLY_ENCODE(original, serialized);
    TestMessage decoded; FLY_DECODE(serialized, TestMessage, decoded);
    EXPECT_EQ(decoded.name.size(), 1000);
}

struct VectorMessage {
    CMVector<int32_t> numbers;
    CMVector<CMString> strings;
    FLY_SERIALIZE(numbers, strings)
};

TEST(SerializationTest, VectorOfInts) {
    VectorMessage original; original.numbers = {1, 2, 3, 4, 5};
    CMString serialized; FLY_ENCODE(original, serialized);
    VectorMessage decoded; FLY_DECODE(serialized, VectorMessage, decoded);
    EXPECT_EQ(decoded.numbers.size(), 5);
}

struct NestedInner {
    int32_t x = 0; CMString label;
    FLY_SERIALIZE(x, label)
};

struct NestedOuter {
    NestedInner inner;
    int32_t outer_value = 0;
    FLY_SERIALIZE(inner, outer_value)
};

TEST(SerializationTest, NestedStruct) {
    NestedOuter original;
    original.inner.x = 42; original.inner.label = "nested";
    original.outer_value = 100;
    CMString serialized; FLY_ENCODE(original, serialized);
    NestedOuter decoded; FLY_DECODE(serialized, NestedOuter, decoded);
    EXPECT_EQ(decoded.inner.x, 42);
    EXPECT_EQ(decoded.inner.label, "nested");
    EXPECT_EQ(decoded.outer_value, 100);
}

struct MapMessage {
    CMMap<CMString, int32_t> int_map;
    CMMap<int32_t, CMString> reverse_map;
    FLY_SERIALIZE(int_map, reverse_map)
};

TEST(SerializationTest, Map) {
    MapMessage original;
    original.int_map["k"] = 1; original.reverse_map[1] = "v";
    CMString serialized; FLY_ENCODE(original, serialized);
    MapMessage decoded; FLY_DECODE(serialized, MapMessage, decoded);
    EXPECT_EQ(decoded.int_map.size(), 1);
    EXPECT_EQ(decoded.reverse_map.size(), 1);
}

struct AllTypesMessage {
    int32_t int_val = 0; int64_t long_val = 0;
    CMString str_val; CMVector<int32_t> vec_val;
    FLY_SERIALIZE(int_val, long_val, str_val, vec_val)
};

TEST(SerializationTest, AllTypes) {
    AllTypesMessage original{123, 9876543210LL, "test", {1, 2, 3}};
    CMString serialized; FLY_ENCODE(original, serialized);
    AllTypesMessage decoded; FLY_DECODE(serialized, AllTypesMessage, decoded);
    EXPECT_EQ(decoded.int_val, 123);
    EXPECT_EQ(decoded.vec_val.size(), 3);
}

TEST(SerializationTest, LargeData) {
    VectorMessage original;
    original.numbers = CMVector<int32_t>(10000, 42);
    CMString serialized; FLY_ENCODE(original, serialized);
    EXPECT_GT(serialized.size(), 10000);
    VectorMessage decoded; FLY_DECODE(serialized, VectorMessage, decoded);
    EXPECT_EQ(decoded.numbers.size(), 10000);
}

TEST(SerializationTest, ZeroValues) {
    AllTypesMessage original{};
    CMString serialized; FLY_ENCODE(original, serialized);
    AllTypesMessage decoded; FLY_DECODE(serialized, AllTypesMessage, decoded);
    EXPECT_EQ(decoded.int_val, 0);
    EXPECT_TRUE(decoded.str_val.empty());
    EXPECT_TRUE(decoded.vec_val.empty());
}
// —— shared_ptr 字段（CMSharedPtr = std::shared_ptr 别名；FLY_FIELD 接
//    bitsery 原生 ext::StdSmartPtr——空指针原生处理、SharedOwner 共享
//    所有权语义，见 bitsery 文档）——

struct SharedPtrTarget {
    int32_t v = 0;
    CMString label;
    CMVector<int32_t> nums;
    FLY_SERIALIZE(v, label, nums)
};

struct SharedPtrHolder {
    // 空（null）与非空各一字段 + 嵌套所指类型（普通字段 + 容器字段）
    CMSharedPtr<SharedPtrTarget> full;
    CMSharedPtr<SharedPtrTarget> empty;
    FLY_SERIALIZE(full, empty)
};

TEST(SerializationSharedPtrTest, NullAndNonNullRoundTrip) {
    SharedPtrHolder original;
    original.full = CMMakeShared<SharedPtrTarget>();
    original.full->v = 7;
    original.full->label = "hello";
    original.full->nums = {1, 2, 3};
    // empty 保持 null

    CMString serialized;
    FLY_ENCODE(original, serialized);
    SharedPtrHolder decoded;
    FLY_DECODE(serialized, SharedPtrHolder, decoded);

    ASSERT_NE(decoded.full, nullptr);
    EXPECT_EQ(decoded.full->v, 7);
    EXPECT_EQ(decoded.full->label, "hello");
    ASSERT_EQ(decoded.full->nums.size(), 3u);
    EXPECT_EQ(decoded.full->nums[2], 3);
    // 空指针 round-trip 保持为空
    EXPECT_EQ(decoded.empty, nullptr);
}

TEST(SerializationSharedPtrTest, ReSerializeEquivalence) {
    // 非空 round-trip 后再序列化等价（字节级一致）
    SharedPtrHolder original;
    original.full = CMMakeShared<SharedPtrTarget>();
    original.full->v = -5;
    original.full->label = "eq";

    CMString first;
    FLY_ENCODE(original, first);
    SharedPtrHolder decoded;
    FLY_DECODE(first, SharedPtrHolder, decoded);
    CMString second;
    FLY_ENCODE(decoded, second);
    EXPECT_EQ(first, second);
}

TEST(SerializationSharedPtrTest, CMSharedPtrAliasDirectUse) {
    // CMSharedPtr（别名）与 std::shared_ptr 同型直用
    static_assert(std::is_same_v<CMSharedPtr<SharedPtrTarget>,
                                 std::shared_ptr<SharedPtrTarget>>);
    CMSharedPtr<SharedPtrTarget> p = CMMakeShared<SharedPtrTarget>();
    p->v = 9;

    SharedPtrHolder original;
    original.full = p;
    CMString serialized;
    FLY_ENCODE(original, serialized);
    SharedPtrHolder decoded;
    FLY_DECODE(serialized, SharedPtrHolder, decoded);
    ASSERT_NE(decoded.full, nullptr);
    EXPECT_EQ(decoded.full->v, 9);
}

TEST(SerializationSharedPtrTest, SharedTopologyPreserved) {
    // 共享拓扑语义确认：同一对象被同一 holder 的两个字段引用——StdSmartPtr
    // 经 PointerLinkingContext 记账，同会话内共享拓扑保留：round-trip 后
    // 两字段仍指向同一重建对象（指针相同、内容相同）
    SharedPtrHolder original;
    original.full = CMMakeShared<SharedPtrTarget>();
    original.full->v = 42;
    original.empty = original.full;  // 同一对象两字段共享

    CMString serialized;
    FLY_ENCODE(original, serialized);
    SharedPtrHolder decoded;
    FLY_DECODE(serialized, SharedPtrHolder, decoded);

    ASSERT_NE(decoded.full, nullptr);
    ASSERT_NE(decoded.empty, nullptr);
    EXPECT_EQ(decoded.full->v, 42);
    EXPECT_EQ(decoded.empty->v, 42);
    // 同会话共享拓扑保留（SharedOwner 记账）：两字段同对象
    EXPECT_EQ(decoded.full.get(), decoded.empty.get());
}

// =============================================================================
// FLY_SERIALIZE_EXTERNAL — 第三方类型外接序列化（宏包装的 ADL 自由函数）
// 模拟第三方风格结构：无成员 serialize、字段私有不可改定义——外接宏
// 经 bitsery selectSerializeFnc 的 ADL 路由命中自由函数
// =============================================================================

struct ThirdPartyStyle {
    // 模拟第三方库字段（public 但类型不可加成员函数——如 tsl::htrie_map）
    int32_t x_ = 0;
    CMString name_;

    int32_t x() const { return x_; }
    const CMString& name() const { return name_; }
};

// 字段版外接（对象参数名固定 o——FLY_FIELD 以 o.field 派发）
FLY_SERIALIZE_EXTERNAL(ThirdPartyStyle, x_, name_)

TEST(SerializationExternalTest, AdlRouteFieldVersionRoundTrip) {
    static_assert(!fly_ser::is_deserializer_v<int>);

    ThirdPartyStyle original;
    original.x_ = 77;
    original.name_ = "external";

    CMString serialized;
    FLY_ENCODE(original, serialized);

    ThirdPartyStyle decoded;
    FLY_DECODE(serialized, ThirdPartyStyle, decoded);
    EXPECT_EQ(decoded.x(), 77);
    EXPECT_EQ(decoded.name(), "external");
}

