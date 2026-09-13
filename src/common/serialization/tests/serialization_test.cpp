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

// =============================================================================
// 扩展容器/复合族（2026-09-13 一次性补齐 bitsery v5.2.4 类型分派面）：
// set 四种 / optional / variant / tuple + pair / array / list 族（list/
// forward_list/deque）/ queue / stack / atomic / chrono / bitset——每族
// 往返（含空容器边界）；元素分派统一经 fly_ser::elem。
// =============================================================================

struct SetMessage {
    CMSet<int32_t> ordered;
    CMUnorderedSet<uint64_t> hashed;
    std::multiset<CMString> multi;
    std::unordered_multiset<int32_t> hashed_multi;
    FLY_SERIALIZE(ordered, hashed, multi, hashed_multi)
};

// 独立 unordered_set 字段（pg 全局集同型）
struct PgSetMsg {
    CMUnorderedSet<uint64_t> ids;
    FLY_SERIALIZE(ids)
};

TEST(SerializationExtendedTest, SetFamilyRoundTrip) {
    SetMessage original;
    original.ordered = {3, 1, 2, 1};                       // 去重 → {1,2,3}
    original.hashed = {10, 20, 30};
    original.multi = {"a", "b", "a"};                      // multiset 保重复
    original.hashed_multi = {7, 7, 9};                     // 无序多重保重复

    CMString serialized;
    FLY_ENCODE(original, serialized);
    SetMessage decoded;
    FLY_DECODE(serialized, SetMessage, decoded);

    EXPECT_EQ(decoded.ordered.size(), 3u);
    EXPECT_TRUE(decoded.ordered.contains(1));
    EXPECT_TRUE(decoded.ordered.contains(3));
    EXPECT_EQ(decoded.hashed.size(), 3u);
    EXPECT_TRUE(decoded.hashed.contains(20));
    EXPECT_EQ(decoded.multi.size(), 3u);
    EXPECT_EQ(decoded.multi.count("a"), 2u);
    EXPECT_EQ(decoded.hashed_multi.size(), 3u);            // review 2026-09-13
    EXPECT_EQ(decoded.hashed_multi.count(7), 2u);          // 重复键保留
    EXPECT_EQ(decoded.hashed_multi.count(9), 1u);
}

TEST(SerializationExtendedTest, EmptySetRoundTrip) {
    SetMessage original;  // 全空
    CMString serialized;
    FLY_ENCODE(original, serialized);
    SetMessage decoded;
    FLY_DECODE(serialized, SetMessage, decoded);
    EXPECT_TRUE(decoded.ordered.empty());
    EXPECT_TRUE(decoded.hashed.empty());
    EXPECT_TRUE(decoded.multi.empty());
    EXPECT_TRUE(decoded.hashed_multi.empty());
}

TEST(SerializationExtendedTest, UnorderedSetRoundTrip) {
    // pg 集场景同型：unordered_set<uint64_t> 独立字段往返 + 空集
    PgSetMsg original;
    original.ids = {7, 9, 11};
    CMString serialized;
    FLY_ENCODE(original, serialized);
    PgSetMsg decoded;
    FLY_DECODE(serialized, PgSetMsg, decoded);
    ASSERT_EQ(decoded.ids.size(), 3u);
    EXPECT_TRUE(decoded.ids.contains(7));
    EXPECT_TRUE(decoded.ids.contains(9));
    EXPECT_TRUE(decoded.ids.contains(11));

    PgSetMsg empty;
    CMString empty_blob;
    FLY_ENCODE(empty, empty_blob);
    PgSetMsg empty_back;
    FLY_DECODE(empty_blob, PgSetMsg, empty_back);
    EXPECT_TRUE(empty_back.ids.empty());
}

struct CompositeHolder {
    std::optional<int32_t> opt_val;
    std::optional<CMString> opt_str;
    std::variant<int32_t, CMString, CMVector<int32_t>> variant_val;
    std::pair<int32_t, CMString> pair_val;
    std::tuple<int32_t, CMString, double> tuple_val;
    FLY_SERIALIZE(opt_val, opt_str, variant_val, pair_val, tuple_val)
};

// 元素级复合容器（vector<pair> / vector<optional>——elem 递归分派形态；
// 此前 s.object 兜底对无 serialize 元素会 static_assert）
struct PairVecMsg {
    CMVector<std::pair<uint64_t, int32_t>> pairs;
    CMVector<std::optional<int32_t>> opts;
    FLY_SERIALIZE(pairs, opts)
};

