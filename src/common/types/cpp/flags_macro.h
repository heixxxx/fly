#pragma once

// ── CM_FLAGS 位标志宏 ── common/types ────────────────────────────────
// 公共工具宏（与 CM_PROPERTY 同库同风格）：单个 flags_ 变量按位记录一
// 组 bool 标记，逐 flag 生成 is_<name>()/set_<name>()/reset_<name>() 三
// 件套 + 整体 reset_flags()，替代「一组独立 bool 成员」与手写位运算样板。
// design db 的 cell/pin 来源与种类标记（design-db-phase2-plan ㉗ 与 R5：
// fake_cell/std_cell/lef_cell/lib_cell/macro_cell/block_cell 等）以本宏
// 声明标记面。
//
// 用法（第 n 个 flag 参数的位号 = n，从 0 起，mask = UnderlyingT(1) << n；
// 底层类型可换 int/uint32_t/uint64_t 等）：
//   class DSCell {
//   public:
//       CM_FLAGS(uint32_t, fake_cell, std_cell, lef_cell)
//   };
//
// 生成接口清单（以 flag 名 fake_cell、序号 0 为例）：
//   bool is_fake_cell() const   读（按位与）
//   void set_fake_cell()        置位（其余位不变）
//   void reset_fake_cell()      清位（其余位不变）
//   void reset_flags()          清空全部位
// 另生成成员 UnderlyingT flags_ = 0。flag 名不得为 flags（与 reset_flags
// 冲突）；flags_ 可再经 CM_PROPERTY(flags) 暴露五件套访问器，两者展开
// 互不冲突。flag 数上限 64（覆盖 uint64_t 底层满位；数量超过底层类型
// 位数时 static_assert 编译期报错）。本宏在类定义中直接展开使用，不再
// 经另一层宏包装调用。
//
// 实现说明：逐参带索引展开为自持基建（参数计数 + 64 级顺序分发，见
// 下方 FOR_EACH 段），不依赖 Boost.PP——serialization_macros.h 的
// FLY_EACH 同源机制（BOOST_PP_SEQ_FOR_EACH）无法提供位号（无索引回
// 调），其带索引版本 SEQ_FOR_EACH_I 从自定义宏的展开文本中调用时需手
// 动匹配递归深度（_R 版本），对封装宏不可靠（实测多种深度均断裂）；
// serialization_macros.h 保持原样不改。

// —— 单 flag 展开（第 i 位；mask 现算，不生成中间常量，保持类内声明
//    面最小）——
#define CM_FLAGS_FLAG_I(underlying_t, name, i)                                 \
    bool is_##name() const {                                                   \
        return (flags_ & (static_cast<underlying_t>(1) << (i))) != 0;          \
    }                                                                          \
    void set_##name() {                                                        \
        flags_ = static_cast<underlying_t>(                                    \
            flags_ | (static_cast<underlying_t>(1) << (i)));                   \
    }                                                                          \
    void reset_##name() {                                                      \
        flags_ = static_cast<underlying_t>(                                    \
            flags_ & ~(static_cast<underlying_t>(1) << (i)));                  \
    }

// —— 参数计数（经典 GET_N 技法：__VA_ARGS__ 后接降序 64..1，第 65 个
//    形参 N 命中第 k+64 实参序列中的第 k 个 = k）——
#define CM_FLAGS_GET_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12,      \
                       _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23,  \
                       _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34,  \
                       _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45,  \
                       _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56,  \
                       _57, _58, _59, _60, _61, _62, _63, _64, N, ...)         \
    N
#define CM_FLAGS_NARG(...)                                                     \
    CM_FLAGS_GET_N(__VA_ARGS__, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54,    \
                   53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, \
                   38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, \
                   23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,  \
                   8, 7, 6, 5, 4, 3, 2, 1)

// —— 64 级顺序分发（第 n 级展开第 n 个参数为位号 n-1、其余转交前级；
//    上限 64 = uint64_t 底层满位）——
#define CM_FLAGS_CONCAT(a, b) CM_FLAGS_CONCAT_I(a, b)
#define CM_FLAGS_CONCAT_I(a, b) a##b
#define CM_FLAGS_FOR_EACH(N, t, ...)                                           \
    CM_FLAGS_CONCAT(CM_FLAGS_FOR_EACH_, N)(t, __VA_ARGS__)

