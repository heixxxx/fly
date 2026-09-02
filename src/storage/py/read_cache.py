import os
import time
import threading
from collections import OrderedDict

_DEFAULT_MAX_BYTES = 1 << 30
_HARD_LIMIT_RATIO = 1.5
_PROTECTION_SEC = 30.0

# 污染哨兵开关（诊断工具）：FLY_CACHE_GUARD=1 时对全部条目做快照对比。
_GUARD_ENABLED = os.environ.get("FLY_CACHE_GUARD", "") == "1"


def _load_low_score_factor():
    # low 等级计分折扣（用户裁定：low 用较低基础分——同等热度下优先级
    # 低于 high，淘汰时沉底）。config 存百分比整数（25 = 0.25），默认 25。
    try:
        from _fly_core import ex_core_get_config
        pct = ex_core_get_config().get_int("low_score_factor")
        if pct > 0:
            return pct / 100.0
    except Exception:
        pass
    return 0.25


class _CacheEntry:
    __slots__ = ('data', 'size', 'last_access', 'read_count', 'created_at', 'level',
                 'guard_hash', 'guard_stack')

    def __init__(self, data, size, level="high"):
        self.data = data
        self.size = size
        self.level = level  # "low" / "high"（temp 池内恒 "temp"，不参与折扣）
        self.last_access = time.monotonic()
        self.read_count = 1
        self.created_at = self.last_access
        # 污染哨兵（诊断工具，FLY_CACHE_GUARD=1 启用）：populate 时快照
        # hash + 调用栈，get 命中时对比——检测"读后原地修改污染缓存"。
        self.guard_hash = None
        self.guard_stack = None
        if _GUARD_ENABLED:
            import pickle as _p
            import traceback as _tb
            try:
                self.guard_hash = hash(_p.dumps(data, protocol=5))
            except Exception:
                self.guard_hash = None
            if self.guard_hash is not None:
                self.guard_stack = "".join(_tb.format_stack()[-8:-1])

    def touch(self):
        self.last_access = time.monotonic()
        self.read_count += 1

    def score(self, now: float, low_factor: float = 1.0) -> float:
        age = max(now - self.last_access, 0.001)
        s = self.read_count / age
        if self.level == "low":
            s *= low_factor
        return s


