"""EMIRProject 类身份与 Project 机制集成测试。

EMIRProject 是 EMIR（电压降 IR + 电迁移 EM）仿真分析的 Project 类型——
全部 13 个数据库及创建 API 归属它（docs/emir-data-flow.md §5）。flow 随
立项注册；本测试钉住三件地基：类身份与 meta class 字段（load_project
动态还原的依据）、meta 持久化与重绑定、flow 注册表与基类隔离。
"""
import json
import os

from fly.project import Project
from emir import EMIRProject


def test_class_identity():
    assert issubclass(EMIRProject, Project)
    # meta class 字段格式：module.qualname（load_project 按 importlib 还原）
    assert EMIRProject.__module__ == "emir.project.py.project"


def test_construct_persists_meta(tmp_path):
    proj = EMIRProject(str(tmp_path / "run"))
    assert proj.list_dbs() == []
    assert proj._meta["class"] == "emir.project.py.project.EMIRProject"

    meta_path = os.path.join(proj.db_path, "_PROJECT_META.json")
    assert os.path.isfile(meta_path), "meta must be persisted on construct"
    with open(meta_path, "r", encoding="utf-8") as f:
        persisted = json.load(f)
    assert persisted["class"] == "emir.project.py.project.EMIRProject"

    # 重新绑定已存在目录：读回而非重建（project_id 稳定）。
    proj2 = EMIRProject(proj.db_path)
    assert proj2._meta == proj._meta


def test_flows_registry_isolated(tmp_path):
    proj = EMIRProject(str(tmp_path / "run"))
    # 当前无 flow；list_flows 沿 MRO 收集，基类表为空 → []。
    assert proj.list_flows() == []
    # 未发生任何 register_flow 前，子类不得污染基类注册表。
    assert Project._flows == {}
    # flow 注册表是类级独立属性：首个 flow 注册时才建表（register_flow 语义）。
    assert "_flows" not in EMIRProject.__dict__
