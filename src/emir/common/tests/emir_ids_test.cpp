// EMIR 族实体强类型 id 实例化单测（src/emir/common/cpp/emir_ids.h，
// 2026-09-16 裁定的单一权威定义点）：八类实例化的哨兵/位宽/hash 键/跨
// 类拒绝（机制证明在框架层 strong_id_test——此处验业务实例本身的健全
// 形态与全类型可用性）。
#include <emir/common/cpp/emir_ids.h>

#include <gtest/gtest.h>

#include <container/cpp/container_aliases.h>

#include <cstdint>
#include <sstream>
#include <type_traits>

namespace {

using fly::CMCellId;
using fly::CMInstanceId;
using fly::CMLayerId;
using fly::CMNetId;
using fly::CMPartitionId;
using fly::CMPinId;
using fly::CMViaCellId;
using fly::CMViaInstanceId;

// —— 位宽分组与哨兵（32 位族十万级以内、64 位族 instance 可达 10⁹ 级）——

static_assert(sizeof(CMCellId) == 4 && sizeof(CMPinId) == 4 &&
              sizeof(CMViaCellId) == 4 && sizeof(CMLayerId) == 4 &&
              sizeof(CMPartitionId) == 4);
static_assert(sizeof(CMInstanceId) == 8 && sizeof(CMNetId) == 8 &&
              sizeof(CMViaInstanceId) == 8);

static_assert(CMCellId::kInvalid == UINT32_MAX);
static_assert(CMPinId::kInvalid == UINT32_MAX);
static_assert(CMViaCellId::kInvalid == UINT32_MAX);
static_assert(CMLayerId::kInvalid == UINT32_MAX);
static_assert(CMPartitionId::kInvalid == UINT32_MAX);
static_assert(CMInstanceId::kInvalid == UINT64_MAX);
static_assert(CMNetId::kInvalid == UINT64_MAX);
static_assert(CMViaInstanceId::kInvalid == UINT64_MAX);

static_assert(std::is_trivially_copyable_v<CMCellId>);
static_assert(!std::is_convertible_v<uint32_t, CMPinId>);
static_assert(!std::is_convertible_v<CMPinId, uint32_t>);

TEST(EmirIdsTest, SentinelAndDefaults) {
    EXPECT_FALSE(CMCellId{}.is_valid());
    EXPECT_FALSE(CMPinId{}.is_valid());
    EXPECT_FALSE(CMInstanceId{}.is_valid());
    EXPECT_TRUE(CMCellId{0}.is_valid());
    EXPECT_TRUE(CMInstanceId{0}.is_valid());
}

TEST(EmirIdsTest, ArithmeticAndComparison) {
    // 区间换算自然书写：start + local（instance 族）
    const CMInstanceId start{100};
    const CMInstanceId local{3};
    EXPECT_EQ((start + local).value(), 103u);
    EXPECT_EQ(start + local, CMInstanceId{103});
    // 差值特例：id − id → 裸值
    static_assert(std::is_same_v<decltype(start - local), uint64_t>);
    EXPECT_EQ(start - local, 97u);
    EXPECT_TRUE(CMPinId{5} < CMPinId{6});
    EXPECT_TRUE(CMPinId{5} == 5u);
}

TEST(EmirIdsTest, HashContainerKey) {
    CMUnorderedMap<CMPinId, CMString> pin_names;
    pin_names[CMPinId{1}] = "A";
    pin_names[CMPinId{2}] = "ZN";
    EXPECT_EQ(pin_names.size(), 2u);
    EXPECT_EQ(pin_names[CMPinId{1}], "A");
    CMUnorderedSet<CMNetId> nets;
    nets.insert(CMNetId{9});
    EXPECT_TRUE(nets.contains(CMNetId{9}));
}

TEST(EmirIdsTest, OstreamOutput) {
    std::ostringstream os;
    os << CMViaCellId{7};
    EXPECT_EQ(os.str(), "7");
}

}  // namespace
