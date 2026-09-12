"""AlphaSettings 声明式基座单测（2026-09-13 裁定：alpha 项五要素声明式
定义，dev-rules §3）。

覆盖：
  - 默认独立性：两次 get_default_alpha_settings() 的 dict 型默认值
    （density_channel_weights）互不影响（实例值 = deepcopy(default)）
  - apply 三态：合法覆盖 / 非法拒绝保留默认 / 未知键收集（纯逻辑，不
    发 message——透出归 build 接线处按模块前缀处理）
  - None 默认项（target_partitions=None）合法 apply None
  - normalize：缺键旧对象补默认、未知属性丢弃（读回兜底，向前兼容）
  - pickle 往返（实例仅值 dict，描述符留在类层）
  - validator 规则抽查：DSAlphaSettings 七键迁移自 ds_flow 现有规则
    （None/类型/NaN/inf/非正/bool 伪装逐条对照）

模块内部单测：直连包符号（BUILD imports 指向 src 根 + 各 .so export 目
录，同 emir_project_test 先例——import emir 触发全链聚合加载）。
"""
import math
import pickle

from emir.common import AlphaSetting, AlphaSettings
from emir.design import DSAlphaSettings, get_default_alpha_settings


def test_default_independence():
    s1 = get_default_alpha_settings()
    s2 = get_default_alpha_settings()
    # dict 型默认值：改一个实例不影响另一个（deepcopy 语义）
    s1.density_channel_weights["instance"] = 99.0
    assert s2.density_channel_weights["instance"] == 6.0, \
        "second instance must keep pristine defaults"
    # 类层默认值本身不被污染
    s3 = get_default_alpha_settings()
    assert s3.density_channel_weights["instance"] == 6.0
    # apply 覆盖同样只作用于本实例
    s3.apply({"density_channel_weights":
              {"instance": 1.0, "metal": 1.0, "via": 1.0}})
    assert s3.density_channel_weights["metal"] == 1.0
    assert get_default_alpha_settings().density_channel_weights["metal"] == 2.0
    print("[OK] default independence (deepcopy semantics)")


def test_apply_three_states():
    s = get_default_alpha_settings()
    result = s.apply({"net_batch_size": 500,           # 合法 → 覆盖
                      "density_bin_size": "abc",       # 非法 → 拒绝保留默认
                      "ghost_key": 1})                 # 未知 → 收集
    assert result["unknown"] == ["ghost_key"], result
    assert set(result["rejected"]) == {"density_bin_size"}, result
    assert "abc" in result["rejected"]["density_bin_size"], result
    assert s.net_batch_size == 500, "valid key must override"
    assert s.density_bin_size == 10, "rejected key must keep default"
    # apply 为纯逻辑：不发 message（透出归调用方）——结果即全部副作用
    assert s.apply(None) == {"rejected": {}, "unknown": []}
    assert s.apply({}) == {"rejected": {}, "unknown": []}
    print("[OK] apply three states: accept / reject+keep / unknown")


def test_none_default_apply():
    s = get_default_alpha_settings()
    assert s.target_partitions is None, "None-default key must start None"
    # 显式 apply None 合法（类型级 validator 放行 None）
    result = s.apply({"target_partitions": None})
    assert not result["rejected"] and not result["unknown"], result
    assert s.target_partitions is None
    s.apply({"target_partitions": "4x3"})
    assert s.target_partitions == "4x3"
    print("[OK] None-default key (target_partitions) accepts None")


def test_normalize():
    s = get_default_alpha_settings()
    # 模拟旧对象：缺键 + 未知属性（白盒构造 _values 偏差）
    del s._values["density_bin_size"]
    s._values["ghost_key"] = 1
    s.normalize()
    assert s.density_bin_size == 10, "missing key must fall back to default"
    assert "ghost_key" not in s._values, "unknown attribute must be dropped"
    # normalize 后 apply/to_dict 面完整
    assert set(s.to_dict()) == set(DSAlphaSettings.setting_names())
    print("[OK] normalize: missing keys refilled, unknown attrs dropped")


def test_pickle_roundtrip():
    s = get_default_alpha_settings()
    s.apply({"density_bin_size": 5, "target_partitions": "2x2"})
    s2 = pickle.loads(pickle.dumps(s))
    assert isinstance(s2, DSAlphaSettings)
    assert s2.density_bin_size == 5 and s2.target_partitions == "2x2"
    # dict 值不共享存储（往返各自 deepcopy）
    s2.density_channel_weights["via"] = 7.0
    assert s.density_channel_weights["via"] == 2.0
    print("[OK] pickle roundtrip (values only, descriptors stay on class)")


