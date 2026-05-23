from _fly_core import EXCoreConfig as Config, ex_core_get_config


def get_config():
    return ex_core_get_config()


__all__ = ['Config', 'get_config']
