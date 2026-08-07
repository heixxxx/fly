"""DB Chain — db 之间的双向 DAG 机制。

每个 db 目录下有一个 ``_DB_CHAIN`` JSON 文件，记录：
- ``uid``：db 的逻辑身份（创建时生成，merge 后不变）。
- ``role``：db 角色（由 _Database 子类的类属性决定）。
- ``prev[]``：前驱 db 列表（DAG 前向边，权威边）。
- ``next[]``：后继 db 列表（DAG 反向边，加速索引，可自愈重建）。
- ``absorbed_from[]``：merge 吸收的源 path 列表（迁移历史）。

并发安全：readers-writer 文件锁（fcntl.flock）。
- 读用 ``LOCK_SH``：多 run 并发读同一 db 不阻塞。
- 写用 ``LOCK_EX``：建链/merge 更新邻居时独占，阻塞读者直到写完。
- 写时整文件替换（``tmp`` + ``os.replace``），无读改写交叉。

详见 ``docs/db-chain-design.md``。
"""

import fcntl
import hashlib
import json
import os
import time

from _fly_log import WARN, INFO, DBG


_CHAIN_FILE = "_DB_CHAIN"
_LOCK_FILE = "_DB_CHAIN.lock"
_CHAIN_VERSION = 1


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


# ── _DB_CHAIN 文件读写（readers-writer 锁）────────────────────────

class DbChainFile:
    """``_DB_CHAIN`` 文件的并发安全读写。

    读用 ``LOCK_SH``（共享，多 run 并发读不阻塞）；
    写用 ``LOCK_EX``（排他，串行化所有写者）。
    写时整文件替换（tmp + os.replace），保证原子性。
    flock 绑定 fd，进程 crash 自动释放锁。
    """

    def __init__(self, db_path):
        self.db_path = db_path
        self.path = os.path.join(db_path, _CHAIN_FILE)
        self.lock_path = os.path.join(db_path, _LOCK_FILE)

    def exists(self):
        """_DB_CHAIN 文件是否存在。"""
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
            WARN(f"DbChainFile.read: corrupted chain at {self.path}: {e}")
            return None

    def update(self, mutator):
        """排写（LOCK_EX），read-modify-write 全程持锁。

        Args:
            mutator: callable(dict) -> dict，接收当前 chain dict（或空 dict），
                     返回修改后的新 dict。

        Returns:
            修改后的 chain dict。
        """
        # 确保目录存在
        os.makedirs(self.db_path, exist_ok=True)
        # 确保 lock 文件存在（flock 需要一个可打开的 fd）
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
                    WARN(f"DbChainFile.update: corrupted chain at {self.path}, "
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

    def write_new(self, chain_data):
        """首次写入（新建 db 时），直接写不需 read-modify-write。

        仍走 LOCK_EX 保证与并发写者互斥。
        """
        os.makedirs(self.db_path, exist_ok=True)
        if not os.path.isfile(self.lock_path):
            with open(self.lock_path, "w") as f:
                f.write("")
        with open(self.lock_path, "w") as lf:
            fcntl.flock(lf.fileno(), fcntl.LOCK_EX)
            tmp = self.path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(chain_data, f, indent=2, ensure_ascii=False)
            os.replace(tmp, self.path)

    def remove(self):
        """删除 _DB_CHAIN 及 lock 文件（merge 彻底删源时调用）。"""
        for p in (self.path, self.lock_path):
            try:
                os.remove(p)
            except FileNotFoundError:
                pass

    def update_neighbor_path(self, uid, new_db_path, is_next):
        """merge 时更新 _DB_CHAIN 中指向 uid 的 db_path（邻居更新用）。

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


# ── chain 数据构造 helpers ────────────────────────────────────────

def make_chain(uid, role, logical_name, created_at=None, prev=None, next_=None,
               absorbed_from=None):
    """构造一个完整的 chain dict。"""
    return {
        "version": _CHAIN_VERSION,
        "uid": uid,
        "role": role,
        "logical_name": logical_name,
        "created_at": created_at if created_at is not None else time.time(),
        "prev": prev or [],
        "next": next_ or [],
        "absorbed_from": absorbed_from or [],
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
