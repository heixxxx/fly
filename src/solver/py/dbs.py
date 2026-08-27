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

    # 求解编队 worker 属性前缀（单点定义）：经 worker_attr 生成的属性形如
    # "rasg:{uid}:{tag}"，uid 是 db 的逻辑身份（跨进程持久于 _DB_META）。
    # 并发 flow 各持不同 uid → 属性零交集 → 调度精确匹配不串池；collective
    # 排除正则统一用 r"^rasg:" 排除已被任一求解编队占用的 worker。
    _ATTR_PREFIX = "rasg"

    def load_solution(self):
        """读取全局解 x 向量。"""
        return self.read_object("__rasg__sol")

    def worker_attr(self, tag: str) -> str:
        """求解编队 worker 属性名（单点生成入口）："rasg:{uid}:{tag}"。

        所有进 worker attributes / task requires 的编队标签必须经此生成，
        禁止手拼字符串——前缀改动只改本处（各 flow 子类各自定义自己的
        属性生成成员，互不混杂）。restart 场景闭环：load_db 回来的同 db
        uid 相同，failed_tasks.bin 还原的 requires 与重新申请分配的属性
        自动一致。

        worker 侧可用性由 executor 时序保证：task 模块导入（包副作用完成
        _ROLE_REGISTRY 注册）先于 db 参数反序列化，db 按 role 重建为子类。
        """
        uid = self.get_uid()
        if not uid:
            raise RuntimeError(
                f"SolveDb has no uid (missing _DB_META): cannot build "
                f"worker attr for tag {tag!r}")
        return f"{self._ATTR_PREFIX}:{uid}:{tag}"
