#pragma once

// ── CM_PROPERTY 属性访问器宏 ── common/types ─────────────────────────
// 公共工具宏（非容器，本库为公共底座零重头依赖）：为「成员名 = 属性名
// + '_'」的类批量生成五件套属性访问接口，消除逐字段手写 getter/setter
// 的重复代码。design db（DS* 类型）与 container 几何类型（CM*）等均以
// 本宏声明属性访问面。
//
// 用法（成员必须以 '_' 结尾——宏按 attr_name##_ 定位成员）：
//   class DSLayer {
//   public:
//       CMString name_;
//       CM_PROPERTY(name)
//   };
//
// 生成接口清单（attr_name 以 name 为例，成员 name_）：
//   auto get_name() const               值拷贝（修改返回拷贝不影响成员）
//   auto& get_ref_name()                可变引用（原地修改成员）
//   const auto& get_cref_name() const   只读引用（const 对象零拷贝读取）
//   void set_name(const auto& value)    拷贝赋值（源对象保持完整）
//   void set_name_move(auto&& value)    移动赋值（大对象零拷贝转移）
//
// 实现说明：set 两式形参为 C++20 abbreviated function template（auto
// 形参），按实参类型实例化为成员函数模板——同一宏适配任意成员类型。

#include <utility>

#define CM_PROPERTY(attr_name)                                              \
    auto get_##attr_name() const { return attr_name##_; }                   \
    auto& get_ref_##attr_name() { return attr_name##_; }                    \
    const auto& get_cref_##attr_name() const { return attr_name##_; }       \
    void set_##attr_name(const auto& value) { attr_name##_ = value; }       \
    void set_##attr_name##_move(auto&& value) { attr_name##_ = std::move(value); }
