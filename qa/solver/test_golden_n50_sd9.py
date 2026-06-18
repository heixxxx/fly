import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from golden_solver import run_golden
run_golden(50, 9, 0.30, max_iter=300)
