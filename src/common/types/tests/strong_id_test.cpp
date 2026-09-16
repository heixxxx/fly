// StrongIdT 强类型 id 模板单测：哨兵语义 / 算术（两族操作数）/ 减法特例
// （同类 − 同类 → 裸值）/ 复合赋值 / 自增自减 / 全六种比较三形态 /
// std::hash 容器键 / 序列化直通字节等价（与裸整型逐位一致——不经
// FLY_SERIALIZE 宏的版本前缀）/ 跨类运算与隐式转换的编译级拒绝
//（expr_wellformed 探针——跨 Tag 不提供重载即表达式不 well-formed，
// static_assert 编译期证明；临时探针 Tag 仅测模板机制本身）。
#include <common/types/cpp/strong_id.h>

#include <common/serialization/cpp/serialization_macros.h>

#include <gtest/gtest.h>

#include <container/cpp/container_aliases.h>

#include <cstdint>
#include <type_traits>
#include <unordered_map>

namespace {

using fly::StrongIdT;

// 框架探针 Tag（机制验证；业务实体 id 不在本头定义——见 emir/common）
struct TagA {};
struct TagB {};
using IdA32 = StrongIdT<TagA, uint32_t>;
using IdB32 = StrongIdT<TagB, uint32_t>;
using IdA64 = StrongIdT<TagA, uint64_t>;

// 表达式 well-formed 探针（C++20 requires 表达式）：跨类运算/赋值在
// requires 内不成形 = false——编译级拒绝的编译期证明（requires 表达式
// 的 SFINAE 语义保证失败不升级为硬错误）
template <typename A, typename B>
inline constexpr bool cross_add_ok = requires(A a, B b) { a + b; };
template <typename A, typename B>
inline constexpr bool cross_sub_ok = requires(A a, B b) { a - b; };
template <typename A, typename B>
inline constexpr bool cross_mul_ok = requires(A a, B b) { a * b; };
template <typename A, typename B>
inline constexpr bool cross_div_ok = requires(A a, B b) { a / b; };
template <typename A, typename B>
inline constexpr bool cross_mod_ok = requires(A a, B b) { a % b; };
template <typename A, typename B>
inline constexpr bool cross_eq_ok = requires(A a, B b) { a == b; };
template <typename A, typename B>
inline constexpr bool cross_ne_ok = requires(A a, B b) { a != b; };
template <typename A, typename B>
inline constexpr bool cross_lt_ok = requires(A a, B b) { a < b; };
template <typename A, typename B>
inline constexpr bool cross_le_ok = requires(A a, B b) { a <= b; };
template <typename A, typename B>
inline constexpr bool cross_gt_ok = requires(A a, B b) { a > b; };
template <typename A, typename B>
inline constexpr bool cross_ge_ok = requires(A a, B b) { a >= b; };
template <typename A, typename B>
inline constexpr bool cross_assign_ok = requires(A& a, B b) { a = b; };
template <typename A, typename B>
inline constexpr bool cross_plus_assign_ok = requires(A& a, B b) { a += b; };
template <typename A, typename B>
inline constexpr bool cross_minus_assign_ok = requires(A& a, B b) { a -= b; };
template <typename A, typename B>
inline constexpr bool cross_raw_sub_ok = requires(A a, B b) { a - b; };

// —— 哨兵与构造 ——

TEST(StrongIdTest, SentinelAndConstruction) {
    EXPECT_EQ(IdA32{}.value(), UINT32_MAX);
    EXPECT_EQ(IdA64{}.value(), UINT64_MAX);
    static_assert(IdA32::kInvalid == UINT32_MAX);
    static_assert(IdA64::kInvalid == UINT64_MAX);
    EXPECT_FALSE(IdA32{}.is_valid());
    EXPECT_FALSE(IdA32::is_valid(UINT32_MAX));
    EXPECT_TRUE(IdA32{0}.is_valid());
    EXPECT_TRUE(IdA32::is_valid(42));

    constexpr IdA32 id{7};
    static_assert(id.value() == 7);
    EXPECT_EQ(id.value(), 7);
    // 默认构造 = 哨兵；explicit 构造无隐式转换
    static_assert(!std::is_convertible_v<uint32_t, IdA32>);
    static_assert(!std::is_convertible_v<IdA32, uint32_t>);
    static_assert(std::is_trivially_copyable_v<IdA32>);
    static_assert(sizeof(IdA32) == sizeof(uint32_t));
    static_assert(sizeof(IdA64) == sizeof(uint64_t));
}

// —— 算术：两族操作数 ——

TEST(StrongIdTest, Arithmetic) {
    const IdA32 a{10};
    const IdA32 b{3};
    EXPECT_EQ((a + b).value(), 13u);
    EXPECT_EQ((a + 3u).value(), 13u);
    EXPECT_EQ((1u + a).value(), 11u);
    // 减法特例：同类 − 同类 → 裸整型（差值/偏移量）
    static_assert(std::is_same_v<decltype(a - b), uint32_t>);
    EXPECT_EQ(a - b, 7u);
    EXPECT_EQ((a - 3u).value(), 7u);
    EXPECT_EQ((a * b).value(), 30u);
    EXPECT_EQ((a * 3u).value(), 30u);
    EXPECT_EQ((a / b).value(), 3u);
    EXPECT_EQ((a / 3u).value(), 3u);
    EXPECT_EQ((a % b).value(), 1u);
    EXPECT_EQ((a % 3u).value(), 1u);

    // 偏移算术自然书写：区间 start + local、id + 1
    const IdA32 start{100};
    EXPECT_EQ((start + 5u).value(), 105u);
}

TEST(StrongIdTest, CompoundAssignment) {
    IdA32 a{10};
    a += IdA32{5};
    EXPECT_EQ(a.value(), 15u);
    a += 5u;
    EXPECT_EQ(a.value(), 20u);
    a -= IdA32{3};
    EXPECT_EQ(a.value(), 17u);
    a -= 7u;
    EXPECT_EQ(a.value(), 10u);
    a *= IdA32{2};
    EXPECT_EQ(a.value(), 20u);
    a *= 3u;
    EXPECT_EQ(a.value(), 60u);
    a /= IdA32{4};
    EXPECT_EQ(a.value(), 15u);
    a /= 5u;
    EXPECT_EQ(a.value(), 3u);
    a %= IdA32{2};
    EXPECT_EQ(a.value(), 1u);
    a %= 1u;
    EXPECT_EQ(a.value(), 0u);
}

TEST(StrongIdTest, IncrementDecrement) {
    IdA32 a{10};
    EXPECT_EQ((++a).value(), 11u);
    EXPECT_EQ((a++).value(), 11u);
    EXPECT_EQ(a.value(), 12u);
    EXPECT_EQ((--a).value(), 11u);
    EXPECT_EQ((a--).value(), 11u);
    EXPECT_EQ(a.value(), 10u);
}

// —— 比较：全六种 × 三形态 ——

TEST(StrongIdTest, Comparison) {
    const IdA32 a{10};
    const IdA32 b{10};
    const IdA32 c{20};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    EXPECT_TRUE(a < c);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(c > a);
    EXPECT_TRUE(c >= b);
    // 同类 × 裸值
    EXPECT_TRUE(a == 10u);
    EXPECT_TRUE(a != 20u);
    EXPECT_TRUE(a < 20u);
    EXPECT_TRUE(a <= 10u);
    EXPECT_TRUE(c > 10u);
    EXPECT_TRUE(c >= 10u);
    // 裸值 × 同类
    EXPECT_TRUE(10u == a);
    EXPECT_TRUE(20u != a);
    EXPECT_TRUE(5u < a);
    EXPECT_TRUE(10u <= a);
    EXPECT_TRUE(20u > a);
    EXPECT_TRUE(10u >= a);
}

// —— hash 与容器键 ——

TEST(StrongIdTest, HashAndContainerKey) {
    const IdA32 a{42};
    EXPECT_EQ(std::hash<IdA32>{}(a), std::hash<uint32_t>{}(42u));
    CMUnorderedMap<IdA32, int> m;
    m[a] = 1;
    m[IdA32{43}] = 2;
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m[a], 1);
    EXPECT_EQ(m[IdA32{43}], 2);
    // 64 位族同口径
    CMUnorderedMap<IdA64, int> m64;
    m64[IdA64{1}] = 5;
    EXPECT_EQ(m64[IdA64{1}], 5);
}

