"""运行器自测子阶段：记录与基准不符的值（场景 2：值不一致、判定不成立、多键）。

基准里 nested.x 是 [1, 2]、verdict_bad 期望 true、且没有 extra_key 键——
本脚本故意写出 [1, 9]、false 和多出的 extra_key，编排脚本捕获 SubcaseError
后断言差异文案。
"""
from test import record

record("alpha", 100)
record("nested", {"x": [1, 9], "y": "hello"})
record("verdict_bad", False, detail=[7])
record("extra_key", 1)
