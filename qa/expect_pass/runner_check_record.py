"""运行器自测子阶段：记录与基准一致的固定值（场景 1：清单比对通过）。"""
from test import record

record("alpha", 100)
record("nested", {"x": [1, 2], "y": "hello"})
record("verdict_ok", True)
