import os
import struct
import tempfile
import threading

_DEFAULT_TEMP_MAX_BYTES = 512 * 1024 * 1024


class _TempEntry:
    __slots__ = ('compressed_data', 'py_name', 'size')

    def __init__(self, compressed_data: bytes, py_name: str, size: int):
        self.compressed_data = compressed_data
        self.py_name = py_name
        self.size = size


class TempStore:
    def __init__(self, max_bytes: int = 0):
        if max_bytes <= 0:
            try:
                from _fly_core import ex_core_get_config
                max_bytes = int(ex_core_get_config().get_int("temp_store_size"))
            except Exception:
                pass
            if max_bytes <= 0:
                max_bytes = _DEFAULT_TEMP_MAX_BYTES
        self._max_bytes = max_bytes
        self._mem: dict[str, _TempEntry] = {}
        self._mem_bytes = 0
        self._disk_files: dict[str, str] = {}
        self._lock = threading.Lock()
        self._tmp_dir = os.path.join(tempfile.gettempdir(), "fly_temp_objects")
        os.makedirs(self._tmp_dir, exist_ok=True)

    def put(self, key: str, compressed_data: bytes, py_name: str):
        size = len(compressed_data)
        with self._lock:
            self._remove_key(key)
            if self._mem_bytes + size <= self._max_bytes:
                self._mem[key] = _TempEntry(compressed_data, py_name, size)
                self._mem_bytes += size
            else:
                self._write_to_disk(key, compressed_data, py_name)

    def get(self, key: str):
        with self._lock:
            entry = self._mem.get(key)
            if entry is not None:
                return entry.compressed_data, entry.py_name
            disk_path = self._disk_files.get(key)
            if disk_path and os.path.exists(disk_path):
                return self._read_from_disk(disk_path)
        return None

    def has(self, key: str) -> bool:
        with self._lock:
            if key in self._mem:
                return True
            if key in self._disk_files:
                return os.path.exists(self._disk_files[key])
        return False

    def remove(self, key: str):
        with self._lock:
            self._remove_key(key)

    def cleanup_all(self):
        with self._lock:
            self._mem.clear()
            self._mem_bytes = 0
            for path in self._disk_files.values():
                self._delete_disk_file(path)
            self._disk_files.clear()
            if os.path.isdir(self._tmp_dir):
                import shutil
                shutil.rmtree(self._tmp_dir, ignore_errors=True)

    def _remove_key(self, key: str):
        entry = self._mem.pop(key, None)
        if entry:
            self._mem_bytes -= entry.size
        path = self._disk_files.pop(key, None)
        if path:
            self._delete_disk_file(path)

    def _write_to_disk(self, key: str, compressed_data: bytes, py_name: str):
        file_path = os.path.join(self._tmp_dir, f"{hash(key) & 0xFFFFFFFF:08x}.tmp")
        py_name_bytes = py_name.encode('utf-8')
        with open(file_path, 'wb') as f:
            f.write(struct.pack('<H', len(py_name_bytes)))
            f.write(py_name_bytes)
            f.write(compressed_data)
        self._disk_files[key] = file_path

    def _read_from_disk(self, file_path: str):
        with open(file_path, 'rb') as f:
            name_len = struct.unpack('<H', f.read(2))[0]
            py_name = f.read(name_len).decode('utf-8')
            compressed_data = f.read()
        return compressed_data, py_name

    def _delete_disk_file(self, path: str):
        try:
            if os.path.exists(path):
                os.unlink(path)
        except OSError:
            pass


_store = None


def get_temp_store() -> TempStore:
    global _store
    if _store is None:
        _store = TempStore()
    return _store
