import time
import threading
from collections import OrderedDict

_DEFAULT_MAX_BYTES = 1 << 30
_HARD_LIMIT_RATIO = 1.5
_PROTECTION_SEC = 30.0


class _CacheEntry:
    __slots__ = ('data', 'size', 'last_access', 'read_count', 'created_at')

    def __init__(self, data, size: int):
        self.data = data
        self.size = size
        self.last_access = time.monotonic()
        self.read_count = 1
        self.created_at = self.last_access

    def touch(self):
        self.last_access = time.monotonic()
        self.read_count += 1

    def score(self, now: float) -> float:
        age = max(now - self.last_access, 0.001)
        return self.read_count / age


class ReadCache:
    """High-tier LRU/LFU cache for deserialized Python objects.

    Low-tier (compressed bytes) caching is handled by the C++ ObjectCache
    (src/storage/cpp/object_cache.h) via FlyBufferPtr zero-copy sharing.
    This Python cache only stores live Python object references that C++
    std::any cannot hold.
    """

    def __init__(self, max_bytes: int = 0):
        if max_bytes <= 0:
            from _fly_core import ex_core_get_config
            max_bytes = int(ex_core_get_config().get_int("read_cache_size"))
            if max_bytes <= 0:
                max_bytes = _DEFAULT_MAX_BYTES
        self._max_bytes = max_bytes
        self._hard_limit = int(max_bytes * _HARD_LIMIT_RATIO)
        self._high: OrderedDict[str, _CacheEntry] = OrderedDict()
        self._high_bytes = 0
        self._lock = threading.Lock()

    def get(self, key: str, level: str = "high"):
        if level != "high":
            return None
        with self._lock:
            entry = self._high.get(key)
            if entry is not None:
                entry.touch()
                self._high.move_to_end(key)
                return entry.data
        return None

    def put(self, key: str, level: str, data, size: int = 0):
        if level != "high":
            return
        if size <= 0:
            size = len(data) if isinstance(data, bytes) else 0

        with self._lock:
            if key in self._high:
                old = self._high.pop(key)
                self._high_bytes -= old.size
            self._high[key] = _CacheEntry(data, size)
            self._high_bytes += size
            self._high.move_to_end(key)
            self._evict()

    def remove(self, key: str, level=None):
        with self._lock:
            if level is None or level == "high":
                entry = self._high.pop(key, None)
                if entry:
                    self._high_bytes -= entry.size

    def clear(self):
        with self._lock:
            self._high.clear()
            self._high_bytes = 0

    def _evict(self):
        if self._high_bytes <= self._max_bytes:
            return

        now = time.monotonic()
        entries = [(k, v) for k, v in self._high.items()
                   if now - v.created_at >= _PROTECTION_SEC]

        if not entries and self._high_bytes > self._hard_limit:
            entries = list(self._high.items())

        entries.sort(key=lambda kv: kv[1].score(now))

        for key, entry in entries:
            if self._high_bytes <= self._max_bytes:
                break
            del self._high[key]
            self._high_bytes -= entry.size


_cache = None


def get_read_cache() -> ReadCache:
    global _cache
    if _cache is None:
        _cache = ReadCache()
    return _cache
