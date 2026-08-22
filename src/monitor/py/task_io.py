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

# IO 事件内存采样的门槛：耗时 >= 5ms 或读入 >= 256KB 才采一次 RSS。
# 依据（用户裁定原则）：读写快的 IO 本身不会产生过大的内存峰值和 IO 带宽
# 占用——对快 IO 采样无信息量，反而在热路径放大开销（cache 命中 µs 级读
# + 一次 /proc 读 ≈ 20µs = 数倍放大）。慢/大 IO 才是峰值与带宽的来源。
_IO_SAMPLE_MIN_MS = 5.0
_IO_SAMPLE_MIN_BYTES = 256 * 1024

_current = None          # 当前归属 task_id（None = 无 task 在跑，record 忽略）
_read_ms = 0.0
_read_bytes = 0
_write_ms = 0.0
_items = []              # 对象级明细 [{name, w, bytes, ms, epoch_ms}]
_io_peak_rss = 0         # IO 时刻见到的最大 RSS（read 返回/write 前后，对象必存活的时刻）


def _read_rss():
    """本进程 VmRSS（字节）。事件驱动采样点用（~20µs，相对慢 IO 可忽略）。"""
    try:
        with open("/proc/self/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) * 1024
    except OSError:
        pass
    return 0


def set_current(task_id):
    """开始归属（executor execute 前）。重复调用重置累计。"""
    global _current, _read_ms, _read_bytes, _write_ms, _items, _io_peak_rss
    _current = task_id
    _read_ms = 0.0
    _read_bytes = 0
    _write_ms = 0.0
    _items = []
    _io_peak_rss = 0


def record_read(full_name, nbytes, ms):
    if _current is None:
        return
    global _read_ms, _read_bytes, _io_peak_rss
    _read_ms += ms
    _read_bytes += nbytes
    _items.append({"name": full_name, "w": False, "bytes": nbytes,
                   "ms": ms, "epoch_ms": int(time.time() * 1000)})
    # 事件驱动内存采样（读结束点：读入数据必在内存——IO 型 task 内存峰值的
    # 天然观测点）。read 开始点无内存信息量（数据未到）、带宽已由本明细的
    # bytes+ms 承载（单次 IO 带宽可直接算），不重复采样。快 IO 按门槛豁免。
    if ms >= _IO_SAMPLE_MIN_MS or nbytes >= _IO_SAMPLE_MIN_BYTES:
        rss = _read_rss()
        if rss > _io_peak_rss:
            _io_peak_rss = rss


def record_write(full_name, ms):
    """write 计时（序列化+压缩+入队段，调用点在 write_object 的 finally——
    此时待写对象仍被用户引用持有）。字节数由 C++ WriteRecord 汇总。
    写时刻无条件采样（不设耗时门槛）：write 自身快 ≠ 进程内存小——用户
    持有的其它大对象与此刻写的对象无关，任意 write 时刻都是合法峰值观测点；
    write 无 cache 命中路径、频率远低于 read，采样开销可忽略。写结束点无
    内存信息量（数据已交出），带宽由 bytes+duration 承载。"""
    if _current is None:
        return
    global _write_ms, _io_peak_rss
    _write_ms += ms
    _items.append({"name": full_name, "w": True, "bytes": 0,
                   "ms": ms, "epoch_ms": int(time.time() * 1000)})
    rss = _read_rss()
    if rss > _io_peak_rss:
        _io_peak_rss = rss


def add_drain_ms(ms):
    """write-back 队列 flush 落盘耗时计入 write_time（不产生明细行）。"""
    if _current is None:
        return
    global _write_ms
    _write_ms += ms


def take_result(task_id):
    """取走当前聚合与明细并清空归属（executor 收尾调用）。

    返回 dict：{read_ms, read_bytes, write_ms, items, mem_peak_rss}，
    task_id 与 current 不匹配时返回空聚合（防御异常路径）。
    mem_peak_rss 为 IO 时刻最大 RSS（0 = 无足重 IO 或读 /proc 失败），由 C++
    侧补入 TaskResourceTracker 窗口（execute 返回后、end 结算前）。
    """
    global _current, _read_ms, _read_bytes, _write_ms, _items, _io_peak_rss
    if _current != task_id:
        return {"read_ms": 0.0, "read_bytes": 0, "write_ms": 0.0, "items": [],
                "mem_peak_rss": 0}
    result = {"read_ms": _read_ms, "read_bytes": _read_bytes,
              "write_ms": _write_ms, "items": _items,
              "mem_peak_rss": _io_peak_rss}
    _current = None
    _read_ms = 0.0
    _read_bytes = 0
    _write_ms = 0.0
    _items = []
    _io_peak_rss = 0
    return result


def current_task_id():
    """测试/诊断用：当前归属 task_id。"""
    return _current
