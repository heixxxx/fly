#pragma once

// ── StrongIdT 强类型 id 模板 ── common/types ──────────────────────────
//
// 2026-09-16 用户裁定：各类 id 一律强类型 class，禁止裸整型——语义不明
// 是缺陷。本头 = 框架层模板机器（运算符集/哨兵/序列化直通/hash），全仓
// 公用；**业务实体的具体 id 类型不在本头定义**——一律在所属业务模块族
// 的公共位置实例化（族内单一权威点、禁止模块内重复定义、统一族前缀，
// 如 emir 族 = src/emir/common 的 EMIRCellId 族）。
//
// 强类型收益 = 编译级区分：跨实体 id 类型的运算/比较/赋值一律编译错
// （跨 Tag 不提供重载），杜绝把 pin id 当 cell id 用的静默错。
//
// 语义规格：
//   - 无效哨兵 = 内部整型最大值（kInvalid，与 design db 既有 kInvalidId
//     哨兵值同口径）；默认构造即哨兵值；
//   - 不提供到裸整型的隐式转换（防跨类漏洞经转换逃逸），显式 value()
//     访问器；
//   - 完整整数算术：+ - * / %、复合赋值、前置/后置自增自减；操作数两
//     族 = 同类×同类、同类×裸值（偏移算术 id + 1、区间 start + local
//     保持自然书写；裸值×同类仅 + 提供交换形态）；
//   - 减法特例：同类 id − 同类 id → 裸整型（差值/偏移量，可直接作下标
//     与长度——「平移差」是数量不是编号）；id − 裸值仍返回 id；
//   - 比较：同类×同类、同类×裸值、裸值×同类全六种。
//
// 序列化直通：serialize 按 sizeof(IntT) 分派 bitsery valueN b——字节级
// 与裸整型完全一致（容器键/元素、map/set 键同路径），既有落盘数据布局
// 零变化。**不经 FLY_SERIALIZE 宏**——宏的版本化 ext 会写版本前缀字节
// （fly::Version::serialize 的 writeSize），破坏与裸整型的逐位一致；
// 且模板成员体延迟实例化，本头保持公共底座零重头依赖（S::valueNb 在
// 实例化点解析，使用方必然已带序列化上下文）。

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

namespace fly {

template <typename Tag, typename IntT>
class StrongIdT {
public:
    // 无效哨兵（id 类型最大值）；默认构造即哨兵
    static constexpr IntT kInvalid = std::numeric_limits<IntT>::max();

    // 哨兵判定（裸值形态——供裸整型上下文判定；实例形态见 is_valid()）
    static constexpr bool is_valid(IntT v) { return v != kInvalid; }
    constexpr bool is_valid() const { return value_ != kInvalid; }

    constexpr StrongIdT() = default;
    explicit constexpr StrongIdT(IntT v) : value_(v) {}

    // 显式裸值访问（唯一出口——无隐式转换）
    constexpr IntT value() const { return value_; }

    // —— 算术（+ - * / %；同类×同类、同类×裸值、裸值×同类仅 +）——
    friend constexpr StrongIdT operator+(StrongIdT a, StrongIdT b) {
        return StrongIdT{static_cast<IntT>(a.value_ + b.value_)};
    }
    friend constexpr StrongIdT operator+(StrongIdT a, IntT b) {
        return StrongIdT{static_cast<IntT>(a.value_ + b)};
    }
    friend constexpr StrongIdT operator+(IntT a, StrongIdT b) {
        return StrongIdT{static_cast<IntT>(a + b.value_)};
    }
    friend constexpr StrongIdT operator-(StrongIdT a, IntT b) {
        return StrongIdT{static_cast<IntT>(a.value_ - b)};
    }
    friend constexpr StrongIdT operator*(StrongIdT a, StrongIdT b) {
        return StrongIdT{static_cast<IntT>(a.value_ * b.value_)};
    }
    friend constexpr StrongIdT operator*(StrongIdT a, IntT b) {
        return StrongIdT{static_cast<IntT>(a.value_ * b)};
    }
    friend constexpr StrongIdT operator/(StrongIdT a, StrongIdT b) {
        return StrongIdT{static_cast<IntT>(a.value_ / b.value_)};
    }
    friend constexpr StrongIdT operator/(StrongIdT a, IntT b) {
        return StrongIdT{static_cast<IntT>(a.value_ / b)};
    }
    friend constexpr StrongIdT operator%(StrongIdT a, StrongIdT b) {
        return StrongIdT{static_cast<IntT>(a.value_ % b.value_)};
    }
    friend constexpr StrongIdT operator%(StrongIdT a, IntT b) {
        return StrongIdT{static_cast<IntT>(a.value_ % b)};
    }

    // 同类 − 同类 → 裸整型（差值/偏移量特例，见文件头）
    friend constexpr IntT operator-(StrongIdT a, StrongIdT b) {
        return static_cast<IntT>(a.value_ - b.value_);
    }

