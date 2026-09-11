// CM_FLAGS 宏单测：位独立性 / is-set-reset 语义 / reset_flags 清空 /
// 位号 = 参数序号 / 底层类型替换（int/uint32_t/uint64_t 三实例化，
// uint8_t 恰满 8 位作边界正向编译期验证）/ 与 CM_PROPERTY 共存。
// 越界（flag 数 > 底层类型位数）为编译期失败，失败样例以惯用法注释
// 保留在文件尾（cc_test 无法表达期望编译失败的正测试）。
#include <common/types/cpp/flags_macro.h>
#include <common/types/cpp/property_macro.h>

#include <gtest/gtest.h>

#include <container/cpp/container_aliases.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using namespace fly;  // CMString

// —— 三种底层类型实例化（flag 集对齐 design-db-phase2-plan ㉗ 的 cell
//    flags 集：fake_cell/std_cell/lef_cell/lib_cell/macro_cell/block_cell）——
class IntFlags {
public:
    CM_FLAGS(int, fake_cell, std_cell, lef_cell, lib_cell, macro_cell,
             block_cell)
};

class U32Flags {
public:
    CM_FLAGS(uint32_t, fake_cell, std_cell, lef_cell)
};

class U64Flags {
public:
    CM_FLAGS(uint64_t, routing, cut)
};

// uint8_t 底层 + 恰好 8 个 flag：边界可用性编译期验证——上限校验为
// static_assert(位数 >= flag 数)，若误写为 > 此实例化即编译失败
class U8FullFlags {
public:
    CM_FLAGS(uint8_t, b0, b1, b2, b3, b4, b5, b6, b7)
};

// —— 与 CM_PROPERTY 共存：flags_ 再经 CM_PROPERTY(flags) 暴露五件套 ——
class PropertyCoexist {
public:
    CMString name_;
    CM_FLAGS(uint32_t, fake_cell, std_cell)

    CM_PROPERTY(name)
    CM_PROPERTY(flags)
};

// —— 编译期验证：成员类型跟随底层类型 ——
static_assert(std::is_same_v<decltype(std::declval<IntFlags&>().flags_), int>,
              "flags_ 类型必须等于底层类型");
static_assert(
    std::is_same_v<decltype(std::declval<U32Flags&>().flags_), uint32_t>,
    "flags_ 类型必须等于底层类型");
static_assert(
    std::is_same_v<decltype(std::declval<U64Flags&>().flags_), uint64_t>,
    "flags_ 类型必须等于底层类型");
static_assert(
    std::is_same_v<decltype(std::declval<U8FullFlags&>().flags_), uint8_t>,
    "flags_ 类型必须等于底层类型");

TEST(CMFlagsTest, BitsIndependent) {
    // 置位一个 flag 不影响其他位：flags_ 精确等于置位位集
    IntFlags f;
    f.set_lef_cell();  // 第 2 个参数 → 位 2
    EXPECT_TRUE(f.is_lef_cell());
    EXPECT_FALSE(f.is_fake_cell());
    EXPECT_FALSE(f.is_std_cell());
    EXPECT_FALSE(f.is_lib_cell());
    EXPECT_FALSE(f.is_macro_cell());
    EXPECT_FALSE(f.is_block_cell());
    EXPECT_EQ(f.flags_, 0b000100);

    f.set_block_cell();  // 位 5，与位 2 并存
    EXPECT_TRUE(f.is_lef_cell());
    EXPECT_TRUE(f.is_block_cell());
    EXPECT_FALSE(f.is_fake_cell());
    EXPECT_FALSE(f.is_std_cell());
    EXPECT_EQ(f.flags_, 0b100100);
}

