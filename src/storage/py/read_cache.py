import struct
import time
import threading
from collections import OrderedDict

_MAGIC_OFFSET = 0
_VERSION_OFFSET = 4
_PY_NAME_LEN_OFFSET = 5
_TOTAL_SIZE_OFFSET = 7
_FIXED_HEADER_SIZE = 20

_DEFAULT_MAX_BYTES = 1 << 30
_HARD_LIMIT_RATIO = 1.5
_PROTECTION_SEC = 30.0


def _extract_decompressed_size(compressed_data: bytes) -> int:
    if len(compressed_data) < _FIXED_HEADER_SIZE:
        return len(compressed_data)
    return struct.unpack_from('<Q', compressed_data, _TOTAL_SIZE_OFFSET)[0]


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
    def __init__(self, max_bytes: int = 0):
        if max_bytes <= 0:
            from _fly_core import ex_core_get_config
            max_bytes = int(ex_core_get_config().get_int("read_cache_size"))
            if max_bytes <= 0:
                max_bytes = _DEFAULT_MAX_BYTES
        self._max_bytes = max_bytes
        self._hard_limit = int(max_bytes * _HARD_LIMIT_RATIO)
        self._low: OrderedDict[str, _CacheEntry] = OrderedDict()
        self._high: OrderedDict[str, _CacheEntry] = OrderedDict()
        self._low_bytes = 0
        self._high_bytes = 0
        self._lock = threading.Lock()

    def get(self, key: str, level: str):
        with self._lock:
            if level == "low":
                entry = self._low.get(key)
                if entry is not None:
                    entry.touch()
                    self._low.move_to_end(key)
                    return entry.data
            elif level == "high":
                entry = self._high.get(key)
                if entry is not None:
                    entry.touch()
                    self._high.move_to_end(key)
                    return entry.data
        return None

    def put(self, key: str, level: str, data, size: int = 0):
        if size <= 0:
            if level == "low":
                raw = data[0] if isinstance(data, tuple) else data
                size = _extract_decompressed_size(raw)
            else:
                size = len(data) if isinstance(data, bytes) else 0

        with self._lock:
            if level == "low":
                if key in self._low:
                    old = self._low.pop(key)
                    self._low_bytes -= old.size
                self._low[key] = _CacheEntry(data, size)
                self._low_bytes += size
                self._low.move_to_end(key)
                self._evict(self._low, "_low_bytes")
            elif level == "high":
                if key in self._high:
                    old = self._high.pop(key)
                    self._high_bytes -= old.size
                self._high[key] = _CacheEntry(data, size)
                self._high_bytes += size
                self._high.move_to_end(key)
                self._evict(self._high, "_high_bytes")

    def remove(self, key: str, level=None):
        with self._lock:
            if level is None or level == "low":
                entry = self._low.pop(key, None)
                if entry:
                    self._low_bytes -= entry.size
            if level is None or level == "high":
                entry = self._high.pop(key, None)
                if entry:
                    self._high_bytes -= entry.size

    def clear(self):
        with self._lock:
            self._low.clear()
            self._high.clear()
            self._low_bytes = 0
            self._high_bytes = 0

    def _evict(self, cache: OrderedDict, bytes_attr: str):
        current_bytes = getattr(self, bytes_attr)
        if current_bytes <= self._max_bytes:
            return

        now = time.monotonic()
        entries = [(k, v) for k, v in cache.items()
                   if now - v.created_at >= _PROTECTION_SEC]

        if not entries and current_bytes > self._hard_limit:
            entries = list(cache.items())

        entries.sort(key=lambda kv: kv[1].score(now))

        for key, entry in entries:
            if current_bytes <= self._max_bytes:
                break
            del cache[key]
            current_bytes -= entry.size

        setattr(self, bytes_attr, current_bytes)


_cache = None


def get_read_cache() -> ReadCache:
    global _cache
    if _cache is None:
        _cache = ReadCache()
    return _cache