def test_validator_rules():
    """七键 validator 规则抽查（迁移自 ds_flow run_design_flow 现有校验）。"""
    cases = [
        # (键, 值, 是否合法)
        ("density_bin_size", 10, True),
        ("density_bin_size", "10", False),          # str：int() 隐式转换不再放行
        ("density_bin_size", 0, False),             # 非正
        ("density_bin_size", -5, False),            # 负数
        ("density_bin_size", True, False),          # bool 伪装
        ("density_bin_size", 2.5, False),           # float 不接受（类型级）
        ("net_batch_size", 1000, True),
        ("net_batch_size", 1.5, False),
        ("net_batch_size", 0, False),
        ("lcp_name_arena", True, True),
        ("lcp_name_arena", False, True),
        ("lcp_name_arena", 1, False),               # 非 bool（含 truthy int）
        ("target_partitions", "4x3", True),
        ("target_partitions", None, True),          # None = 未设置
        ("target_partitions", 123, False),
        ("partition_count", 0, True),               # 0 = 未设置语义
        ("partition_count", 9, True),
        ("partition_count", True, False),
        ("partition_count", "9", False),
        ("partition_target_density", 150000, True),
        ("partition_target_density", True, False),
        ("partition_target_density", 15.0, False),
        # dict 型：NaN/inf/负数/bool/非数逐条拒绝（ds_flow NaN 恒 False
        # 放行坑位已封——NaN/inf 显式判别）
        ("density_channel_weights", {"instance": 6, "metal": 2, "via": 2}, True),
        # dict 型逐 key 键（review 2026-09-13 修复后）：非法子键的拒绝
        # 明细键名为 "density_channel_weights.<子键>"（逐个回退该子键
        # 默认），不再是整键拒绝——下方专测断言合并结果
        ("density_channel_weights", {"metal": 3}, True),   # 缺 key 合法（用默认）
        ("density_channel_weights", [1, 2], False),        # 顶层非 dict 整键拒绝
        ("density_channel_weights", None, False),
    ]
    for key, value, expect_ok in cases:
        s = get_default_alpha_settings()
        result = s.apply({key: value})
        ok = key not in result["rejected"]
        assert ok == expect_ok, \
            f"{key}={value!r}: expected {'accept' if expect_ok else 'reject'}, " \
            f"got {result}"
    print("[OK] validator rules spot-check (7 keys, migrated from ds_flow)")


def test_channel_weights_merge_semantics():
    """density_channel_weights 合并语义（review 2026-09-13 Blocker 修复）：
    缺 key 保留默认（旧 _parse_channel_weights 语义恢复——整键覆盖会丢
    缺 key 致消费侧 KeyError）；非法子键逐个回退；未知子键提醒忽略。"""
    # ① 缺 key：三键齐备、未给出的 key 保持默认
    s = get_default_alpha_settings()
    r = s.apply({"density_channel_weights": {"instance": 5.0}})
    assert not r["rejected"], r
    w = s.density_channel_weights
    assert w == {"instance": 5.0, "metal": 2.0, "via": 2.0}, w
    # ② 空 dict：等于默认三键（不丢 key）
    s = get_default_alpha_settings()
    s.apply({"density_channel_weights": {}})
    assert s.density_channel_weights == {"instance": 6.0, "metal": 2.0,
                                         "via": 2.0}
    # ③ 非法子键逐个回退：合法子键保留、非法子键回退默认 + 明细
    s = get_default_alpha_settings()
    r = s.apply({"density_channel_weights": {"instance": 5, "metal": -1}})
    assert "density_channel_weights.metal" in r["rejected"], r
    assert s.density_channel_weights == {"instance": 5.0, "metal": 2.0,
                                         "via": 2.0}
    # ④ 未知子键：提醒忽略、三键不变
    s = get_default_alpha_settings()
    r = s.apply({"density_channel_weights": {"foo": 1}})
    assert "density_channel_weights.foo" in r["rejected"], r
    assert s.density_channel_weights == {"instance": 6.0, "metal": 2.0,
                                         "via": 2.0}
    # ⑤ 非法子键规则抽查（NaN/inf/bool/类型——逐 key 明细）
    for bad in (float("nan"), float("inf"), -1, True, "6"):
        s = get_default_alpha_settings()
        r = s.apply({"density_channel_weights": {"via": bad}})
        assert "density_channel_weights.via" in r["rejected"], (bad, r)
        assert s.density_channel_weights["via"] == 2.0
    print("[OK] channel weights merge semantics (partial defaults kept)")


def test_descriptor_base():
    """基座机制抽查：五要素齐备 + __set_name__ 绑定 + get 兜底。"""
    class _Demo(AlphaSettings):
        rate = AlphaSetting(default=1.5, value_type="float",
                            constraint="positive", validator=None,
                            description="demo setting")
        # validator 抛异常 → rejected（原因 = 异常文本）

        @staticmethod
        def _boom(value):
            raise ValueError("boom")

        bad = AlphaSetting(default=0, value_type="int", validator=_boom)

    d = _Demo()
    assert d.rate == 1.5
    # 数据描述符：赋值 = 覆盖当前值（deepcopy，不遮蔽类层描述符）
    d.rate = 2.5
    assert d._values["rate"] == 2.5 and d.rate == 2.5
    d._values["bad"] = 1
    result = d.apply({"bad": 2})
    assert "bad" in result["rejected"], result
    assert "boom" in result["rejected"]["bad"], result
    assert d._values["bad"] == 1, "rejected key must keep current value"
    # 缺失值兜底默认（描述符 get）
    del d._values["rate"]
    assert d.rate == 1.5
    print("[OK] descriptor base: five elements, __set_name__, get fallback")


test_default_independence()
test_apply_three_states()
test_none_default_apply()
test_normalize()
test_pickle_roundtrip()
test_validator_rules()
test_channel_weights_merge_semantics()
test_descriptor_base()
print("[PASS] test_alpha_settings")
