#pragma once

// Liberty .lib 解析适配层：包装新思 Open Liberty 参考解析器（si2dr PI 接口），
// 将组树遍历提取为 LIBLibrary 自有结构（深拷贝，PI 内存随后可整体丢弃）。
// 解析器以独立第三方库引入（SYNOPSYS Open Source License v1.0，保留于
// third_party/liberty/COPYING.pdf），本层是唯一的 C 接口触点。

#include <emir/lib/cpp/lib_types.h>

namespace fly {

// 解析单个 .lib 文件。文件打不开或语法错误抛 std::runtime_error。
LIBLibrary lib_parse_lib_file(const CMString& path);

}  // namespace fly
