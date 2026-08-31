from _fly_solver import (
    EXSlvSubdomainInfo,
    EXSlvSubdomainSolver,
    ex_slv_build_poisson_2d,
    ex_slv_partition_1d,
    ex_slv_extract_subdomain_matrix,
    ex_slv_residual_norm,
    ex_slv_ras_subdomain_update,
    ex_slv_graph_expand_overlap,
    ex_slv_find_outside_connections,
    ex_slv_ras_bupdated_solve,
)
# 求解器收敛（2026-08-31 用户裁定）：单次=多时间步单步，ras.py /
# ras_graph.py(v1) / ras_graph_daemon.py(v2) 三代入口退役，仅保留
# dynamic（attributes 编队 + gen 代际 + 矩阵 key 缓存 + RPC 迭代，
# 架构最完整）。单次求解经 solve_once（dynamic 单步封装）。
from .dbs import *
from .ras_graph_dynamic import (
    solve_ras_graph_dynamic, get_dynamic_result, solve_once,
    generate_poisson_matrix, MATRIX_OBJ_KEY,
    compute_exact_from_matrix, compute_exact_solution,
)
from .project import *
