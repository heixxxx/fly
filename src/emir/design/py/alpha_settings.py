"""DSAlphaSettings — design db alpha 设置的声明式定义（2026-09-13 裁定）。

前七键迁移自 ds_flow.run_design_flow 边界的手工解析（``alpha.get`` +
isinstance 系列，已删除）：校验规则逐条保留、迁入各键 validator——拒绝
一律 user warn message（DSGN::0013 一次汇总，build_design_db 接线处发）
后保留默认值继续，不 raise。

键表（语义与默认值见各键 description；权威口径 docs/emir/design-db-plan.md
§6 alpha 键表）：
  density_bin_size / net_batch_size / lcp_name_arena / target_partitions /
  partition_count / partition_target_density / density_channel_weights /
  def_aggregate_threshold

target_partitions 的 '{x}x{y}' 解析细节仍留 C++ S8 路径（解析失败由
ds_decide_partitions 内发 DSGN::0013 回退）——Python validator 只做
str/None 类型级（None = 未设置；C++ 消费侧 None/空串同义）。
"""

import math

from emir.common import AlphaSetting, AlphaSettings


def _is_positive_int(value):
    """int 且非 bool 且 >= 1（bool 是 int 子类——伪装值一律拒绝）。"""
    return (isinstance(value, int) and not isinstance(value, bool)
            and value >= 1)


def _is_int(value):
    """int 且非 bool（0/负数不在此拒——未设置/回退语义在消费侧/C++）。"""
    return isinstance(value, int) and not isinstance(value, bool)


def _is_bool(value):
    return isinstance(value, bool)


def _is_target_partitions(value):
    """None（未设置）或 str；'{x}x{y}' 可解析性由 C++ S8 判别。"""
    return value is None or isinstance(value, str)


def _is_channel_weight(value):
    """单通道比重：非负有限数（NaN 比较恒 False 会放行、inf 非有限——
    一并显式拒绝；bool 伪装拒绝）。迁自 ds_flow._parse_channel_weights
    的逐 key 规则——经 AlphaSetting.key_validator 走合并语义：缺 key
    保留当前值、非法子键逐个回退（review 2026-09-13 修复：整键覆盖会
    丢缺 key 致消费侧 KeyError）。"""
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return False
    return not (math.isnan(value) or math.isinf(value) or value < 0)


class DSAlphaSettings(AlphaSettings):
    """design db 建库 alpha 设置（八键声明式，对象名 "alpha_settings" 写
    入 db，消费点 read_object 读回后 normalize 兜底）。"""

    density_bin_size = AlphaSetting(
        default=10, value_type="int", constraint=">= 1",
        validator=_is_positive_int,
        description="密度采样格边长（µm；提交侧 ×1000 换算 DBU——全局恒基"
                    "准 ㉝），缺省 10")
    net_batch_size = AlphaSetting(
        default=1000, value_type="int", constraint=">= 1",
        validator=_is_positive_int,
        description="S5b 网内容解析的批界网数（分批多阶段控内存峰值），缺"
                    "省 1000（裁定 ③）")
    lcp_name_arena = AlphaSetting(
        default=False, value_type="bool", constraint="",
        validator=_is_bool,
        description="R8d 裁定 55：名字伴生对象 DSBlockNames_<i> instance/"
                    "net 两 hasher id→name 侧 LCP 后缀压缩封口（容量换内"
                    "存），缺省 False 形态一零变化")
    target_partitions = AlphaSetting(
        default=None, value_type="None or str ('{x}x{y}')", constraint="",
        validator=_is_target_partitions,
        description="S8 直切分区形态 '{x}x{y}'（如 '4x3'，x/y ≥ 1；跳过分"
                    "区数计算与行列分布推导，切线仍按前缀和）；None = 未设"
                    "置。优先级 target_partitions > partition_count > "
                    "partition_target_density（2026-09-12 裁定 4）")
    partition_count = AlphaSetting(
        default=0, value_type="int", constraint="0 = unset",
        validator=_is_int,
        description="S8 总分区数 N（行列分布按负载自适应）；0 = 未设置。优"
                    "先级居中（裁定 4）")
    partition_target_density = AlphaSetting(
        default=150000, value_type="int", constraint="",
        validator=_is_int,
        description="S8 目标每分区合成负载（N = ceil(总负载/目标)；缺省 "
                    "150000——每分区约 10-20 万 leaf instance，裁定 4）。"
                    "非正值由 C++ 回退默认并 DSGN::0013 提醒")
    density_channel_weights = AlphaSetting(
        default={"instance": 6.0, "metal": 2.0, "via": 2.0},
        value_type="dict", constraint="instance/metal/via -> non-negative "
                                      "finite number",
        key_validator=_is_channel_weight,
        description="S8 密度通道比重（合成负载 = w_inst×inst + w_metal×Σ层"
                    "metal_l + w_via×Σ层 via_l）；**合并语义**——缺 key 用该"
                    "通道默认（6/2/2，2026-09-12 裁定 3），非法子键逐个回退"
                    "该子键默认并 DSGN::0013 提醒，未知子键提醒忽略（"
                    "review 2026-09-13：恢复旧逐 key 语义）")
    def_aggregate_threshold = AlphaSetting(
        default=67108864, value_type="int", constraint=">= 1",
        validator=_is_positive_int,
        description="S9 小 DEF 聚合阈值（字节，D26）：预估展开数据规模 = "
                    "DEF 文件大小 × 树上实例化次数，≥ 阈值的定义独占一个展"
                    "开任务，< 阈值的多个小定义按 def_paths 序贪心聚合到同"
                    "一任务（累计预估不超阈值）以减少任务数。缺省 64 MiB "
                    "保守标定：单任务内存峰值 = 估算数据的数倍（读取 + 展开"
                    "变换 + 分片缓冲），64 MiB 下可控于数百 MB；而常见秒级"
                    "任务调度开销（依赖解析 + 读写注册）相对 64 MiB I/O 可"
                    "忽略——再大的阈值聚合收益递减、内存风险线性上升")


def get_default_alpha_settings():
    """返回全新 DSAlphaSettings（deepcopy 语义：多次创建互不污染默认值，
    dict 型默认值 density_channel_weights 关键）。"""
    return DSAlphaSettings()
