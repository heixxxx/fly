"""Test: launch_ssh_workers（SSH 启动 worker，localhost 自连环回）.

环境前置（一次性配置）：
  sudo apt-get install -y openssh-server && sudo service ssh start
  cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys   # chmod 700 ~/.ssh; 600 authorized_keys

场景：
1. 前置检测 ssh BatchMode 免密可用（不可用 → 明确 fail 附配置指引）
2. launch_ssh_workers 经 ssh localhost 启动 2 worker（其一带属性）→ 全部注册
3. 数据面：依赖链 write_data → read_data 在 ssh worker 上执行并校验
4. 生命周期：stop 后远端 worker 进程退干净（ShutdownMessage 自杀闭环）
"""
import os
import subprocess
import time

from _fly_log import INFO
from test import read_data, write_data
from fly import (open_db, wait_tasks, launch_ssh_workers,
                 wait_workers_registered, get_agent, get_config)

LOG_DIR = get_config().get_str("log_dir")
DB_PATH = os.path.join(LOG_DIR, "db")

import shutil
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)


def _ssh_ok():
    r = subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
         "localhost", "true"],
        capture_output=True, timeout=15)
    return r.returncode == 0


# 1. 前置检测：ssh 免密不可用则 fail 并给出一次性配置指引
assert _ssh_ok(), (
    "ssh localhost 免密不可用——launch_ssh_workers 测试要求本机 sshd 运行且密钥免密：\n"
    "  sudo apt-get install -y openssh-server && sudo service ssh start\n"
    "  cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys")
INFO("[1] ssh localhost BatchMode OK")

# 2. ssh 启动 2 worker（其一含属性，验证 CLI 透传）→ 注册
worker_ids = launch_ssh_workers(
    [{"host": "localhost"}, {"host": "localhost", "attributes": ["ssh_attr"]}])
assert isinstance(worker_ids, list) and len(worker_ids) == 2, \
    f"launch_ssh_workers should return 2 worker ids, got {worker_ids}"
assert len(set(worker_ids)) == 2, f"worker ids must be unique, got {worker_ids}"
assert wait_workers_registered(timeout=60) is True, \
    "ssh workers should register within 60s"
INFO(f"[2] {len(worker_ids)} ssh workers registered: {worker_ids}")

# 3. 数据面：依赖链经 ssh worker 执行
master = get_agent()
db = open_db(DB_PATH)
write_data(db, "ssh_key", "ssh_value")
read_data(db, "ssh_key", deps=[db.get_full_name("ssh_key")])
wait_tasks(timeout=30)
assert db.read_object("ssh_key") == "ssh_value"
INFO("[3] write/read data plane over ssh workers OK")

# 4. 生命周期：stop 广播 Shutdown → 远端 nohup worker 自杀，进程退干净
master.stop()
deadline = time.time() + 20
gone = False
while time.time() < deadline:
    r = subprocess.run(["pgrep", "-f",
                        f"--worker-id {worker_ids[0]}"],
                       capture_output=True)
    if r.returncode != 0:
        gone = True
        break
    time.sleep(0.5)
assert gone, "ssh worker process should exit after master stop (ShutdownMessage)"
INFO("[4] ssh workers exited cleanly after stop")

INFO("[PASS] test_launch_ssh_workers")
