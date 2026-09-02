"""PeerRpc 边缘 API：respond_failure→ERROR / notify_failure→FAILED /
非 bytes payload→TypeError / PeerChannelGroup pickle 往返 / listener.port /
connect 未监听端口→ConnectionError。

覆盖（2026-09 覆盖率批次 14 项之 6）：
  - master 进程本地：payload 类型校验（假 agent 直测）、pickle __reduce__
    只保留 group_id、PeerListener.port 属性
  - 双 worker 真连：服务端 respond_failure / notify_failure 的客户端状态码、
    connect 到无人监听端口 → ConnectionError（worker 内捕获写结果对象）
"""
import os
import pickle
import shutil

from _fly_log import INFO

from agent import PeerChannel, PeerListener, PeerChannelGroup, PeerRpcStatus
from fly import open_db, as_task, wait_tasks
from fly.runtime import get_agent
from test import qa_tmp

DB_PATH = qa_tmp("peer_rpc_edge_db")
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

# ── 1. master 进程本地：类型校验 / pickle / port（不触网）────────────
chan = PeerChannel(None, 1)
for bad_payload in ("str_payload", 123, None):
    try:
        chan.rpc(bad_payload)
        raise AssertionError("rpc non-bytes must raise TypeError")
    except TypeError as e:
        assert "bytes" in str(e), str(e)
try:
    chan.notify_failure("not_bytes")
    raise AssertionError("notify_failure non-bytes must raise TypeError")
except TypeError:
    pass

listener = PeerListener(None, 0)
for call in (
    lambda: listener.respond(1, 1, "not_bytes"),
    lambda: listener.respond_failure(1, 1, "not_bytes"),
    lambda: listener.notify_failure(1, "not_bytes"),
):
    try:
        call()
        raise AssertionError("listener non-bytes must raise TypeError")
    except TypeError:
        pass
INFO("[PASS] PeerChannel/PeerListener payload type guards")

# PeerChannel __del__ 兜底 close 对假 agent 安全（异常吞掉）
del chan

group = PeerChannelGroup("edge_gid_123")
restored = pickle.loads(pickle.dumps(group))
assert isinstance(restored, PeerChannelGroup)
assert restored.group_id == "edge_gid_123", "pickle 往返必须保留 group_id"
assert restored._temp_name() == "__fly_chan_edge_gid_123"
INFO("[PASS] PeerChannelGroup pickle round-trip (group_id preserved)")

assert PeerListener(None, 12345).port == 12345
INFO("[PASS] PeerListener.port property")

# ── 2. 双 worker 真连：失败通道 + ConnectionError ───────────────────
master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2, timeout=30), "workers must connect"

db = open_db(DB_PATH)
group_ok = PeerChannelGroup("edge_live")


@as_task()
def edge_server(db, group_id):
    """服务端：respond_failure 打第一个请求，notify_failure 打第二个。"""
    g = PeerChannelGroup(group_id)
    lst = g.listen(db)
    assert lst.port > 0
    conn1, rpc1, _, payload1 = lst.accept_one(timeout=30)
    lst.respond_failure(conn1, rpc1, b"biz_reason")
    conn2, rpc2, _, payload2 = lst.accept_one(timeout=30)
    lst.notify_failure(conn2, b"global_biz_fail")
    lst.close()


@as_task(inputs=lambda db, group_id: [])
def edge_client(db, group_id):
    """客户端：验证 ERROR/FAILED 状态码 + TypeError + ConnectionError。"""
    g = PeerChannelGroup(group_id)
    results = {}

    c = g.connect(db, timeout=30)
    status1, resp1 = c.rpc(b"req1", timeout=15)
    results["status1"] = status1
    results["reason1"] = resp1.decode("utf-8", "replace")
    status2, resp2 = c.rpc(b"req2", timeout=15)
    results["status2"] = status2

    try:
        c.rpc("not_bytes")
        results["type_guard"] = False
    except TypeError:
        results["type_guard"] = True

    c.close()

    # connect 到无人监听的保留端口 → conn_id==0 → ConnectionError
    g_bad = PeerChannelGroup("edge_bad")
    db.write_object(g_bad._temp_name(), {"host": "127.0.0.1", "port": 1})
    try:
        g_bad.connect(db, timeout=10)
        results["conn_err"] = False
    except ConnectionError:
        results["conn_err"] = True

    db.write_object("edge_results", results)


edge_server(db, group_ok.group_id)
edge_client(db, group_ok.group_id)
wait_tasks(timeout=90)

results = db.read_object("edge_results")
assert results["status1"] == PeerRpcStatus.ERROR, results
assert results["reason1"] == "biz_reason", results
assert results["status2"] == PeerRpcStatus.FAILED, results
assert results["type_guard"] is True, results
assert results["conn_err"] is True, results
INFO(f"[PASS] peer rpc edge statuses: {results}")

master.stop()
INFO("[PASS] test_peer_rpc_edge")
