from _fly_core import EXCoreConfig as Config, ex_core_get_config
from _fly_core import EXProcessInfo as ProcessInfo, ex_core_get_process_info


def get_config():
    return ex_core_get_config()


def get_process_info():
    return ex_core_get_process_info()


__all__ = ['Config', 'get_config', 'ProcessInfo', 'get_process_info']
