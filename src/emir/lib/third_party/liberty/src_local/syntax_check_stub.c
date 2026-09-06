/* syntax_check 桩：上游 si2drCheckLibertyLibrary（语法语义检查）内部调用
   syntax_check 驱动独立语法检查器；fly 适配层不调用检查入口，PI.o 的引用
   由本桩满足——检查能力随 syntax_parser/synttok/syntax_decls 一并裁剪
   （该套与解析套存在同名内部辅助函数，全量链接冲突）。 */
#include "si2dr_liberty.h"

si2drErrorT syntax_check(si2drGroupIdT lib)
{
    (void)lib;
    return SI2DR_NO_ERROR;
}
