"""DB Meta — db 目录的统一元信息文件（JSON）。

每个 db 目录下有一个 ``_DB_META`` JSON 文件（version 2，由原 bitsery
``_DB_META`` header + JSON ``_DB_CHAIN`` 合并而来），记录：
- ``created_at``：建库时间（>0 是 load_db 的"db 有效"哨兵）。
- ``data_path``：正式数据目录（空 = 数据在 db 目录内自包含）。db 级属性，
  task 参数编码不再携带，加载 db 时从此处获取。
- ``uid``：db 的逻辑身份（创建时生成，merge/迁移后不变——跨路径的稳定键）。
- ``role`` / ``logical_name``：角色与逻辑名。
- ``prev[]`` / ``next[]``：DAG 前向/反向边（edge = {uid, role, logical_name, db_path}）。
- ``absorbed_from[]``：merge 吸收的源 path 列表（迁移历史）。
- ``workers[]``：写者登记（worker_id/writer_id/hostname/ip_address/
  launch_command），跨进程 load_db 按 host 派发 idx load 的依据。

写者纪律（全部经本模块，不绕过）：
- 生产写路径全部在 master 进程（open_db 初写 / merge / migrate / WorkerInfo
  追加回调）；唯一例外是 worker 进程 task 内 open_db 建新库的一次性初写
  （新库创建时无其他写者）。worker 端 deserialize_args 只读。
- master 进程内存在真并发（C++ lane 线程的 WorkerInfo 追加 ↔ 主线程
  merge/migrate 边改写），GIL 不保护跨文件 IO 的 read-modify-write——
  由 ``_DB_META.lock`` 的 flock 串行化（flock 作用于 inode，同进程不同
  fd 间同样互斥；阻塞时释放 GIL）。
- 读用 ``LOCK_SH``（多读者并发），写用 ``LOCK_EX`` + tmp + os.replace
  原子替换；flock 绑定 fd，进程 crash 自动释放。
- 写路径之间不得嵌套（mutator 为纯函数，update 内不得再触发文件写）。

详见 ``docs/db-chain-design.md``。
"""

import fcntl
import hashlib
import json
import os
import time

from _fly_log import WARN


_META_FILE = "_DB_META"
_LOCK_FILE = "_DB_META.lock"
_META_VERSION = 2


# ── uid 生成 ──────────────────────────────────────────────────────

def generate_uid(db_path, role):
    """综合 目录 + 纳秒创建时间 + role 生成 uid。

    单次 run 内极低重复率（纳秒时间戳保证）。跨 run 不保证唯一
    （符合设计：load_db 加载新 db，建立新 uid 体系）。

    Args:
        db_path: db 目录路径。
        role: db 角色（可为 None）。

    Returns:
        12 字符 hex 字符串。
    """
    raw = f"{db_path}:{time.time_ns()}:{role or 'none'}"
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()[:12]


# ── _DB_META 文件读写（readers-writer 锁）────────────────────────

