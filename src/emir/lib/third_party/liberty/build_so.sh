#!/usr/bin/env bash
# 新思 Open Liberty 参考解析器 —— 按上游原构建方式（CMakeLists.txt：glob 全部
# src/*.c 除 main.c）编译为库，供 fly bazel 引入。源码保留于本目录以便后续优化。
#
# 成员清单（等价上游静态库的「按需提取首定义胜出」语义）：
# - 解析套：PI.c liberty_parser.c liberty_front_lex.c token.c
#           + libhash.c libstrtab.c mymalloc.c（基础库）
#           + attr_lookup.c group_lookup.c（gperf 组/属性名查找表，解析路径需要）
# - syntax_check 桩（src_local/）：上游语法检查套（syntax_checks.c syntax_parser.c
#   synttok.c syntax_decls.c syntform.c）与解析套存在同名内部辅助函数
#   （push_group/make_simple 等），全量链接冲突；fly 不使用检查入口，以桩
#   裁剪（src_local/ 下集中存放 fly 侧对上游的最小补丁）。
# - 排除 main.c / syntform.c：上游独立工具入口。
#
# 产物 lib/libsi2dr_liberty.{a,so} 签入仓库；改源码后重跑本脚本再提交。
set -euo pipefail
cd "$(dirname "$0")"

MEMBERS="src/PI.c src/liberty_parser.c src/liberty_front_lex.c src/token.c
src/attr_lookup.c src/group_lookup.c src/libhash.c src/libstrtab.c src/mymalloc.c
src_local/syntax_check_stub.c"

OBJS=""
for s in $MEMBERS; do
    o="build_obj/$(basename "${s%.c}").pic.o"
    gcc -c -O2 -fPIC -w -Iinclude "$s" -o "$o"
    OBJS="$OBJS $o"
done

ar rcs lib/libsi2dr_liberty.a $OBJS
gcc -shared -Wl,-soname,libsi2dr_liberty.so -o lib/libsi2dr_liberty.so $OBJS
rm -rf build_obj

echo "built lib/libsi2dr_liberty.a + lib/libsi2dr_liberty.so ($(du -h lib/libsi2dr_liberty.a | cut -f1))"