TEST(SerializationExtendedTest, OptionalVariantPairTupleRoundTrip) {
    CompositeHolder original;
    original.opt_val = 42;
    original.opt_str = "opt";
    original.variant_val = CMVector<int32_t>{5, 6};   // 第三形态（容器臂）
    original.pair_val = {7, "p"};
    original.tuple_val = {8, "t", 2.5};

    CMString serialized;
    FLY_ENCODE(original, serialized);
    CompositeHolder decoded;
    FLY_DECODE(serialized, CompositeHolder, decoded);

    ASSERT_TRUE(decoded.opt_val.has_value());
    EXPECT_EQ(*decoded.opt_val, 42);
    ASSERT_TRUE(decoded.opt_str.has_value());
    EXPECT_EQ(*decoded.opt_str, "opt");
    ASSERT_TRUE(std::holds_alternative<CMVector<int32_t>>(decoded.variant_val));
    EXPECT_EQ(std::get<CMVector<int32_t>>(decoded.variant_val)[1], 6);
    EXPECT_EQ(decoded.pair_val.first, 7);
    EXPECT_EQ(decoded.pair_val.second, "p");
    EXPECT_EQ(std::get<0>(decoded.tuple_val), 8);
    EXPECT_EQ(std::get<1>(decoded.tuple_val), "t");
    EXPECT_DOUBLE_EQ(std::get<2>(decoded.tuple_val), 2.5);
}

TEST(SerializationExtendedTest, OptionalEmptyAndVariantArmsRoundTrip) {
    // 边界：nullopt + variant 各臂形态（int 臂 / string 臂）
    CompositeHolder original;
    original.variant_val = 99;
    CMString serialized;
    FLY_ENCODE(original, serialized);
    CompositeHolder decoded;
    FLY_DECODE(serialized, CompositeHolder, decoded);
    EXPECT_FALSE(decoded.opt_val.has_value());
    EXPECT_FALSE(decoded.opt_str.has_value());
    ASSERT_TRUE(std::holds_alternative<int32_t>(decoded.variant_val));
    EXPECT_EQ(std::get<int32_t>(decoded.variant_val), 99);

    CompositeHolder str_arm;
    str_arm.variant_val = CMString("arm");
    CMString blob2;
    FLY_ENCODE(str_arm, blob2);
    CompositeHolder decoded2;
    FLY_DECODE(blob2, CompositeHolder, decoded2);
    ASSERT_TRUE(std::holds_alternative<CMString>(decoded2.variant_val));
    EXPECT_EQ(std::get<CMString>(decoded2.variant_val), "arm");
}

struct SequenceHolder {
    std::array<int32_t, 4> fixed;
    CMList<int32_t> list_val;
    std::forward_list<CMString> flist_val;
    CMDeque<int32_t> deque_val;
    FLY_SERIALIZE(fixed, list_val, flist_val, deque_val)
};

TEST(SerializationExtendedTest, ArrayAndListFamilyRoundTrip) {
    SequenceHolder original;
    original.fixed = {1, 2, 3, 4};
    original.list_val = {10, 20};
    original.flist_val = {"x", "y"};
    original.deque_val = {30, 40, 50};

    CMString serialized;
    FLY_ENCODE(original, serialized);
    SequenceHolder decoded;
    FLY_DECODE(serialized, SequenceHolder, decoded);

    EXPECT_EQ(decoded.fixed[2], 3);
    ASSERT_EQ(decoded.list_val.size(), 2u);
    EXPECT_EQ(decoded.list_val.back(), 20);
    CMString fl_front = decoded.flist_val.front();
    EXPECT_EQ(fl_front, "x");
    ASSERT_EQ(decoded.deque_val.size(), 3u);
    EXPECT_EQ(decoded.deque_val[0], 30);

    // 空容器边界（array 定长不空——其余族空往返）
    SequenceHolder empty;
    empty.fixed = {0, 0, 0, 0};
    CMString blob;
    FLY_ENCODE(empty, blob);
    SequenceHolder empty_back;
    FLY_DECODE(blob, SequenceHolder, empty_back);
    EXPECT_TRUE(empty_back.list_val.empty());
    EXPECT_TRUE(empty_back.deque_val.empty());
}

