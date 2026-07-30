#pragma once

#include <common/cpp/common_types.h>

namespace fly {

// 收集并格式化 fly 启动时的基础信息（binary / 机器 / 网络 / 运行时），
// 供 FLY::0000 message 在 master/worker 启动时打印。
//
// 多行分组对齐排版，每行形如 "  field : value"，字段名对齐到固定宽度便于阅读。
// 各信息收集自 /proc、uname、gethostname、Config、ProcessInfo、build_info 等。
class SystemInfo {
public:
    // 收集全部信息并返回排版后的多行文本（每行含换行）。
    // role: "master" 或 "worker"，标注当前进程角色。
    // listening_port: 实际监听端口（master 的 reactor 绑定端口 / worker 的 data server 端口），
    //   传 0 表示尚未绑定或不可用。
    // caller 负责逐行套上 [FLY::0000] 前缀后输出。
    static CMString format_startup_info(const CMString& role, int listening_port);

private:
    // 对齐辅助：返回 "  " + label 填充到 width + " : " + value + "\n"。
    static CMString align(const CMString& label, const CMString& value, size_t width = 18);
};

}  // namespace fly
