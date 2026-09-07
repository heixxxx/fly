"""lib 模块对外函数层——供其他模块读取 lib db 的容器。

不含解析入口（解析是 lib db 的内部流程，lib_parse_lib_file 仅经 lib_export
供 lib_utils/测试使用，不对外暴露）。
"""

from .lib_db import LibDb


def load_lib_library(db):
    """读取 lib db 的 LIBLibrary 整合容器（EXLIBLibrary 对象）。

    供下游模块（design/power/current 等建库 flow）读取 cell 集合与数据表。
    """
    return db.read_object(LibDb.LIBRARY_OBJ)