    // —— 复合赋值（同类、裸值）——
    constexpr StrongIdT& operator+=(StrongIdT b) {
        value_ = static_cast<IntT>(value_ + b.value_);
        return *this;
    }
    constexpr StrongIdT& operator+=(IntT b) {
        value_ = static_cast<IntT>(value_ + b);
        return *this;
    }
    constexpr StrongIdT& operator-=(StrongIdT b) {
        value_ = static_cast<IntT>(value_ - b.value_);
        return *this;
    }
    constexpr StrongIdT& operator-=(IntT b) {
        value_ = static_cast<IntT>(value_ - b);
        return *this;
    }
    constexpr StrongIdT& operator*=(StrongIdT b) {
        value_ = static_cast<IntT>(value_ * b.value_);
        return *this;
    }
    constexpr StrongIdT& operator*=(IntT b) {
        value_ = static_cast<IntT>(value_ * b);
        return *this;
    }
    constexpr StrongIdT& operator/=(StrongIdT b) {
        value_ = static_cast<IntT>(value_ / b.value_);
        return *this;
    }
    constexpr StrongIdT& operator/=(IntT b) {
        value_ = static_cast<IntT>(value_ / b);
        return *this;
    }
    constexpr StrongIdT& operator%=(StrongIdT b) {
        value_ = static_cast<IntT>(value_ % b.value_);
        return *this;
    }
    constexpr StrongIdT& operator%=(IntT b) {
        value_ = static_cast<IntT>(value_ % b);
        return *this;
    }

    // —— 自增自减（前置/后置）——
    constexpr StrongIdT& operator++() {
        value_ = static_cast<IntT>(value_ + 1);
        return *this;
    }
    constexpr StrongIdT operator++(int) {
        const StrongIdT tmp = *this;
        ++*this;
        return tmp;
    }
    constexpr StrongIdT& operator--() {
        value_ = static_cast<IntT>(value_ - 1);
        return *this;
    }
    constexpr StrongIdT operator--(int) {
        const StrongIdT tmp = *this;
        --*this;
        return tmp;
    }

    // —— 比较（同类×同类、同类×裸值、裸值×同类，全六种）——
    friend constexpr bool operator==(StrongIdT a, StrongIdT b) {
        return a.value_ == b.value_;
    }
    friend constexpr bool operator!=(StrongIdT a, StrongIdT b) {
        return a.value_ != b.value_;
    }
    friend constexpr bool operator<(StrongIdT a, StrongIdT b) {
        return a.value_ < b.value_;
    }
    friend constexpr bool operator<=(StrongIdT a, StrongIdT b) {
        return a.value_ <= b.value_;
    }
    friend constexpr bool operator>(StrongIdT a, StrongIdT b) {
        return a.value_ > b.value_;
    }
    friend constexpr bool operator>=(StrongIdT a, StrongIdT b) {
        return a.value_ >= b.value_;
    }
    friend constexpr bool operator==(StrongIdT a, IntT b) {
        return a.value_ == b;
    }
    friend constexpr bool operator!=(StrongIdT a, IntT b) {
        return a.value_ != b;
    }
    friend constexpr bool operator<(StrongIdT a, IntT b) {
        return a.value_ < b;
    }
    friend constexpr bool operator<=(StrongIdT a, IntT b) {
        return a.value_ <= b;
    }
    friend constexpr bool operator>(StrongIdT a, IntT b) {
        return a.value_ > b;
    }
    friend constexpr bool operator>=(StrongIdT a, IntT b) {
        return a.value_ >= b;
    }
    friend constexpr bool operator==(IntT a, StrongIdT b) {
        return a == b.value_;
    }
    friend constexpr bool operator!=(IntT a, StrongIdT b) {
        return a != b.value_;
    }
    friend constexpr bool operator<(IntT a, StrongIdT b) {
        return a < b.value_;
    }
    friend constexpr bool operator<=(IntT a, StrongIdT b) {
        return a <= b.value_;
    }
    friend constexpr bool operator>(IntT a, StrongIdT b) {
        return a > b.value_;
    }
    friend constexpr bool operator>=(IntT a, StrongIdT b) {
        return a >= b.value_;
    }

    // —— 序列化直通（见文件头；按 sizeof 分派，字节级与裸 IntT 一致）——
    template <typename S>
    void serialize(S& s) {
        if constexpr (sizeof(IntT) == 8) {
            s.value8b(value_);
        } else if constexpr (sizeof(IntT) == 4) {
            s.value4b(value_);
        } else if constexpr (sizeof(IntT) == 2) {
            s.value2b(value_);
        } else {
            s.value1b(value_);
        }
    }

    // 唯一数据成员（项目风格公开 + '_' 后缀；序列化/hash 直书友好）
    IntT value_ = kInvalid;
};

// 强类型不变式自检（框架探针 Tag）
static_assert(sizeof(StrongIdT<struct StrongIdSizeProbeTag, uint32_t>) ==
              sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<
              StrongIdT<struct StrongIdTrivialProbeTag, uint64_t>>);

}  // namespace fly

// 容器键（std::unordered_map/set 直用）
namespace std {

template <typename Tag, typename IntT>
struct hash<fly::StrongIdT<Tag, IntT>> {
    size_t operator()(const fly::StrongIdT<Tag, IntT>& id) const noexcept {
        return hash<IntT>{}(id.value());
    }
};

}  // namespace std

// 诊断输出（日志/调试打印）
template <typename Tag, typename IntT>
std::ostream& operator<<(std::ostream& os, fly::StrongIdT<Tag, IntT> id) {
    return os << id.value();
}