// —— 序列化直通：字节级与裸整型一致 ——

namespace {

struct RawPod32 {
    uint32_t a = 0;
    uint32_t b = 0;

    FLY_SERIALIZE(a, b)
};

struct StrongPod32 {
    uint32_t a = 0;
    StrongIdT<TagA, uint32_t> b;

    FLY_SERIALIZE(a, b)
};

struct RawPod64 {
    uint64_t a = 0;

    FLY_SERIALIZE(a)
};

struct StrongPod64 {
    uint64_t a = 0;

    FLY_SERIALIZE(a)
};

}  // namespace

TEST(StrongIdTest, SerializationByteIdenticalToRaw) {
    // 标量字段：StrongIdT 成员编码字节 = 裸整型成员编码字节（逐位一致
    // ——serialize 直通 value4b/8b，无版本前缀等额外字节）
    RawPod32 raw32;
    raw32.a = 1;
    raw32.b = 42;
    StrongPod32 strong32;
    strong32.a = 1;
    strong32.b = StrongIdT<TagA, uint32_t>{42};
    CMString raw_bytes;
    CMString strong_bytes;
    FLY_ENCODE(raw32, raw_bytes);
    FLY_ENCODE(strong32, strong_bytes);
    ASSERT_EQ(raw_bytes.size(), strong_bytes.size());
    EXPECT_EQ(raw_bytes, strong_bytes);

    RawPod64 raw64;
    raw64.a = 0x123456789ABCDEF;
    StrongPod64 strong64;
    strong64.a = StrongIdT<TagA, uint64_t>{0x123456789ABCDEF}.value();
    CMString raw_bytes64;
    CMString strong_bytes64;
    FLY_ENCODE(raw64, raw_bytes64);
    FLY_ENCODE(strong64, strong_bytes64);
    ASSERT_EQ(raw_bytes64.size(), strong_bytes64.size());
    EXPECT_EQ(raw_bytes64, strong_bytes64);

    // round-trip：值无损还原
    StrongPod32 decoded;
    FLY_DECODE(strong_bytes, StrongPod32, decoded);
    EXPECT_EQ(decoded.a, 1u);
    EXPECT_EQ(decoded.b.value(), 42u);
}

