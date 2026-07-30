"""message 系统端到端测试共享工具。

提供 master/worker 部署、message.log 路径解析、断言辅助等通用逻辑，
供各聚焦测试文件复用。
"""
import os
import time


def wait_for(condition, timeout=20.0, interval=0.3):
    """轮询等待条件成立，超时返回 False。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def get_message_log_content():
    """读取 master 进程写的 message.log 全文。

    master 进程的 Logger::resolve_log_dir 可能把 log_dir 转成 .latest 软链指向的
    子目录，也可能直接用（单进程/无历史轮转）。优先查 log_dir，再查 .latest 指向。
    """
    from fly import get_config
    log_dir = get_config().get_str("log_dir")
    candidates = [log_dir]
    latest = os.path.join(log_dir, "fly_log.latest")
    if os.path.islink(latest):
        candidates.append(os.path.join(log_dir, os.readlink(latest)))
    for c in candidates:
        p = os.path.join(c, "message.log")
        if os.path.isfile(p):
            with open(p, encoding="utf-8") as f:
                return f.read()
    raise AssertionError(f"message.log 不存在，查找过: {candidates}")


def get_master_debug_log():
    """读取 master 进程的 master.log（debug log），验证 message 是否也写本地 debug log。"""
    from fly import get_config
    log_dir = get_config().get_str("log_dir")
    candidates = [log_dir]
    latest = os.path.join(log_dir, "fly_log.latest")
    if os.path.islink(latest):
        candidates.append(os.path.join(log_dir, os.readlink(latest)))
    for c in candidates:
        p = os.path.join(c, "master.log")
        if os.path.isfile(p):
            with open(p, encoding="utf-8") as f:
                return f.read()
    raise AssertionError(f"master.log 不存在，查找过: {candidates}")


def get_worker_debug_log(worker_id):
    """读取某 worker 进程的 worker<N>.log（debug log）。"""
    from fly import get_config
    log_dir = get_config().get_str("log_dir")
    candidates = [log_dir]
    latest = os.path.join(log_dir, "fly_log.latest")
    if os.path.islink(latest):
        candidates.append(os.path.join(log_dir, os.readlink(latest)))
    fname = f"worker{worker_id}.log"
    for c in candidates:
        p = os.path.join(c, fname)
        if os.path.isfile(p):
            with open(p, encoding="utf-8") as f:
                return f.read()
    raise AssertionError(f"{fname} 不存在，查找过: {candidates}")


def count_summary_block(content):
    """从 message.log 内容中提取 summary 块文本（含标题到结尾分隔线）。"""
    start = content.find("========== Message Trigger Summary")
    if start < 0:
        return ""
    end = content.find("=============================================", start)
    return content[start:end] if end > start else content[start:]