class ReadCache:
    """解压 Python 对象缓存（2026-08-30 双池 + level 改造，用户裁定）。

    双池结构：
      - 主池（_main）：常规对象，low/high 等级标记——命中查询不分级，
        等级只影响淘汰优先级（low 计分乘 low_score_factor 折扣，同热度
        沉底先淘汰；命中不自动升级）。
      - temp 池（_temp）：temp 对象独立存放，容量 = 主池一半，机制同构
        （LRU+计分+保护期+硬限），池内不分级。
    单对象不设预算上限（用户裁定）——超预算对象照常入池，由淘汰兜底。

    历史：本缓存曾是纯 high tier（low-tier 压缩缓存归 C++ ObjectCache，
    §4.7 取消）；2026-08-30 起 low 重新定义为"完整对象 + 低淘汰优先级"
    等级（与 high 同池），Python 侧不再有压缩字节缓存。
    """

    def __init__(self, max_bytes: int = 0):
        if max_bytes <= 0:
            from _fly_core import ex_core_get_config
            max_bytes = int(ex_core_get_config().get_int("read_cache_size"))
            if max_bytes <= 0:
                max_bytes = _DEFAULT_MAX_BYTES
        self._max_bytes = max_bytes
        self._hard_limit = int(max_bytes * _HARD_LIMIT_RATIO)
        # temp 池：容量减半（用户裁定），硬限同比例。
        self._temp_max_bytes = max_bytes // 2
        self._temp_hard_limit = int(self._temp_max_bytes * _HARD_LIMIT_RATIO)
        self._low_factor = _load_low_score_factor()
        self._main: OrderedDict[str, _CacheEntry] = OrderedDict()
        self._main_bytes = 0
        self._temp: OrderedDict[str, _CacheEntry] = OrderedDict()
        self._temp_bytes = 0
        self._lock = threading.Lock()

    # 兼容别名（旧属性名 _high/_high_bytes——内部/测试引用迁移期）。
    @property
    def _high(self):
        return self._main

    @property
    def _high_bytes(self):
        return self._main_bytes

    def get(self, key: str, level=None):
        # 命中查询不分级（等级只影响淘汰）。level 参数兼容旧调用（忽略）。
        with self._lock:
            entry = self._main.get(key)
            if entry is None:
                entry = self._temp.get(key)
            if entry is not None:
                self._guard_check(key, entry)
                entry.touch()
                pool = self._main if key in self._main else self._temp
                pool.move_to_end(key)
                return entry.data
        return None

    @staticmethod
    def _guard_check(key, entry):
        # 哨兵：命中时对象 hash ≠ populate 时 → 读后原地修改污染缓存。
        if not _GUARD_ENABLED or entry.guard_hash is None:  # pragma: no cover（哨兵仅 FLY_CACHE_GUARD=1 子进程门控）
            return
        import pickle as _p
        try:
            cur = hash(_p.dumps(entry.data, protocol=5))
        except Exception:
            return
        if cur != entry.guard_hash:
            import sys
            print(f"\n[CACHE-GUARD] MUTATED key={key}\n"
                  f"--- populate 栈 ---\n{entry.guard_stack}\n"
                  f"--- 命中时 hash {entry.guard_hash} → {cur} ---\n",
                  file=sys.stderr, flush=True)

    def put(self, key: str, level: str, data, size: int = 0):
        # level: "low"/"high" → 主池；"temp" → temp 池。
        if size <= 0:
            size = len(data) if isinstance(data, (bytes, bytearray)) else 0

        if level == "temp":
            with self._lock:
                if key in self._temp:
                    old = self._temp.pop(key)
                    self._temp_bytes -= old.size
                self._temp[key] = _CacheEntry(data, size, "temp")
                self._temp_bytes += size
                self._temp.move_to_end(key)
                self._evict_pool(self._temp, is_temp=True)
            return

        with self._lock:
            if key in self._main:
                old = self._main.pop(key)
                self._main_bytes -= old.size
            # temp 池同名条目一并清（对象等级切换时防陈旧）。
            if key in self._temp:
                old = self._temp.pop(key)
                self._temp_bytes -= old.size
            self._main[key] = _CacheEntry(data, size, level)
            self._main_bytes += size
            self._main.move_to_end(key)
            self._evict_pool(self._main, is_temp=False)

    def remove(self, key: str, level=None):
        with self._lock:
            entry = self._main.pop(key, None)
            if entry:
                self._main_bytes -= entry.size
            entry = self._temp.pop(key, None)
            if entry:
                self._temp_bytes -= entry.size

    def clear(self):
        with self._lock:
            self._main.clear()
            self._main_bytes = 0
            self._temp.clear()
            self._temp_bytes = 0

    def _evict_pool(self, pool: OrderedDict, is_temp: bool):
        # 同构淘汰：保护期候选 → 硬限全候选 → score 升序淘汰至预算内。
        # temp 池无 level 折扣（池内不分级）。
        if is_temp:
            max_bytes, hard_limit = self._temp_max_bytes, self._temp_hard_limit

            def _bytes():
                return self._temp_bytes

            def _set_bytes(v):
                self._temp_bytes = v
        else:
            max_bytes, hard_limit = self._max_bytes, self._hard_limit

            def _bytes():
                return self._main_bytes

            def _set_bytes(v):
                self._main_bytes = v

        if _bytes() <= max_bytes:
            return

        now = time.monotonic()
        entries = [(k, v) for k, v in pool.items()
                   if now - v.created_at >= _PROTECTION_SEC]

        if not entries and _bytes() > hard_limit:
            entries = list(pool.items())

        entries.sort(key=lambda kv: kv[1].score(now, self._low_factor))

        for key, entry in entries:
            if _bytes() <= max_bytes:
                break
            del pool[key]
            _set_bytes(_bytes() - entry.size)


_cache = None


def get_read_cache() -> ReadCache:
    global _cache
    if _cache is None:
        _cache = ReadCache()
    return _cache
