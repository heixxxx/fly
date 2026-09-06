# fly 侧对上游源码的补丁记录

上游源码（src/）原则上原样保留；以下为 fly 引入所必需的最小改动，与
`src/lefdef` 的魔改 fork 模式一致。改上游源码后须重跑 `build_so.sh` 再提交。

## liberty_parser.c —— yyparse 入口重置组栈深度（2026-09-06）

`gs`/`gsindex`（组栈，static 文件级）在 yyparse 正常结束时由 pop_group 回卷，
但**语法错误提前 abort 的路径不回卷**——同进程后续解析把新组挂到已清理
（PIQuit）的旧组对象上，use-after-free SIGSEGV（LibraryBasics 崩溃实测）。

补丁：`yyparse()` 函数体入口 `gsindex = 0;`（见 liberty_parser.c:1150 附近，
标记 `fly patch`）。等价于每次解析从空栈开始；已签入的 liberty_parser.c 为
yacc 生成文件，重生成时须重新应用本补丁。

## src_local/syntax_check_stub.c —— 语法检查桩

上游语法检查套（syntax_checks.c syntax_parser.c synttok.c syntax_decls.c
syntform.c）与解析套存在同名内部辅助函数（push_group/make_simple 等），
全量链接冲突；fly 不使用 si2drCheckLibertyLibrary 检查入口，以桩裁剪。
