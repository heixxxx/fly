from _fly_core import EXCoreConfig as Config, ex_core_get_config
from _fly_core import EXProcessInfo as ProcessInfo, ex_core_get_process_info
from _fly_core import ex_core_get_work_directory


def get_config():
    return ex_core_get_config()


def get_process_info():
    return ex_core_get_process_info()


def get_work_directory():
    """返回当前进程的工作目录路径（即 log_dir）。

    每个 QA 测试由 runqa 分配独立的 log_dir，测试用它拼接 DB 路径
    以获得进程隔离的工作空间，避免并发测试间互相干扰。
    """
    return ex_core_get_work_directory()


__all__ = ['Config', 'get_config', 'ProcessInfo', 'get_process_info',
           'get_work_directory']