class DbMetaFile:
    """``_DB_META`` 文件的并发安全读写。

    读用 ``LOCK_SH``（共享，多 run 并发读不阻塞）；
    写用 ``LOCK_EX``（排他，串行化所有写者——含同进程不同线程）。
    写时整文件替换（tmp + os.replace），保证原子性。
    flock 绑定 fd，进程 crash 自动释放锁。
    """

    def __init__(self, db_path):
        self.db_path = db_path
        self.path = os.path.join(db_path, _META_FILE)
        self.lock_path = os.path.join(db_path, _LOCK_FILE)

    def exists(self):
        """_DB_META 文件是否存在。"""
        return os.path.isfile(self.path)

    def read(self):
        """并发安全读（LOCK_SH）。

        Returns:
            dict 或 None（文件不存在/损坏）。
        """
        if not os.path.isfile(self.path):
            return None
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                fcntl.flock(f.fileno(), fcntl.LOCK_SH)
                return json.load(f)
        except (json.JSONDecodeError, IOError, OSError) as e:
            WARN(f"DbMetaFile.read: corrupted meta at {self.path}: {e}")
            return None

    def update(self, mutator):
        """排写（LOCK_EX），read-modify-write 全程持锁。

        Args:
            mutator: callable(dict) -> dict，接收当前 meta dict（或空 dict），
                     返回修改后的新 dict。必须是纯函数（不得再触发文件写，
                     防嵌套 flock 自死锁）。

        Returns:
            修改后的 meta dict。
        """
        # 确保目录存在；确保 lock 文件存在（flock 需要一个可打开的 fd）
        os.makedirs(self.db_path, exist_ok=True)
        if not os.path.isfile(self.lock_path):
            with open(self.lock_path, "w") as f:
                f.write("")

        with open(self.lock_path, "w") as lf:
            fcntl.flock(lf.fileno(), fcntl.LOCK_EX)

            # 持锁读当前
            current = None
            if os.path.isfile(self.path):
                try:
                    with open(self.path, "r", encoding="utf-8") as cf:
                        current = json.load(cf)
                except (json.JSONDecodeError, IOError, OSError) as e:
                    WARN(f"DbMetaFile.update: corrupted meta at {self.path}, "
                         f"treating as empty: {e}")
            if current is None:
                current = {}

            # 改内存
            new_data = mutator(current)

            # 原子落盘
            tmp = self.path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(new_data, f, indent=2, ensure_ascii=False)
            os.replace(tmp, self.path)

        return new_data

    def write_new(self, meta_data):
        """初写（新建 db / merge target 继承身份时）。

        持 LOCK_EX 的读-改-写：文件已存在时保留 ``workers`` 键——merge
        重写与并发 WorkerInfo 追加存在窄窗口，整文件覆盖会丢已追加的
        登记条目。
        """
        def merge_existing(d):
            merged = dict(meta_data)
            if d.get("workers"):
                merged["workers"] = d["workers"]
            return merged

        return self.update(merge_existing)

    def remove(self):
        """删除 _DB_META 及 lock 文件。"""
        for p in (self.path, self.lock_path):
            try:
                os.remove(p)
            except FileNotFoundError:
                pass

    def append_worker(self, worker_entry):
        """workers[] 追加一条写者登记（WorkerInfo 回调的落点）。"""
        def updater(d):
            workers = d.setdefault("workers", [])
            if worker_entry not in workers:
                workers.append(worker_entry)
            return d
        return self.update(updater)

    def update_neighbor_path(self, uid, new_db_path, is_next):
        """merge 时更新 _DB_META 中指向 uid 的 db_path（邻居更新用）。

        Args:
            uid: 被迁移 db 的 uid。
            new_db_path: 迁移后的新 path。
            is_next: True → 更新 next[]；False → 更新 prev[]。
        """
        field = "next" if is_next else "prev"

        def updater(d):
            edges = d.get(field, [])
            for edge in edges:
                if edge.get("uid") == uid:
                    edge["db_path"] = new_db_path
            return d

        self.update(updater)


# ── meta 数据构造 helpers ─────────────────────────────────────────

def make_meta(uid, role, logical_name, created_at=None, prev=None, next_=None,
              absorbed_from=None, data_path=""):
    """构造一个完整的 _DB_META dict。"""
    return {
        "version": _META_VERSION,
        "created_at": created_at if created_at is not None else time.time(),
        "data_path": data_path or "",
        "uid": uid,
        "role": role,
        "logical_name": logical_name,
        "prev": prev or [],
        "next": next_ or [],
        "absorbed_from": absorbed_from or [],
        "workers": [],
    }


def make_edge(uid, role, logical_name, db_path):
    """构造 prev/next 列表中的一个边节点。"""
    return {
        "uid": uid,
        "role": role,
        "logical_name": logical_name,
        "db_path": db_path,
    }


def find_edge(edges, uid):
    """在 prev/next 列表中按 uid 查找边节点。

    Returns:
        匹配的 edge dict 或 None。
    """
    for edge in edges:
        if edge.get("uid") == uid:
            return edge
    return None


def update_edge_path(edges, uid, new_db_path):
    """更新 prev/next 列表中指定 uid 的 db_path（merge 更新邻居时用）。

    Returns:
        True 若找到了并更新；False 若未找到该 uid。
    """
    for edge in edges:
        if edge.get("uid") == uid:
            edge["db_path"] = new_db_path
            return True
    return False


def remove_edge(edges, uid):
    """从 prev/next 列表中移除指定 uid 的边。

    Returns:
        移除后的新列表（不修改原列表）。
    """
    return [e for e in edges if e.get("uid") != uid]


def append_edge(edges, edge):
    """向 prev/next 列表追加一条边（若 uid 已存在则不重复追加）。

    Returns:
        (new_list, appended) — 新列表 + 是否实际追加了。
    """
    if find_edge(edges, edge["uid"]) is not None:
        return edges, False
    new_list = list(edges)
    new_list.append(edge)
    return new_list, True


# ── 匹配逻辑 ──────────────────────────────────────────────────────

def match_edge(edge, role=None, logical_name=None, uid=None):
    """判断一个边节点是否匹配查询条件。

    所有非 None 的条件都需满足（AND 组合）。全部 None 则匹配任意。

    Args:
        edge: prev/next 列表中的边节点 dict。
        role: 要求的角色。
        logical_name: 要求的 logical_name。
        uid: 要求的 uid。

    Returns:
        True 若匹配。
    """
    if uid is not None and edge.get("uid") != uid:
        return False
    if role is not None and edge.get("role") != role:
        return False
    if logical_name is not None and edge.get("logical_name") != logical_name:
        return False
    return True
