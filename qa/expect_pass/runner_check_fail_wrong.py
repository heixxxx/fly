"""运行器自测子阶段：记录与基准不符的值后以非零退出（场景 7：expect_pass=False）。"""
import sys
from test import record

record("exit_flag", "wrong")
sys.exit(1)
