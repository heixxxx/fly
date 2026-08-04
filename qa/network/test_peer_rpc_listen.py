#!/usr/bin/env python3
"""最小测试：单 worker 内调 PeerChannelGroup.listen 确认端口绑定。"""
import os, sys
from fly import open_db, get_config, as_task

DB_PATH = os.path.join(get_config().get_str("log_dir"), "rpc_listen_db")


@as_task()
def listen_test(db):
    from agent import PeerChannelGroup
    print("[LISTEN_TEST] start", flush=True)
    group = PeerChannelGroup("test-listen")
    try:
        listener = group.listen(db)
        print(f"[LISTEN_TEST] OK port={listener.port}", flush=True)
        assert listener.port > 0, f"port should be >0, got {listener.port}"
        listener.close()
        print("[LISTEN_TEST] close OK", flush=True)
    except Exception as e:
        print(f"[LISTEN_TEST] FAILED: {e}", flush=True)
        import traceback; traceback.print_exc()
        raise


def main():
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)
    db = open_db(DB_PATH)
    get_config().set_int("fail_unscheduleable_tasks", 0)
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{"attributes": []}])
    assert master.wait_for_workers(1), "Worker failed to connect"
    listen_test(db)
    from fly import wait_tasks
    wait_tasks()
    print("[PASS] listen test")
    master.stop()


if __name__ == "__main__":
    main()
