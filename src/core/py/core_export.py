"""core 模块 export 层——_fly_core.so 符号唯一导入点。

Config/ProcessInfo 的 as 别名在此处完成（绑定符号名 → 公开符号名），
包根 ``from core import Config`` 经本层转发。
"""

from _fly_core import EXCoreConfig as Config, ex_core_get_config
from _fly_core import EXProcessInfo as ProcessInfo, ex_core_get_process_info
from _fly_core import ex_core_get_work_directory
