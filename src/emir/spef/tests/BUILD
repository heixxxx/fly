# emir spef db 单测（阶段一）：SPEF 解析器单测（sp_parser_test——
# spef-db-plan §9.2 解析矩阵：头部全构造/单位换算/多值取值/名字映射/
# 节点分类与合计口径/方言容忍/跳过项/注释/文件级与段级错误边界/切块
# 计划/整文件与分段等价/真实工具样张回归〔文件存在才跑〕）+ 换算/路
# 由/合并层单测（sp_partition_test——spef-db-plan §9.3 换算矩阵：名字
# 换算与容错/块绑定三形态 + strip_prefix/分区路由与复制/按来源作用域
# 合并/覆盖率统计/汇总/序列化 round-trip；输入经 sp_parse_spef_text
# 走解析 → 换算 → 合并真实链路）。
# 手写样张 data/ 每个文件头注释写明用途与钉死的期望值；真实工具样张
# 为 GPL 许可（OpenSTA examples），不拷入仓库，按绝对路径存在才跑。

package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "sp_parser_test",
    srcs = ["sp_parser_test.cpp"],
    data = [
        "data/bad_first_line.spef",
        "data/bad_missing_end.spef",
        "data/basic.spef",
        "data/comments.spef",
        "data/dialect_conn.spef",
        "data/dialect_misc.spef",
        "data/namemap.spef",
        "data/nodes_self_other.spef",
        "data/rnet_pnet.spef",
        "data/units_ff.spef",
        "data/units_ff100.spef",
        "data/units_mix.spef",
    ],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/spef/cpp:fly_emir_spef",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)

cc_test(
    name = "sp_partition_test",
    srcs = ["sp_partition_test.cpp"],
    deps = [
        "@googletest//:gtest_main",
        "//src/emir/spef/cpp:fly_emir_spef",
    ],
    linkstatic = True,
    copts = ["-std=c++20"],
)
