"""Project — 业务流程管理对象（比 db 更高一级的管理单元）。

Project 把一条业务流程的多个步骤打包，统一管理各步骤产生的 db。

核心设计：
1. **流程注册制**：基类只提供机制（建库/取库/冻结/持久化/load），不含业务流程；
   每个流程 API 是普通函数，通过 ``@register_flow(子类)`` 注入到指定 Project 子类，
   实现拆分到不同模块，避免基类臃肿。
2. **flow 自己建库返回**：不暴露通用 create_db；每个 flow 内部 ``self._create_db(name)``
   建库并 ``return db``。``name`` = db 子目录名 = Project 内部 key；重名 → WARN + 自动递增。
3. **跨流程数据依赖显式传**：flow 间不默认传数据；需用另一 db 数据时由用户显式传 db 对象
   （如 ``solve(name, matrix_db, ...)``），支持 project 内/外 db 作输入。

详见 docs/project-design.md。
"""

import json
import os
import time
import uuid

from _fly_log import WARN, INFO, DBG


_PROJECT_META = "_PROJECT_META.json"


def register_flow(target_cls):
    """把函数注册为 ``target_cls`` 的流程方法（业务流程 API）。

    注册后 func 成为 target_cls 的方法，可通过 target_cls 实例调用，
    ``self`` 自动绑定为该 Project 实例，可访问 ``self._create_db`` / ``self.get_db`` 等。

    注册时函数直接 ``setattr`` 到类上（描述符协议让实例访问时自动绑 self），
    与类体内定义的方法行为完全一致。

    Args:
        target_cls: 目标 Project 子类（如 SolverProject）。传 Project 基类
            则所有子类实例都获得该方法（慎用，会污染所有子类）。

    Example::

        @register_flow(SolverProject)
        def build_matrix(self, name, matrix_path):
            db = self._create_db(name)
            ...
            return db
    """
    def decorator(func):
        existing = target_cls.__dict__.get(func.__name__)
        if existing is not None:
            WARN(f"register_flow: overriding existing flow '{func.__name__}' "
                 f"on {target_cls.__name__}")
        setattr(target_cls, func.__name__, func)
        # UserDoc owner 回填：@document 装饰器在内层先执行，把 doc 挂在 func._fly_userdoc
        # 上（此时 owner=None）；本装饰器在外层后执行，回填 owner=所属类，零耦合（仅 getattr
        # 检测属性，不 import userdoc 模块）。详见 fly.userdoc。
        doc = getattr(func, "_fly_userdoc", None)
        if doc is not None:
            doc._owner = target_cls
        # 类级注册表（内省/list_flows 用）。每个子类持有自己的表，避免共享基类表。
        if "_flows" not in target_cls.__dict__:
            target_cls._flows = {}
        target_cls._flows[func.__name__] = func
        DBG(f"register_flow: '{func.__name__}' registered on {target_cls.__name__}")
        return func
    return decorator