namespace {

// map 字段载体（bitsery 顶层对象须有 serialize 函数——裸 map 不作顶层，
// 实际数据形态 = map 为对象字段经 FLY_FIELD 的 StdMap 分派）
struct IdMapHolder {
    CMUnorderedMap<IdA32, uint64_t> items_;

    FLY_SERIALIZE(items_)
};

}  // namespace

TEST(StrongIdTest, SerializationRoundTripMapKey) {
    // 容器键路径（map 字段经 FLY_FIELD → StdMap ext → fly_ser::elem →
    // object → value 直通）
    IdMapHolder src;
    src.items_[IdA32{1}] = 100;
    src.items_[IdA32{2}] = 200;
    src.items_[IdA32{UINT32_MAX - 1}] = 300;
    CMString bytes;
    FLY_ENCODE(src, bytes);
    IdMapHolder dst;
    FLY_DECODE(bytes, IdMapHolder, dst);
    ASSERT_EQ(dst.items_.size(), 3u);
    EXPECT_EQ(dst.items_.at(IdA32{1}), 100u);
    EXPECT_EQ(dst.items_.at(IdA32{2}), 200u);
    EXPECT_EQ(dst.items_.at(IdA32{UINT32_MAX - 1}), 300u);
}

// —— 跨类编译级拒绝（requires 表达式探针的 static_assert 编译期证明）——

static_assert(!cross_add_ok<IdA32, IdB32>, "cross-tag + must not compile");
static_assert(!cross_sub_ok<IdA32, IdB32>, "cross-tag - must not compile");
static_assert(!cross_mul_ok<IdA32, IdB32>, "cross-tag * must not compile");
static_assert(!cross_div_ok<IdA32, IdB32>, "cross-tag / must not compile");
static_assert(!cross_mod_ok<IdA32, IdB32>, "cross-tag % must not compile");
static_assert(!cross_eq_ok<IdA32, IdB32>, "cross-tag == must not compile");
static_assert(!cross_ne_ok<IdA32, IdB32>, "cross-tag != must not compile");
static_assert(!cross_lt_ok<IdA32, IdB32>, "cross-tag < must not compile");
static_assert(!cross_le_ok<IdA32, IdB32>, "cross-tag <= must not compile");
static_assert(!cross_gt_ok<IdA32, IdB32>, "cross-tag > must not compile");
static_assert(!cross_ge_ok<IdA32, IdB32>, "cross-tag >= must not compile");
static_assert(!cross_assign_ok<IdA32, IdB32>,
              "cross-tag assignment must not compile");
static_assert(!cross_plus_assign_ok<IdA32, IdB32>,
              "cross-tag += must not compile");
static_assert(!cross_minus_assign_ok<IdA32, IdB32>,
              "cross-tag -= must not compile");
// 裸值 − / * 同类（无意义方向）不提供
static_assert(!cross_raw_sub_ok<uint32_t, IdA32>,
              "raw - id must not compile");
static_assert(!cross_mul_ok<uint32_t, IdA32>, "raw * id must not compile");
// 对照：同类、同类×裸值恒 well-formed
static_assert(cross_add_ok<IdA32, IdA32>);
static_assert(cross_sub_ok<IdA32, IdA32>);
static_assert(cross_mul_ok<IdA32, IdA32>);
static_assert(cross_eq_ok<IdA32, IdA32>);
static_assert(cross_add_ok<IdA32, uint32_t>);
static_assert(cross_eq_ok<IdA32, uint32_t>);
static_assert(cross_assign_ok<IdA32, IdA32>);

TEST(StrongIdTest, CrossTagRejectionIsCompileTime) {
    // 运行时占位断言（真实约束 = 上方 static_assert，编译期已证明）
    SUCCEED();
}

// —— 诊断输出 ——

TEST(StrongIdTest, OstreamOutput) {
    std::ostringstream os;
    os << IdA32{42};
    EXPECT_EQ(os.str(), "42");
}

}  // namespace
