"""回归测试 test_stream_after_async_write.py 的数据类辅助模块.

独立成模块（非 test_ 前缀辅助脚本约定）而非定义在测试脚本内：fly 用户脚本
被 exec 进临时命名空间（bootstrap.get_script_namespace），脚本内定义的类
__module__ 记为 "__main__"，而真实的 sys.modules["__main__"] 是 fly 入口
模块——标准 pickle 按引用寻址类时找不到，写读两侧均 PicklingError。类放进
可导入模块（fly 已把脚本所在目录注入 sys.path）后按真实模块名寻址，往返
成立。qa/storage 的既有脚本只写内置类型（str/dict），未曾踩到该限制。
"""


class PythonTaskData:
    def __init__(self, value=0, name="", tags=None):
        self.value = value
        self.name = name
        self.tags = tags or []