#define CM_FLAGS_FOR_EACH_1(t, p1) CM_FLAGS_FLAG_I(t, p1, 0)
#define CM_FLAGS_FOR_EACH_2(t, p1, p2) CM_FLAGS_FOR_EACH_1(t, p1) CM_FLAGS_FLAG_I(t, p2, 1)
#define CM_FLAGS_FOR_EACH_3(t, p1, p2, p3) CM_FLAGS_FOR_EACH_2(t, p1, p2) CM_FLAGS_FLAG_I(t, p3, 2)
#define CM_FLAGS_FOR_EACH_4(t, p1, p2, p3, p4) CM_FLAGS_FOR_EACH_3(t, p1, p2, p3) CM_FLAGS_FLAG_I(t, p4, 3)
#define CM_FLAGS_FOR_EACH_5(t, p1, p2, p3, p4, p5) CM_FLAGS_FOR_EACH_4(t, p1, p2, p3, p4) CM_FLAGS_FLAG_I(t, p5, 4)
#define CM_FLAGS_FOR_EACH_6(t, p1, p2, p3, p4, p5, p6) CM_FLAGS_FOR_EACH_5(t, p1, p2, p3, p4, p5) CM_FLAGS_FLAG_I(t, p6, 5)
#define CM_FLAGS_FOR_EACH_7(t, p1, p2, p3, p4, p5, p6, p7) CM_FLAGS_FOR_EACH_6(t, p1, p2, p3, p4, p5, p6) CM_FLAGS_FLAG_I(t, p7, 6)
#define CM_FLAGS_FOR_EACH_8(t, p1, p2, p3, p4, p5, p6, p7, p8) CM_FLAGS_FOR_EACH_7(t, p1, p2, p3, p4, p5, p6, p7) CM_FLAGS_FLAG_I(t, p8, 7)
#define CM_FLAGS_FOR_EACH_9(t, p1, p2, p3, p4, p5, p6, p7, p8, p9) CM_FLAGS_FOR_EACH_8(t, p1, p2, p3, p4, p5, p6, p7, p8) CM_FLAGS_FLAG_I(t, p9, 8)
#define CM_FLAGS_FOR_EACH_10(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10) CM_FLAGS_FOR_EACH_9(t, p1, p2, p3, p4, p5, p6, p7, p8, p9) CM_FLAGS_FLAG_I(t, p10, 9)
#define CM_FLAGS_FOR_EACH_11(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11) CM_FLAGS_FOR_EACH_10(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10) CM_FLAGS_FLAG_I(t, p11, 10)
#define CM_FLAGS_FOR_EACH_12(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12) CM_FLAGS_FOR_EACH_11(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11) CM_FLAGS_FLAG_I(t, p12, 11)
#define CM_FLAGS_FOR_EACH_13(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13) CM_FLAGS_FOR_EACH_12(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12) CM_FLAGS_FLAG_I(t, p13, 12)
#define CM_FLAGS_FOR_EACH_14(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14) CM_FLAGS_FOR_EACH_13(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13) CM_FLAGS_FLAG_I(t, p14, 13)
#define CM_FLAGS_FOR_EACH_15(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15) CM_FLAGS_FOR_EACH_14(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14) CM_FLAGS_FLAG_I(t, p15, 14)
#define CM_FLAGS_FOR_EACH_16(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16) CM_FLAGS_FOR_EACH_15(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15) CM_FLAGS_FLAG_I(t, p16, 15)
#define CM_FLAGS_FOR_EACH_17(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17) CM_FLAGS_FOR_EACH_16(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16) CM_FLAGS_FLAG_I(t, p17, 16)
#define CM_FLAGS_FOR_EACH_18(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18) CM_FLAGS_FOR_EACH_17(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17) CM_FLAGS_FLAG_I(t, p18, 17)
#define CM_FLAGS_FOR_EACH_19(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19) CM_FLAGS_FOR_EACH_18(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18) CM_FLAGS_FLAG_I(t, p19, 18)
#define CM_FLAGS_FOR_EACH_20(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20) CM_FLAGS_FOR_EACH_19(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19) CM_FLAGS_FLAG_I(t, p20, 19)
#define CM_FLAGS_FOR_EACH_21(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21) CM_FLAGS_FOR_EACH_20(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20) CM_FLAGS_FLAG_I(t, p21, 20)
#define CM_FLAGS_FOR_EACH_22(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22) CM_FLAGS_FOR_EACH_21(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21) CM_FLAGS_FLAG_I(t, p22, 21)
#define CM_FLAGS_FOR_EACH_23(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23) CM_FLAGS_FOR_EACH_22(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22) CM_FLAGS_FLAG_I(t, p23, 22)
#define CM_FLAGS_FOR_EACH_24(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24) CM_FLAGS_FOR_EACH_23(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23) CM_FLAGS_FLAG_I(t, p24, 23)
#define CM_FLAGS_FOR_EACH_25(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25) CM_FLAGS_FOR_EACH_24(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24) CM_FLAGS_FLAG_I(t, p25, 24)
#define CM_FLAGS_FOR_EACH_26(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26) CM_FLAGS_FOR_EACH_25(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25) CM_FLAGS_FLAG_I(t, p26, 25)
#define CM_FLAGS_FOR_EACH_27(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27) CM_FLAGS_FOR_EACH_26(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26) CM_FLAGS_FLAG_I(t, p27, 26)
#define CM_FLAGS_FOR_EACH_28(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28) CM_FLAGS_FOR_EACH_27(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27) CM_FLAGS_FLAG_I(t, p28, 27)
#define CM_FLAGS_FOR_EACH_29(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29) CM_FLAGS_FOR_EACH_28(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28) CM_FLAGS_FLAG_I(t, p29, 28)
#define CM_FLAGS_FOR_EACH_30(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30) CM_FLAGS_FOR_EACH_29(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29) CM_FLAGS_FLAG_I(t, p30, 29)
#define CM_FLAGS_FOR_EACH_31(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31) CM_FLAGS_FOR_EACH_30(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30) CM_FLAGS_FLAG_I(t, p31, 30)
#define CM_FLAGS_FOR_EACH_32(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32) CM_FLAGS_FOR_EACH_31(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31) CM_FLAGS_FLAG_I(t, p32, 31)
#define CM_FLAGS_FOR_EACH_33(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33) CM_FLAGS_FOR_EACH_32(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32) CM_FLAGS_FLAG_I(t, p33, 32)
#define CM_FLAGS_FOR_EACH_34(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34) CM_FLAGS_FOR_EACH_33(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33) CM_FLAGS_FLAG_I(t, p34, 33)
#define CM_FLAGS_FOR_EACH_35(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35) CM_FLAGS_FOR_EACH_34(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34) CM_FLAGS_FLAG_I(t, p35, 34)
#define CM_FLAGS_FOR_EACH_36(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36) CM_FLAGS_FOR_EACH_35(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35) CM_FLAGS_FLAG_I(t, p36, 35)
#define CM_FLAGS_FOR_EACH_37(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37) CM_FLAGS_FOR_EACH_36(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36) CM_FLAGS_FLAG_I(t, p37, 36)
#define CM_FLAGS_FOR_EACH_38(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38) CM_FLAGS_FOR_EACH_37(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37) CM_FLAGS_FLAG_I(t, p38, 37)
#define CM_FLAGS_FOR_EACH_39(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39) CM_FLAGS_FOR_EACH_38(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38) CM_FLAGS_FLAG_I(t, p39, 38)
#define CM_FLAGS_FOR_EACH_40(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40) CM_FLAGS_FOR_EACH_39(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39) CM_FLAGS_FLAG_I(t, p40, 39)
#define CM_FLAGS_FOR_EACH_41(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41) CM_FLAGS_FOR_EACH_40(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40) CM_FLAGS_FLAG_I(t, p41, 40)
#define CM_FLAGS_FOR_EACH_42(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42) CM_FLAGS_FOR_EACH_41(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41) CM_FLAGS_FLAG_I(t, p42, 41)
#define CM_FLAGS_FOR_EACH_43(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43) CM_FLAGS_FOR_EACH_42(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42) CM_FLAGS_FLAG_I(t, p43, 42)
#define CM_FLAGS_FOR_EACH_44(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44) CM_FLAGS_FOR_EACH_43(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43) CM_FLAGS_FLAG_I(t, p44, 43)
#define CM_FLAGS_FOR_EACH_45(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45) CM_FLAGS_FOR_EACH_44(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44) CM_FLAGS_FLAG_I(t, p45, 44)
#define CM_FLAGS_FOR_EACH_46(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46) CM_FLAGS_FOR_EACH_45(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45) CM_FLAGS_FLAG_I(t, p46, 45)
#define CM_FLAGS_FOR_EACH_47(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47) CM_FLAGS_FOR_EACH_46(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46) CM_FLAGS_FLAG_I(t, p47, 46)
#define CM_FLAGS_FOR_EACH_48(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48) CM_FLAGS_FOR_EACH_47(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47) CM_FLAGS_FLAG_I(t, p48, 47)
#define CM_FLAGS_FOR_EACH_49(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49) CM_FLAGS_FOR_EACH_48(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48) CM_FLAGS_FLAG_I(t, p49, 48)
#define CM_FLAGS_FOR_EACH_50(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50) CM_FLAGS_FOR_EACH_49(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49) CM_FLAGS_FLAG_I(t, p50, 49)
#define CM_FLAGS_FOR_EACH_51(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51) CM_FLAGS_FOR_EACH_50(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50) CM_FLAGS_FLAG_I(t, p51, 50)
#define CM_FLAGS_FOR_EACH_52(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52) CM_FLAGS_FOR_EACH_51(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51) CM_FLAGS_FLAG_I(t, p52, 51)
#define CM_FLAGS_FOR_EACH_53(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53) CM_FLAGS_FOR_EACH_52(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52) CM_FLAGS_FLAG_I(t, p53, 52)
#define CM_FLAGS_FOR_EACH_54(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54) CM_FLAGS_FOR_EACH_53(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53) CM_FLAGS_FLAG_I(t, p54, 53)
#define CM_FLAGS_FOR_EACH_55(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55) CM_FLAGS_FOR_EACH_54(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54) CM_FLAGS_FLAG_I(t, p55, 54)
#define CM_FLAGS_FOR_EACH_56(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56) CM_FLAGS_FOR_EACH_55(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55) CM_FLAGS_FLAG_I(t, p56, 55)
#define CM_FLAGS_FOR_EACH_57(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57) CM_FLAGS_FOR_EACH_56(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56) CM_FLAGS_FLAG_I(t, p57, 56)
#define CM_FLAGS_FOR_EACH_58(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58) CM_FLAGS_FOR_EACH_57(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57) CM_FLAGS_FLAG_I(t, p58, 57)
#define CM_FLAGS_FOR_EACH_59(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59) CM_FLAGS_FOR_EACH_58(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58) CM_FLAGS_FLAG_I(t, p59, 58)
#define CM_FLAGS_FOR_EACH_60(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60) CM_FLAGS_FOR_EACH_59(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59) CM_FLAGS_FLAG_I(t, p60, 59)
#define CM_FLAGS_FOR_EACH_61(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61) CM_FLAGS_FOR_EACH_60(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60) CM_FLAGS_FLAG_I(t, p61, 60)
#define CM_FLAGS_FOR_EACH_62(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62) CM_FLAGS_FOR_EACH_61(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61) CM_FLAGS_FLAG_I(t, p62, 61)
#define CM_FLAGS_FOR_EACH_63(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62, p63) CM_FLAGS_FOR_EACH_62(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62) CM_FLAGS_FLAG_I(t, p63, 62)
#define CM_FLAGS_FOR_EACH_64(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62, p63, p64) CM_FLAGS_FOR_EACH_63(t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, p33, p34, p35, p36, p37, p38, p39, p40, p41, p42, p43, p44, p45, p46, p47, p48, p49, p50, p51, p52, p53, p54, p55, p56, p57, p58, p59, p60, p61, p62, p63) CM_FLAGS_FLAG_I(t, p64, 63)

// 用法与生成接口清单见文件头注释。
#define CM_FLAGS(UnderlyingT, ...)                                             \
    static_assert(                                                             \
        sizeof(UnderlyingT) * 8 >= CM_FLAGS_NARG(__VA_ARGS__),                 \
        "CM_FLAGS: flag count exceeds underlying type bit width");             \
    UnderlyingT flags_ = 0;                                                    \
    void reset_flags() { flags_ = static_cast<UnderlyingT>(0); }               \
    CM_FLAGS_FOR_EACH(CM_FLAGS_NARG(__VA_ARGS__), UnderlyingT, __VA_ARGS__)
