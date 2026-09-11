// CM_PROPERTY 宏单测：五接口语义（值拷贝隔离 / get_ref 原地修改 /
// const 对象读取 / set 拷贝源完整 / set_move 非 SSO 缓冲区指针零拷贝
// 转移）+ 返回类型 static_assert 编译期验证。
#include <common/types/cpp/property_macro.h>

#include <gtest/gtest.h>

#include <container/cpp/container_aliases.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using namespace fly;  // CMString / CMVector

// 被测样例类：成员名 = 属性名 + '_'（CM_PROPERTY 的硬约束）
class Holder {
public:
    CMString name_;
    CMVector<int32_t> vec_;

    CM_PROPERTY(name)
    CM_PROPERTY(vec)
};

// —— 编译期返回类型验证 ——
static_assert(std::is_same_v<decltype(std::declval<const Holder&>().get_name()), CMString>,
              "get_* 必须返回值拷贝");
static_assert(std::is_same_v<decltype(std::declval<Holder&>().get_ref_name()), CMString&>,
              "get_ref_* 必须返回可变引用 T&");
static_assert(std::is_same_v<decltype(std::declval<const Holder&>().get_cref_name()),
                             const CMString&>,
              "get_cref_* 必须返回只读引用 const T&");

TEST(PropertyMacroTest, GetValueCopyIsolation) {
    // get 返回值拷贝：修改返回拷贝不影响成员
    Holder h;
    h.set_name("original");

    CMString copy = h.get_name();
    copy += " mutated";

    EXPECT_EQ(copy, "original mutated");
    EXPECT_EQ(h.get_cref_name(), "original");
}

TEST(PropertyMacroTest, GetRefInPlaceModify) {
    // get_ref 返回可变引用：原地修改直接作用于成员
    Holder h;
    h.set_name("first");

    h.get_ref_name() = "second";
    EXPECT_EQ(h.get_name(), "second");

    h.get_ref_vec().push_back(7);
    h.get_ref_vec().push_back(8);
    ASSERT_EQ(h.get_vec().size(), 2u);
    EXPECT_EQ(h.get_vec()[0], 7);
    EXPECT_EQ(h.get_vec()[1], 8);
}

TEST(PropertyMacroTest, ConstObjectAccess) {
    // const 对象上 get（值拷贝）与 get_cref（只读引用）均可用
    Holder h;
    h.set_name("const_read");
    h.get_ref_vec().push_back(42);

    const Holder& ch = h;
    EXPECT_EQ(ch.get_name(), "const_read");
    EXPECT_EQ(ch.get_cref_name(), "const_read");
    EXPECT_EQ(ch.get_cref_vec().size(), 1u);
    EXPECT_EQ(ch.get_cref_vec()[0], 42);
}

TEST(PropertyMacroTest, SetCopyKeepsSourceIntact) {
    // set 拷贝赋值：源对象保持完整，且之后修改源不影响成员
    Holder h;
    CMString src = "hello";

    h.set_name(src);
    EXPECT_EQ(src, "hello");
    EXPECT_EQ(h.get_name(), "hello");

    src += " world";
    EXPECT_EQ(src, "hello world");
    EXPECT_EQ(h.get_name(), "hello");
}

TEST(PropertyMacroTest, SetMoveTransfersLongStringBuffer) {
    // set_move：非 SSO 长字符串（>15 字符走堆缓冲）移动后目标缓冲区
    // 指针 == 源原指针（零拷贝转移），源变 empty
    Holder h;
    CMString src(64, 'x');
    ASSERT_GT(src.size(), 15u);
    const char* old_data = src.data();

    h.set_name_move(std::move(src));

    EXPECT_EQ(h.get_cref_name().data(), old_data);
    EXPECT_EQ(h.get_cref_name().size(), 64u);
    EXPECT_TRUE(src.empty());
}

TEST(PropertyMacroTest, SetMoveTransfersVectorBuffer) {
    // set_move 对 CMVector<int> 同样零拷贝转移：缓冲区指针不变、源变空
    Holder h;
    CMVector<int32_t> src(100, 42);
    const int32_t* old_data = src.data();

    h.set_vec_move(std::move(src));

    ASSERT_EQ(h.get_cref_vec().size(), 100u);
    EXPECT_EQ(h.get_cref_vec().data(), old_data);
    EXPECT_EQ(h.get_cref_vec()[99], 42);
    EXPECT_TRUE(src.empty());
}

}  // namespace
