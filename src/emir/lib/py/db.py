"""LibDb — lib 库 db（role="lib"）。

保存 Liberty 单元库数据：顶层产出为单一整合容器 LIBLibrary
（cell 集合 + 库头单位与默认参数 + 查找表模板集，docs/emir-data-flow.md
§4 裁定 10），lib 阶段以 cell name 区分 cell；cell id 由 design db 建库时
读入 LIBLibrary 后分配（裁定 9）。
"""

from storage import Database


class LibDb(Database):
    """lib 库 db。role="lib"。"""

    role = "lib"

    # LIBLibrary 容器对象名（唯一正式对象）
    LIBRARY_OBJ = "LIBLibrary"

    def load_library(self):
        """读取 LIBLibrary 整合容器（EXLIBLibrary 对象）。"""
        return self.read_object(self.LIBRARY_OBJ)