TEST(CMFlagsTest, IsSetResetSemantics) {
    // 默认全空；set 置位 is 为真；reset 单位仅清该位、其余位保持；
    // reset 未置位的位 = no-op
    U32Flags f;
    EXPECT_EQ(f.flags_, 0u);
    EXPECT_FALSE(f.is_fake_cell());
    EXPECT_FALSE(f.is_std_cell());
    EXPECT_FALSE(f.is_lef_cell());

    f.set_std_cell();
    EXPECT_TRUE(f.is_std_cell());
    EXPECT_EQ(f.flags_, 0b10u);

    f.set_lef_cell();
    f.reset_std_cell();
    EXPECT_FALSE(f.is_std_cell());
    EXPECT_TRUE(f.is_lef_cell());
    EXPECT_EQ(f.flags_, 0b100u);

    f.reset_fake_cell();
    EXPECT_EQ(f.flags_, 0b100u);
}

TEST(CMFlagsTest, ResetFlagsClearsAll) {
    // reset_flags 一次清空全部位，之后可重新置位
    IntFlags f;
    f.set_fake_cell();
    f.set_lib_cell();
    f.set_block_cell();
    EXPECT_NE(f.flags_, 0);

    f.reset_flags();
    EXPECT_EQ(f.flags_, 0);
    EXPECT_FALSE(f.is_fake_cell());
    EXPECT_FALSE(f.is_lib_cell());
    EXPECT_FALSE(f.is_block_cell());

    f.set_macro_cell();
    EXPECT_TRUE(f.is_macro_cell());
    EXPECT_EQ(f.flags_, 0b10000);
}

TEST(CMFlagsTest, UnderlyingTypeSubstitution) {
    // int/uint32_t/uint64_t 三实例化同一语义（成员类型断言见文件头）
    U64Flags f;
    f.set_cut();  // 位 1
    EXPECT_TRUE(f.is_cut());
    EXPECT_FALSE(f.is_routing());
    EXPECT_EQ(f.flags_, 0b10u);
    f.reset_cut();
    EXPECT_EQ(f.flags_, 0u);
    EXPECT_FALSE(f.is_cut());

    U8FullFlags g;
    g.set_b0();
    g.set_b3();
    g.set_b7();  // 8 位底层的高位（1 << 7）可正常表达
    EXPECT_TRUE(g.is_b0());
    EXPECT_TRUE(g.is_b3());
    EXPECT_FALSE(g.is_b1());
    EXPECT_TRUE(g.is_b7());
    EXPECT_EQ(g.flags_, 0x89);  // 0b10001001
}

TEST(CMFlagsTest, CoexistsWithCmProperty) {
    // flags_ 经 CM_PROPERTY(flags) 暴露后，属性五件套与 flag 三件套
    // 操作同一存储，展开互不冲突
    PropertyCoexist p;
    p.set_name("cell1");

    p.set_flags(static_cast<uint32_t>(0b11));
    EXPECT_TRUE(p.is_fake_cell());
    EXPECT_TRUE(p.is_std_cell());
    EXPECT_EQ(p.get_flags(), 0b11u);

    p.set_std_cell();  // 已置位，幂等
    EXPECT_EQ(p.get_flags(), 0b11u);
    p.reset_fake_cell();
    EXPECT_EQ(p.get_flags(), 0b10u);
    EXPECT_TRUE(p.is_std_cell());
    EXPECT_FALSE(p.is_fake_cell());

    // CM_PROPERTY 其余字段不受 flags 展开影响
    EXPECT_EQ(p.get_name(), "cell1");
}

// —— 编译期失败样例（惯用法：以注释保留，不参与构建）——
// flag 数（9）超过底层类型位数（8）时上限校验 static_assert 编译报错：
//
//   class U8OverflowFlags {
//   public:
//       CM_FLAGS(uint8_t, b0, b1, b2, b3, b4, b5, b6, b7, b8)
//   };
//
// 报错信息：CM_FLAGS: flag count exceeds underlying type bit width
// （uint32_t 上限 32 个、uint64_t 上限 64 个，越界同理由）

}  // namespace
