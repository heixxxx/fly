"""运行器自测子阶段：记录与基准不符的值后以非零退出（场景 8：expect_pass 默认 True）。"""
import sys
from test import record

record("exit_flag2", "wrong")
sys.exit(1)
