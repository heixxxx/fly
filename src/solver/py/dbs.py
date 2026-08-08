"""SolverProject 的 db 子类定义（db chain role 体系）。

每个子类用类属性 ``role`` 声明身份，自动注册到 ``Database._ROLE_REGISTRY``。
find_db 按 role 匹配前驱，返回对应子类实例。

领域映射（EDA/求解等固定流程天然符合）：
- MatrixDb (role=matrix)：存储输入矩阵（build_matrix 产物）
- SolveDb (role=solve)：存储求解过程与结果（solve 产物）
"""

from storage import Database


class MatrixDb(Database):
    """存储输入矩阵的 db。role=matrix。

    build_matrix flow 的产物。含 "matrix" 对象（COO 格式矩阵 dict）。
    """
    role = "matrix"

    def load_matrix(self):
        """读取矩阵 dict（COO 格式：n, N, rows, cols, vals, b）。"""
        return self.read_object("matrix")


class SolveDb(Database):
    """存储求解过程与结果的 db。role=solve。

    solve flow 的产物。含 "__rasg__sol" 对象（全局解 x 向量）。
    """
    role = "solve"

    def load_solution(self):
        """读取全局解 x 向量。"""
        return self.read_object("__rasg__sol")