struct AdapterHolder {
    CMQueue<int32_t> queue_val;
    std::priority_queue<int32_t> pqueue_val;
    CMStack<CMString> stack_val;
    FLY_SERIALIZE(queue_val, pqueue_val, stack_val)
};

TEST(SerializationExtendedTest, QueueStackRoundTrip) {
    AdapterHolder original;
    original.queue_val.push(1);
    original.queue_val.push(2);
    original.pqueue_val.push(5);
    original.pqueue_val.push(9);   // 堆顶 9
    original.stack_val.push("s1");
    original.stack_val.push("s2"); // 栈顶 s2

    CMString serialized;
    FLY_ENCODE(original, serialized);
    AdapterHolder decoded;
    FLY_DECODE(serialized, AdapterHolder, decoded);

    ASSERT_EQ(decoded.queue_val.size(), 2u);
    EXPECT_EQ(decoded.queue_val.front(), 1);   // FIFO 序保持
    EXPECT_EQ(decoded.queue_val.back(), 2);
    ASSERT_EQ(decoded.pqueue_val.size(), 2u);
    EXPECT_EQ(decoded.pqueue_val.top(), 9);
    ASSERT_EQ(decoded.stack_val.size(), 2u);
    EXPECT_EQ(decoded.stack_val.top(), "s2");  // LIFO 序保持

    // 空容器边界
    AdapterHolder empty;
    CMString blob;
    FLY_ENCODE(empty, blob);
    AdapterHolder empty_back;
    FLY_DECODE(blob, AdapterHolder, empty_back);
    EXPECT_TRUE(empty_back.queue_val.empty());
    EXPECT_TRUE(empty_back.stack_val.empty());
}

struct ChronoBitsetHolder {
    std::chrono::milliseconds duration_val;
    std::chrono::system_clock::time_point time_val;
    std::bitset<10> bits;
    FLY_SERIALIZE(duration_val, time_val, bits)
};

TEST(SerializationExtendedTest, ChronoBitsetRoundTrip) {
    ChronoBitsetHolder original;
    original.duration_val = std::chrono::milliseconds{1234};
    original.time_val = std::chrono::system_clock::time_point{
        std::chrono::milliseconds{987654321}};
    original.bits = std::bitset<10>(std::string("1010101010"));

    CMString serialized;
    FLY_ENCODE(original, serialized);
    ChronoBitsetHolder decoded;
    FLY_DECODE(serialized, ChronoBitsetHolder, decoded);

    EXPECT_EQ(decoded.duration_val.count(), 1234);
    // time_point 以系统时钟原生 duration 序列化（Linux = 纳秒计数）——
    // 断言按构造同口径换算回毫秒
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(
                  decoded.time_val.time_since_epoch()).count(),
              987654321);
    EXPECT_EQ(decoded.bits.to_string(), "1010101010");
}

TEST(SerializationExtendedTest, AtomicElemDirectRoundTrip) {
    // std::atomic：成员级往返经 fly_ser::elem 直测（含 atomic 成员的
    // holder 结构不可 move-assign，FLY_DECODE 的 std::move 出参不可用
    // ——atomic 的分派正确性以元素级往返锁定）
    std::atomic<int32_t> original{42};
    FlySerBuf buf;
    {
        bitsery::Serializer<FlyOutputAdapter> ser{buf};
        fly_ser::elem(ser, original);
        ser.adapter().flush();
    }
    std::atomic<int32_t> decoded{0};
    {
        bitsery::Deserializer<FlyInputAdapter> des{buf.begin(), buf.size()};
        fly_ser::elem(des, decoded);
    }
    EXPECT_EQ(decoded.load(), 42);
}

TEST(SerializationExtendedTest, CompositeElementsInsideContainers) {
    PairVecMsg original;
    original.pairs = {{1, 2}, {3, 4}};
    original.opts = {5, std::nullopt, 7};

    CMString serialized;
    FLY_ENCODE(original, serialized);
    PairVecMsg decoded;
    FLY_DECODE(serialized, PairVecMsg, decoded);

    ASSERT_EQ(decoded.pairs.size(), 2u);
    EXPECT_EQ(decoded.pairs[0].first, 1);
    EXPECT_EQ(decoded.pairs[1].second, 4);
    ASSERT_EQ(decoded.opts.size(), 3u);
    EXPECT_EQ(*decoded.opts[0], 5);
    EXPECT_FALSE(decoded.opts[1].has_value());
    EXPECT_EQ(*decoded.opts[2], 7);
}

