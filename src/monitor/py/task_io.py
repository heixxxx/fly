"""task_io —— worker 侧 task 执行窗口的 read/write IO 计时归属。

被归属的调用点（Python 层咽喉）：
  - storage/py/database.py 的 read_object / read_object_raw：
    record_read(full_name, nbytes, ms)
  - storage/py/database.py 的 write_object / _write_temp / write_object_raw：
    record_write(full_name, ms)（序列化+压缩+入队段）
  - agent/py/executor.py postprocess 的 drain_write_back：
    add_drain_ms(ms)（write-back 队列 flush 落盘段，计入 write_time）

线程模型：task 在 worker 的 Python 主线程串行执行，current 为模块级状态，
无锁。master 进程不执行 task，current 恒为 None，record_* 是空操作（一次
None 判断的调用开销）。

字节口径（docs/monitor-design.md）：
  - read：解压后字节（pickle 路径 len(data) 精确；C++ nanobind 对象路径
    Python 拿不到字节数，记 0）。
  - write：压缩后字节数由 C++ WriteRecord.size_bytes_ 汇总（TaskComplete
    的 written_objects），此处 write 计时不含字节数；object_io 明细表里
    write 行的 bytes 来自 Python 可得部分（pickle 路径）或 0。

数据流：executor 结束时 take_result(task_id) 取走聚合 + 对象级明细，
聚合四元组（read/write 时间与字节）随 TaskComplete/FailedMessage 上报；
明细经 MonitorTaskIoMessage 独立消息上报（master 落 object_io 表）。
"""
import time

_current = None          # 当前归属 task_id（None = 无 task 在跑，record 忽略）
_read_ms = 0.0
_read_bytes = 0
_write_ms = 0.0
_items = []              # 对象级明细 [{name, w, bytes, ms, epoch_ms}]


def set_current(task_id):
    """开始归属（executor execute 前）。重复调用重置累计。"""
    global _current, _read_ms, _read_bytes, _write_ms, _items
    _current = task_id
    _read_ms = 0.0
    _read_bytes = 0
    _write_ms = 0.0
    _items = []


def record_read(full_name, nbytes, ms):
    if _current is None:
        return
    global _read_ms, _read_bytes
    _read_ms += ms
    _read_bytes += nbytes
    _items.append({"name": full_name, "w": False, "bytes": nbytes,
                   "ms": ms, "epoch_ms": int(time.time() * 1000)})


def record_write(full_name, ms):
    """write 计时（序列化+压缩+入队段）。字节数由 C++ WriteRecord 汇总
    （TaskComplete.written_objects 的压缩后 size），此处不带 nbytes。"""
    if _current is None:
        return
    global _write_ms
    _write_ms += ms
    _items.append({"name": full_name, "w": True, "bytes": 0,
                   "ms": ms, "epoch_ms": int(time.time() * 1000)})


def add_drain_ms(ms):
    """write-back 队列 flush 落盘耗时计入 write_time（不产生明细行）。"""
    if _current is None:
        return
    global _write_ms
    _write_ms += ms


def take_result(task_id):
    """取走当前聚合与明细并清空归属（executor 收尾调用）。

    返回 dict：{read_ms, read_bytes, write_ms, items}，task_id 与 current
    不匹配时返回空聚合（防御异常路径）。
    """
    global _current, _read_ms, _read_bytes, _write_ms, _items
    if _current != task_id:
        return {"read_ms": 0.0, "read_bytes": 0, "write_ms": 0.0, "items": []}
    result = {"read_ms": _read_ms, "read_bytes": _read_bytes,
              "write_ms": _write_ms, "items": _items}
    _current = None
    _read_ms = 0.0
    _read_bytes = 0
    _write_ms = 0.0
    _items = []
    return result


def current_task_id():
    """测试/诊断用：当前归属 task_id。"""
    return _current