class Project:
    """业务流程管理对象基类。

    Project 自身只存必要元信息（下属 db 的路径列表 + 状态），不存实质数据。
    通过 ``_PROJECT_META.json`` 持久化，``load()`` 全量恢复所有 db。

    业务流程（flow）通过 :func:`register_flow` 注册到子类，基类不含任何具体流程。
    """

    # 类级注册表：flow_name -> func（内省/list_flows 用）。子类各自持有。
    _flows = {}

    def __init__(self, db_path: str):
        self.db_path = os.path.abspath(db_path)
        os.makedirs(self.db_path, exist_ok=True)

        meta_path = os.path.join(self.db_path, _PROJECT_META)
        self._db_cache = {}      # actual_name -> Database（避免重复 load/open）
        self._meta_path = meta_path

        if os.path.isfile(meta_path):
            # 已存在 project：读回绑定（不重建目录）。
            with open(meta_path, "r", encoding="utf-8") as f:
                self._meta = json.load(f)
        else:
            # 新建 project。
            self._meta = {
                "class": f"{type(self).__module__}.{type(self).__name__}",
                "project_id": uuid.uuid4().hex[:6],
                "created_at": int(time.time()),
                "dbs": {},
            }
            self.save()

    # ── protected：供注册的 flow 内部建库 ──────────────────────────────

    def _create_db(self, name: str, data_path: str = "", db_cls=None, prev=None):
        """flow 内部建库（不暴露终端用户）。

        在 project 主目录下创建 ``<name>`` 子目录作为 db db_path，
        调 ``fly.open_db``（已存在则自动递增 ``name.1``/``name.2``），
        记入 meta 并缓存句柄。

        Args:
            name: db 子目录名 + Project 内部 key（logical_name）。
                重名 → WARN 提醒（实际目录由 open_db 自动递增）。
            data_path: 可选 db data_path（透传给 open_db）。
            db_cls: _Database 子类（决定 role）。默认 _Database（无 role）。
            prev: 前驱 db 句柄列表（DAG 边）。默认 None（无前驱）。

        Returns:
            ``Database`` 句柄。
        """
        from fly import open_db

        db_base = os.path.join(self.db_path, name)

        # 检测 logical_name 重名 → WARN 提醒（意见1）。
        existing = [v for v in self._meta["dbs"].values()
                    if v.get("logical_name") == name]
        if existing:
            WARN(f"Project: db name '{name}' already exists, "
                 f"creating a new variant (e.g. '{name}.1')")

        db = open_db(db_base, data_path, db_cls=db_cls, prev=prev, logical_name=name)
        actual_name = os.path.relpath(db.get_db_path(), self.db_path)
        actual_name = actual_name.replace(os.sep, "/")

        self._meta["dbs"][actual_name] = {
            "logical_name": name,
            "db_path": db.get_db_path(),
            "data_path": data_path,  # 迁移自包含校验依据（旧 meta 无此字段视为 ""）
            "uid": db.get_uid(),     # db chain uid（用于跨 run 迁移追踪）
            # 浮点秒：同名多次运行可能密集发生，int 秒无法区分先后。
            "created_at": time.time(),
        }
        self._db_cache[actual_name] = db
        self.save()
        DBG(f"Project._create_db: name={name}, actual={actual_name}, "
            f"db_path={db.get_db_path()}")
        return db

    def _freeze_task_deps(self, db, depends_on: list) -> list:
        """flow 内部 helper：构造 freeze task 的 inputs（依赖对象 full_name）。

        freeze 作为异步 task（非 flow 内同步调用）：它在依赖对象写完后由 master 调度，
        task 内调 db.freeze() 通知 master（stream 模式即时确认+广播；非 stream 模式
        task 成功时 commit_pending_frozen + 广播）。详见 docs/project-design.md §6.7。

        Args:
            db: flow 内 ``_create_db`` 返回的 db 句柄（用它拼 full_name，避免重名递增
                时 logical_name 与 actual_name 不一致的歧义）。
            depends_on: 依赖的短对象名列表（在 db 命名空间下），这些对象写完后 freeze
                task 才被调度。

        Returns:
            full_name 列表，供 @as_task(inputs=...) 使用。
        """
        return [db.get_full_name(short) for short in depends_on]

    # ── 公共 API ──────────────────────────────────────────────────────

    def get_db(self, name: str, latest: bool = False):
        """取回指定 db 句柄。

        语义：
        - 默认（latest=False）：按 **actual_name**（磁盘子目录名）精确匹配。
          重名递增产生的 ``db.1``/``db.2`` 是独立 actual_name，需显式
          ``get_db('db.1')`` 获取。
        - latest=True：把 name 当作 logical_name，取该名 created_at 最大者
          （即同名多次运行中最新的一次）。

        命中缓存直接返回；否则 ``load_db`` 恢复（master-only）。

        Args:
            name: actual_name（默认）或 logical_name（latest=True 时）。
            latest: 若 True，按 logical_name 取最新版。

        Returns:
            ``Database`` 句柄。

        Raises:
            KeyError: 该 name 不存在。
        """
        actual = self._resolve_actual_name(name, latest=latest)
        if actual is None:
            raise KeyError(f"Project has no db named '{name}'. "
                           f"Available: {self.list_dbs()}")
        if actual in self._db_cache:
            return self._db_cache[actual]
        # 缓存未命中（如重新绑定 Project / load 后未访问过）。
        # 注意：不能用 open_db——它会因 _DB_META 已存在而递增创建空库（read→EOF）。
        # 已 freeze 的库走 load_db（恢复索引，master-only）；未 freeze 的库说明
        # 同进程刚建（必在缓存），缓存未命中即等价于跨进程/重新绑定，按既有库 load。
        from fly import load_db
        db_path = self._meta["dbs"][actual]["db_path"]
        db = load_db(db_path)
        self._db_cache[actual] = db
        return db

    def freeze_db(self, name: str, latest: bool = False):
        """同步冻结指定 db（master 本地 freeze，阻塞到完成）。

        用于纯管理场景（非 flow 异步路径）。flow 内部应改用提交 freeze task
        （见 ``_freeze_task_deps``）以保持异步范式。

        Args:
            name: actual_name（默认）或 logical_name（latest=True 时）。
        """
        db = self.get_db(name)
        db.freeze()

    def is_db_frozen(self, name: str, latest: bool = False) -> bool:
        """查询 db 是否已 frozen（懒查询 master 权威状态）。

        flow 提交 freeze task 后立即返回 db，frozen 是"将来态"。本方法实时向
        master 查询（``MasterAgent::is_db_frozen``，覆盖 confirmed ∪ pending），
        返回当前真实状态，不依赖 meta 缓存。

        master-only（worker 进程无此 API）。

        Args:
            name: actual_name（默认）或 logical_name（latest=True 时）。
            latest: 若 True，按 logical_name 取最新版。
        """
        from fly.runtime import get_agent
        actual = self._resolve_actual_name(name, latest=latest)
        if actual is None:
            return False
        db_path = self._meta["dbs"][actual]["db_path"]
        return get_agent()._agent.is_db_frozen(db_path)

    def wait_frozen(self, name: str, timeout: float = 3600.0, interval: float = 0.5,
                    latest: bool = False):
        """阻塞等待某 db 的异步 freeze task 完成（轮询 master 状态）。

        flow 提交 freeze task 后，freeze 由 master 调度在依赖数据写完后执行。
        本方法轮询 db 句柄本进程 frozen 标志（master 已 commit 并广播后置位）。

        Args:
            name: actual_name（默认）或 logical_name（latest=True 时）。
            timeout: 最大等待秒数（默认 3600）。
            interval: 轮询间隔（默认 0.5s）。
            latest: 若 True，按 logical_name 取最新版。

        Returns:
            True 若 confirmed frozen；False 若超时。
        """
        import time as _t
        t0 = _t.time()
        actual = self._resolve_actual_name(name, latest=latest)
        if actual is None:
            return False
        db = self.get_db(name, latest=latest)
        while _t.time() - t0 < timeout:
            if db.is_frozen():
                return True
            _t.sleep(interval)
        return False

    def freeze_all(self):
        """同步冻结所有未 frozen 的 db（master 本地，阻塞）。"""
        from fly.runtime import get_agent
        agent = get_agent()
        for actual, info in self._meta["dbs"].items():
            db_path = info["db_path"]
            if not agent._agent.is_db_frozen(db_path):
                db = self._db_cache.get(actual)
                if db is None:
                    # 缓存未命中（跨进程重新绑定 Project 等）。必须用 load_db 恢复
                    # 已有库——不能用 open_db：它会因 _DB_META 已存在而递增创建
                    # 一个空库（见 get_db 同款陷阱，L198 注释），freeze 那个空库
                    # 对原 db_path 无效，is_db_frozen 仍为 False。load_db 是
                    # master-only，freeze_all 本身即 master 本地操作，语义匹配。
                    from fly import load_db
                    db = load_db(info["db_path"])
                    self._db_cache[actual] = db
                db.freeze()

    def list_dbs(self) -> list:
        """返回所有 db 的 actual_name（磁盘子目录名，即 get_db 的精确匹配键）。"""
        return list(self._meta["dbs"].keys())

    def resume(self):
        """断点重跑：重投各归属 db 目录下 failed_tasks.bin 里的未完成 task。

        task 级断点（用户裁定粒度）：不记录/不检测已完成 task——已完成 task 的
        输出对象（正式+temp）经 ``load_project`` 恢复即 ready，调度是对象驱动的
        （mark_data_ready）；FAILED/PENDING/Running task 由 failed_tasks.bin 持久化
        （FAILED 实时落盘；PENDING/RUNNING 在优雅退出路径 stop/fast_exit 落盘），
        重投后依赖图自然收敛。

        失败记录按 task 归属 db 落盘（{db_path}/failed_tasks.bin，Task db 归属
        规则）——遍历 project 全部 db 目录搜索，bin 随 db 目录迁移/持久。

        前提：worker 已唤起（重投 task 需要执行者）。
        语义边界：SIGKILL/OOM 场景 RUNNING/PENDING 的 spec 丢失（无 persist
        时机），resume 只恢复此前 FAILED 记录，其余靠 flow 重放兜底（同
        write_context_hash 幂等重写 + ready 对象下游直通）。

        master-only。无任何 bin（旧 project 或从未失败）为 no-op。
        """
        from fly.runtime import get_agent
        db_paths = [os.path.join(self.db_path, actual)
                    for actual in self._meta["dbs"].keys()]
        restarted = get_agent().restart_failed_tasks(db_paths)
        if restarted == 0:
            INFO("Project.resume: no failed_tasks.bin under any db of "
                 f"{self.db_path} (nothing to resume)")
        else:
            INFO(f"Project.resume: restarted {restarted} failed tasks")

    def status(self) -> list:
        """返回每个 db 的状态快照（断点导航）：[{actual, logical, db_path, frozen}]。

        frozen 实时查询 master（懒查询，不依赖 meta 缓存）。master-only。
        """
        from fly.runtime import get_agent
        out = []
        for actual, info in self._meta["dbs"].items():
            frozen = False
            try:
                frozen = get_agent()._agent.is_db_frozen(info["db_path"])
            except Exception:
                pass
            out.append({
                "actual": actual,
                "logical": info.get("logical_name"),
                "db_path": info["db_path"],
                "frozen": frozen,
            })
        return out

    def migrate(self, new_path: str):
        """project 目录整体搬迁到 new_path（离线操作，不要求 frozen）。

        含全部 db 目录、_PROJECT_META.json（断点 bin 按归属 db 随各 db 目录迁移）。
        半成品 db（未 frozen）同样可迁——idx 事务段 + ABORT truncate 保证搬后
        可恢复（load_db 丢弃未闭合段）。

        前提：相关 master/worker 进程已退出（不在运行中搬迁）。
        原子性：目录 rename（同 FS）原子；meta/chain 改写在搬迁后进行——
        中断重入时 load_project 的 stale db_path fallback 兜底恢复。

        Raises:
            RuntimeError: new_path 已存在 / meta 缺失 / db 目录缺失 /
                data 不自包含（data_path 在 project 外——先 consolidate）。
        """
        import shutil as _shutil
        new_path = os.path.abspath(new_path)
        if new_path == self.db_path:
            raise RuntimeError(f"migrate: new_path identical to current: {new_path}")
        if os.path.exists(new_path):
            raise RuntimeError(f"migrate: new_path already exists: {new_path}")

        old_root = self.db_path
        # 校验 + 构建路径映射（旧 → 新）。
        path_map = {}   # db_path / data_path 通用：旧绝对路径 → 新绝对路径
        for actual, info in self._meta["dbs"].items():
            old_bp = info["db_path"]
            if not os.path.isdir(old_bp):
                raise RuntimeError(f"migrate: db dir missing: {old_bp}")
            rel = os.path.relpath(old_bp, old_root)
            if rel.startswith(".."):
                raise RuntimeError(
                    f"migrate: db '{actual}' lives outside project dir "
                    f"({old_bp}) — unsupported")
            path_map[old_bp] = os.path.join(new_path, rel)
            old_dp = info.get("data_path", "")
            if old_dp:
                if not os.path.isdir(old_dp):
                    raise RuntimeError(f"migrate: data_path missing: {old_dp}")
                drel = os.path.relpath(old_dp, old_root)
                if drel.startswith(".."):
                    raise RuntimeError(
                        f"migrate: data_path of db '{actual}' outside project "
                        f"({old_dp}) — data not self-contained; run "
                        f"migrate_project(consolidate=True) first")
                path_map[old_dp] = os.path.join(new_path, drel)

        # 物理搬迁（同 FS rename 原子；跨 FS move = copy + rm）。
        os.makedirs(os.path.dirname(new_path) or ".", exist_ok=True)
        _shutil.move(old_root, new_path)

        # meta 改写（tmp + os.replace 原子）。
        self.db_path = new_path
        self._meta_path = os.path.join(new_path, _PROJECT_META)
        for info in self._meta["dbs"].values():
            if info["db_path"] in path_map:
                info["db_path"] = path_map[info["db_path"]]
            old_dp = info.get("data_path", "")
            if old_dp and old_dp in path_map:
                info["data_path"] = path_map[old_dp]
        self.save()

        # chain 邻居边改写：project 内 db 间的 prev/next 边 db_path 更新
        #（uid 不变；project 外的边不动）。DbMetaFile.update 持 LOCK_EX +
        # 原子替换（顶层 data_path 同批改写）。
        from storage import DbMetaFile
        for info in self._meta["dbs"].values():
            meta_f = DbMetaFile(info["db_path"])
            if not meta_f.exists():
                continue

            def _rewrite(d, mapping=path_map):
                for field in ("prev", "next"):
                    for edge in d.get(field, []):
                        old = edge.get("db_path")
                        if old in mapping:
                            edge["db_path"] = mapping[old]
                if d.get("data_path") in mapping:
                    d["data_path"] = mapping[d["data_path"]]
                return d

            meta_f.update(_rewrite)

        INFO(f"Project.migrate: {old_root} -> {new_path} "
             f"({len(self._meta['dbs'])} dbs, chain edges rewritten)")

    def list_flows(self) -> list:
        """返回已注册到本类（及基类）的 flow 名（内省用）。"""
        # 收集本类及所有基类的 _flows（MRO 顺序，去重，子类覆盖基类）。
        flows = {}
        for cls in reversed(type(self).__mro__):
            flows.update(getattr(cls, "_flows", {}))
        return sorted(flows.keys())

    # ── 持久化 ────────────────────────────────────────────────────────

    def save(self):
        """把内存 meta dump 成 _PROJECT_META.json。"""
        tmp = self._meta_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self._meta, f, indent=2, ensure_ascii=False)
        os.replace(tmp, self._meta_path)

    @classmethod
    def load(cls, db_path: str) -> "Project":
        """读 _PROJECT_META.json + 动态还原子类 + 对每个 db 调 fly.load_db。

        master-only（内部用 fly.load_db，worker 调用会 AttributeError）。
        全量恢复所有 db 的索引 + 按需拉起 worker。

        Args:
            db_path: Project 主目录路径。

        Returns:
            还原出的子类实例（如 SolverProject）；class 路径失效则回退基类。
        """
        from fly.runtime import _mode
        meta_path = os.path.join(db_path, _PROJECT_META)
        if not os.path.isfile(meta_path):
            raise RuntimeError(f"load_project: no {_PROJECT_META} at {db_path}")
        with open(meta_path, "r", encoding="utf-8") as f:
            meta = json.load(f)

        # 按 meta["class"] 动态还原真实子类。
        real_cls = cls
        cls_path = meta.get("class")
        if cls_path:
            try:
                import importlib
                mod_name, _, qualname = cls_path.rpartition(".")
                mod = importlib.import_module(mod_name)
                real_cls = getattr(mod, qualname)
                # import 子类模块时，其顶层 import flows（如 solver.project
                # import solver.flows）会触发 @register_flow 注册，flow 随之可用。
            except (ImportError, AttributeError) as e:
                WARN(f"load_project: cannot restore class '{cls_path}' ({e}), "
                     "falling back to base Project (flows unavailable)")
                real_cls = cls

        proj = real_cls.__new__(real_cls)
        proj.db_path = os.path.abspath(db_path)
        proj._meta = meta
        proj._meta_path = meta_path
        proj._db_cache = {}

        # 全量 load_db 恢复每个 db（master-only）。
        if _mode != "master":
            raise RuntimeError("load_project is master-only (uses fly.load_db)")

        from fly import load_db
        for actual, info in meta["dbs"].items():
            bp = info["db_path"]
            # db_path 失效 fallback：meta 的 db_path 是绝对路径快照，project 目录
            # 搬迁后过期（migrate_project 会改写 meta，此分支兜底"搬了目录但 meta
            # 未改/旧 project"）。actual_name 本是 project 目录的 relpath，天然
            # 相对定位。
            if not os.path.isdir(bp):
                fallback = os.path.join(proj.db_path, actual)
                if os.path.isdir(fallback):
                    WARN(f"load_project: db_path stale ({bp}), fallback to "
                         f"current project layout: {fallback}")
                    bp = fallback
                    info["db_path"] = fallback
                else:
                    WARN(f"load_project: db db_path missing, skipping: {bp}")
                    continue
            try:
                proj._db_cache[actual] = load_db(bp)
                INFO(f"load_project: restored db '{actual}' ({info.get('db_path')})")
            except Exception as e:
                WARN(f"load_project: failed to load db '{actual}' at {bp}: {e}")
        DBG(f"load_project: restored {len(proj._db_cache)} dbs, "
            f"class={real_cls.__name__}")
        return proj

    # ── 内部辅助 ──────────────────────────────────────────────────────

    def _resolve_actual_name(self, name: str, latest: bool = False):
        """name → actual_name。

        - latest=False（默认）：按 actual_name（磁盘子目录名）精确匹配。
          重名递增产生的 'db.1'/'db.2' 是独立 actual_name，需显式传入。
        - latest=True：把 name 当 logical_name，取 created_at 最大者（同名最新版）。

        找不到返回 None。
        """
        dbs = self._meta["dbs"]
        if not latest:
            return name if name in dbs else None
        candidates = [(actual, info) for actual, info in dbs.items()
                      if info.get("logical_name") == name]
        if not candidates:
            return None
        candidates.sort(key=lambda x: x[1].get("created_at", 0), reverse=True)
        return candidates[0][0]

    # ── pickle 支持（仿 MapReduceJob，便于作 task 参数传递）────────────

    def __getstate__(self):
        return {
            "db_path": self.db_path,
            "meta": self._meta,
        }

    def __setstate__(self, state):
        self.db_path = state["db_path"]
        self._meta = state["meta"]
        self._meta_path = os.path.join(self.db_path, _PROJECT_META)
        self._db_cache = {}

    def __repr__(self):
        return (f"{type(self).__name__}(path={self.db_path!r}, "
                f"dbs={self.list_dbs()})")
