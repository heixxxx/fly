"""DbChainRegistry — master 进程级的 uid↔path 映射注册表。

本 run 内 db uid 到当前物理路径的权威映射。merge 时更新（单点覆盖）。
find_db 解析前驱物理位置时查询此注册表。

工作模型：
- master 进程级单例（同 DataService），所有 db 操作共用。
- 每个 db 被 open_db/load_db 打开时注册 uid_to_path_[uid] = db_path。
- merge 后覆盖 uid_to_path_[uid] = target_path（key 不变，value 更新）。
- worker 进程也有一个实例，但只注册本地已知的 db（find_db 在 worker 上
  也可用，但 uid→path 解析可能查不到——此时 fallback 到 prev.db_path）。

线程安全：操作在 master Python 主线程串行（建链/merge），但用锁保护
防未来多线程扩展。
"""

import threading

from _fly_log import DBG, WARN


class DbChainRegistry:
    """uid ↔ db_path 双向映射注册表。"""

    def __init__(self):
        self._lock = threading.RLock()
        self._uid_to_path = {}       # uid -> db_path
        self._path_to_uid = {}       # db_path -> uid

    def register(self, uid, db_path):
        """注册 uid → db_path（双向）。merge 时覆盖 path（uid 不变）。"""
        with self._lock:
            old_path = self._uid_to_path.get(uid)
            if old_path is not None and old_path != db_path:
                # merge：uid 指向新 path，清旧 path 反向映射
                self._path_to_uid.pop(old_path, None)
                DBG(f"DbChainRegistry: uid={uid} migrated {old_path} -> {db_path}")
            self._uid_to_path[uid] = db_path
            self._path_to_uid[db_path] = uid

    def unregister(self, uid):
        """注销 uid（db 被删除时）。"""
        with self._lock:
            path = self._uid_to_path.pop(uid, None)
            if path is not None:
                self._path_to_uid.pop(path, None)

    def resolve_uid(self, uid):
        """uid → 当前 db_path。找不到返回 None。"""
        with self._lock:
            return self._uid_to_path.get(uid)

    def resolve_path(self, db_path):
        """db_path → uid。找不到返回 None。"""
        with self._lock:
            return self._path_to_uid.get(db_path)

    def update_path(self, uid, new_db_path):
        """merge 后更新 uid 指向的 path（uid 不变，只改 path）。"""
        with self._lock:
            old_path = self._uid_to_path.get(uid)
            if old_path is None:
                WARN(f"DbChainRegistry.update_path: uid={uid} not registered")
                return
            if old_path != new_db_path:
                self._path_to_uid.pop(old_path, None)
            self._uid_to_path[uid] = new_db_path
            self._path_to_uid[new_db_path] = uid
            DBG(f"DbChainRegistry: uid={uid} path updated {old_path} -> {new_db_path}")

    def all_uids(self):
        """返回所有已注册的 uid（快照）。"""
        with self._lock:
            return list(self._uid_to_path.keys())

    def clear(self):
        """清空注册表（测试用）。"""
        with self._lock:
            self._uid_to_path.clear()
            self._path_to_uid.clear()


# ── 进程级单例 ────────────────────────────────────────────────────

_registry = None
_registry_lock = threading.Lock()


def get_registry():
    """获取进程级 DbChainRegistry 单例。"""
    global _registry
    if _registry is None:
        with _registry_lock:
            if _registry is None:
                _registry = DbChainRegistry()
    return _registry
