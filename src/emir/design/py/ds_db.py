"""DesignDb — design db（role="design"）与建库流程入口 build_design_db。

保存物理设计库数据：tech lef → DSStack（层堆叠，裁定 ⑭ 独立对象）+ via
集合（VIARULE 展开模板，㉚）；cell lef → DSCell 集合 + 简化 pin（裁定
⑰）+ pin 几何独立对象（R4 键 = 全局 pin id）；S3 与 lib 库 merge（裁定
⑯）产出 lib 关联与 DSPinTables（逐 pin 落位）；DEF 头扫描 → block cell
（㉙ = DSCell + block_cell 位，DIEAREA 双存 bbox/polygon ㉞）+ port 几何 +
via cell 权威表（⑫ design_name:: 前缀）。DSDesign 容器的运行时专用字段
（pin 表/几何）不序列化（裁定 ⑱），业务方经 load_design_with 按需注入。

阶段链装配在 ds_flow.run_design_flow（流程实现与入口分离）。
约定（择简）：lef_paths[0] 为 tech lef（确立 DBU 基准与层堆叠），其余
为 cell lef。

debug 读库 API（2026-09-13 裁定，DesignDb 方法六个：get_cell/
get_instance/get_net/get_layer/convert_to_id/convert_to_name）：挂 db
句柄的交互定位面——内部完成 id↔name 转换（inst/net 走层级路径 name
mapper、其余走 hasher），映射定位分区后整分区对象按需加载 + 进程内
LRU 缓存（容量定值防撑爆）；方向/power/ground/clock 统计直接数连接
flags 位（S9 已存位，零推导）；pg 网（全局 pg 网 id 集 "pg_nets" 命中
——2026-09-13 重组裁定 O(1) 判定）不返回 connections
明细（数据量保护，只返回属性与计数概要）。
"""

from log import INFO

from fly import register_flow
from fly import UserDoc, Schema, document
from storage import Database

# A6 后由 emir/__init__ 聚合触发（先 project 后 design），此处包根已完成
# 初始化，可安全取 EMIRProject。
from emir.project import EMIRProject


class DesignDb(Database):
    """design db。role="design"。"""

    role = "design"

    # 正式持久化对象名（裁定 ⑭/⑰/⑱：Stack 独立对象 + design 容器 +
    # 两个运行时注入独立对象）+ per-DEF 实例产物（DSBlock_<i>：instances/
    # density/stats，按 def_paths 序号，block 名冗余在对象 block_name_
    # 字段）+ per-DEF 名字伴生对象（DSBlockNames_<i>：instance/net 两
    # hasher，R7 ㊵② 落盘拆分——按需加载其一）
    DESIGN_OBJ = "DSDesign"
    STACK_OBJ = "DSStack"
    PIN_TABLES_OBJ = "DSPinTables"
    PIN_GEOMETRY_OBJ = "DSPinGeometry"
    # S8 全局密度图（三通道独立对象，不进 DSDesign 容器——⑬ 大体量数据
    # 独立对象；S8 任务唯一写定）
    GLOBAL_DENSITY_OBJ = "global_density"
    # S7 跨块连接归并（并查集，仅 port 相连网；单对象——2026-09-13 裁定
    # 规模 port 级不分块；S7 汇总任务唯一写定）
    NET_UNION_OBJ = "net_union"
    # S10 全局校验报告（DSDesignCheckReport；损坏类 fatal 后不落盘——
    # freeze 依赖本对象，损坏库不冻结；S10 全局校验任务唯一写定）
    VERIFY_REPORT_OBJ = "verify_report"
    # 建库 alpha 设置对象（DSAlphaSettings，声明式五要素 2026-09-13 裁
    # 定；master 侧随建库写入，消费点 read_object 读回 + normalize 兜底）
    ALPHA_SETTINGS_OBJ = "alpha_settings"

    # S9 分区六类正式对象 kind（partition_obj_name 的 kind 入参；
    # 2026-09-14 拆分裁定：原四类中 GEOMETRY/NETS 各按 pg/信号拆分——
    # GEOMETRY_PG/NETS_PG 为 pg 侧独立对象（信号大文件与 pg 小文件物理
    # 分离，④ 提取首期专注电源网络时只加载 GEOMETRY_PG + NETS_PG 两个
    # 小对象），每分区一合并任务唯一写定）
    PARTITION_KINDS = ("GEOMETRY", "GEOMETRY_PG", "INSTANCES",
                       "INST_CONNECTIONS", "NETS", "NETS_PG")
    # 全局 pg 网 id 集（DSPgNetSet："pg_nets" 正式对象，S9 汇总任务唯一
    # 写定——power/ground 两 unordered_set，debug API is_pg 快速判定）
    PG_NETS_OBJ = "pg_nets"

    # id → partition 反向映射正式对象 kind（2026-09-13 debug 定位裁定：
    # inst/net 各一张全空间映射——段表 + 分段段对象按需加载）
    ID_MAP_KINDS = ("INST", "NET")
    # 段粒度 = 2^20 id/段（ds_id_map.h kIdMapSegmentBits 同值口径；段对
    # 象 = 定长 partition id 数组，uint32 × 2^20 ≈ 4 MiB/段）
    ID_MAP_SEGMENT_BITS = 20
    ID_MAP_SEGMENT_SIZE = 1 << ID_MAP_SEGMENT_BITS
    # debug 读库 LRU 容量定值（2026-09-13 裁定「容量上限防撑爆」实施
    # 定值）：段对象 ≈ 4 MiB/段、分区对象随分区数据量更大——两族各缓存
    # 8 个条目，容量上限 ≈ 数十 MiB 量级
    _SEGMENT_CACHE_CAPACITY = 8
    _PARTITION_CACHE_CAPACITY = 8

    @staticmethod
    def partition_obj_name(xp: int, yp: int, kind: str) -> str:
        """S9 分区正式对象名（PART_{xp}_{yp}.{kind}；kind ∈
        PARTITION_KINDS，每分区一合并任务唯一写定；GEOMETRY_PG/NETS_PG
        为 pg 侧独立对象——2026-09-14 拆分裁定。`.` 作层级分隔——对象名
        字符集仅允许字母/数字/`.`/`_`，2026-09-14 裁定）。"""
        return f"PART_{xp}_{yp}.{kind}"

    @staticmethod
    def id_map_index_obj_name(kind: str) -> str:
        """id→partition 映射段表对象名（id_partition_map.{kind}；kind ∈
        ID_MAP_KINDS——轻对象，非空段起始 id 升序表）。"""
        return f"id_partition_map.{kind}"

    @staticmethod
    def id_map_segment_obj_name(kind: str, seg_index: int) -> str:
        """id→partition 映射段对象名（id_partition_map.{kind}.S{seg_index}
        ——段号 = 段起始 id >> ID_MAP_SEGMENT_BITS；段对象按需加载）。"""
        return f"id_partition_map.{kind}.S{seg_index}"

    @staticmethod
    def id_slice_obj_name(id_slice_prefix: str, pid: int, kind: str) -> str:
        """id→partition 映射分区片段临时对象名（S9 每分区合并任务写本区
        片段，映射汇总任务 merge 后清理；kind ∈ ID_MAP_KINDS）。"""
        return f"{id_slice_prefix}{kind}{pid}"

    @staticmethod
    def block_obj_name(index: int) -> str:
        """per-DEF 实例产物对象名（DSBlockBuildData，按 def_paths 序号；
        不含名字——名字在 names_obj_name 伴生对象）。"""
        return f"DSBlock_{index}"

    @staticmethod
    def names_obj_name(index: int) -> str:
        """per-DEF 名字伴生对象名（DSBlockNames，按 def_paths 序号，
        与 block_obj_name 同序对齐；R7 ㊵②）。"""
        return f"DSBlockNames_{index}"

    @staticmethod
    def net_obj_name(index: int) -> str:
        """per-DEF 网内容产物对象名（DSNetBuildData，按 def_paths 序号，
        与 block_obj_name 同序对齐）。"""
        return f"DSNet_{index}"

    def load_design(self):
        """读取 DSDesign 容器（EXDSDesign 对象，不含运行时注入字段）。"""
        return self.read_object(self.DESIGN_OBJ)

    def load_stack(self):
        """读取 DSStack 层堆叠（EXDSStack 对象）。"""
        return self.read_object(self.STACK_OBJ)

    # ── debug 读库 API（2026-09-13 裁定：挂 db 句柄的交互定位面；六个
    #    方法 + 惰性缓存辅助。返回 dict 一律 id + name 成对的可读字段）──

    # 枚举字符串化表（DSPinType / DSPinDirection / DSPlacementStatus /
    # DSLayerType / DSDirection；与 ds_types.h 枚举值序一一对应）
    _PIN_TYPE_NAMES = ("SIGNAL", "POWER", "GROUND", "CLOCK")
    _PIN_DIRECTION_NAMES = ("INPUT", "OUTPUT", "INOUT")
    _PLACEMENT_NAMES = ("UNPLACED", "FIXED", "COVER", "PLACED")
    _LAYER_TYPE_NAMES = ("ROUTING", "CUT")
    _LAYER_DIRECTION_NAMES = ("HORIZONTAL", "VERTICAL", "NONE")
    # DSCell 种类 flags 位 → 业务名（CM_FLAGS 位序）
    _CELL_FLAG_NAMES = ("fake_cell", "std_cell", "lef_cell", "lib_cell",
                        "macro_cell", "block_cell", "polygon")

    def _lru_put(self, attr, capacity, key, value):
        """进程内简单 LRU（OrderedDict；容量上限防撑爆——2026-09-13 裁
        定实施定值）。cache 属性惰性初始化（Database 构造不归本类管）。"""
        cache = getattr(self, attr, None)
        if cache is None:
            import collections
            cache = collections.OrderedDict()
            setattr(self, attr, cache)
        if key in cache:
            cache.move_to_end(key)
        cache[key] = value
        while len(cache) > capacity:
            cache.popitem(last=False)
        return value

    def _lru_get(self, attr, key):
        cache = getattr(self, attr, None)
        if cache is None or key not in cache:
            return None
        cache.move_to_end(key)
        return cache[key]

    def _cached_design(self):
        """DSDesign 容器缓存（冻结库只读——单条目缓存足够）。"""
        design = self._lru_get("_design_cache", "design")
        if design is None:
            design = self._lru_put("_design_cache", 1, "design",
                                   self.read_object(self.DESIGN_OBJ))
        return design

    def _cached_stack(self):
        stack = self._lru_get("_stack_cache", "stack")
        if stack is None:
            stack = self._lru_put("_stack_cache", 1, "stack",
                                  self.read_object(self.STACK_OBJ))
        return stack

    def _cached_pg_nets(self):
        """全局 pg 网 id 集缓存（DSPgNetSet；冻结库只读——单条目缓存足
        够）。get_net 的 is_pg 快速判定（O(1)）数据源。"""
        pg = self._lru_get("_pg_nets_cache", "pg_nets")
        if pg is None:
            pg = self._lru_put("_pg_nets_cache", 1, "pg_nets",
                               self.read_object(self.PG_NETS_OBJ))
        return pg

    def _ensure_mappers(self):
        """层级 name mapper 惰性加载（读全部 DSBlockNames_<i> 伴生对象 →
        组装 instance/net 两维度 mapper——与 load_name_mapper 同构，挂在
        debug API 生命周期内缓存）。"""
        mappers = self._lru_get("_mapper_cache", "mappers")
        if mappers is not None:
            return mappers
        from .ds_export import ds_make_name_mapper
        design = self._cached_design()
        names_list = []
        i = 0
        while True:
            try:
                names_list.append(self.read_object(self.names_obj_name(i)))
            except Exception:
                break  # 连续段结束（per-DEF 伴生对象序 = def_paths 序）
            i += 1
        mappers = self._lru_put("_mapper_cache", 1, "mappers", (
            design,
            ds_make_name_mapper(design, names_list, 0),   # instance 维度
            ds_make_name_mapper(design, names_list, 1),   # net 维度
        ))
        return mappers

    def _partition_xy(self, partition_id):
        """partition id → (xp, yp) 网格坐标（DSDesign.partitions_ 表；
        未命中 None）。"""
        design = self._cached_design()
        for i in range(design.partition_count):
            p = design.partition_at(i)
            if p.partition_id == partition_id:
                return (p.xp, p.yp)
        return None

    def _load_partition_obj(self, xp, yp, kind):
        """分区正式对象按需加载 + LRU（键 = (xp, yp, kind)）。"""
        key = (xp, yp, kind)
        obj = self._lru_get("_partition_cache", key)
        if obj is None:
            obj = self._lru_put("_partition_cache",
                                self._PARTITION_CACHE_CAPACITY, key,
                                self.read_object(
                                    self.partition_obj_name(xp, yp, kind)))
        return obj

    def _locate_partition(self, map_kind, obj_id):
        """id → partition 定位（段表 → 段对象按需加载 → pids[id − start]
        ；返回 (partition id, (xp, yp))；映射无条目 = 无分区副本，None）。"""
        index = self._lru_get("_index_cache", map_kind)
        if index is None:
            index = self._lru_put(
                "_index_cache", 2, map_kind,
                self.read_object(self.id_map_index_obj_name(map_kind)))
        seg_start = index.find_segment_start(obj_id)
        if seg_start is None:
            return None
        seg_index = seg_start >> self.ID_MAP_SEGMENT_BITS
        key = (map_kind, seg_index)
        segment = self._lru_get("_segment_cache", key)
        if segment is None:
            segment = self._lru_put(
                "_segment_cache", self._SEGMENT_CACHE_CAPACITY, key,
                self.read_object(
                    self.id_map_segment_obj_name(map_kind, seg_index)))
        pid = segment.partition_of(obj_id)
        if pid is None:  # 空洞（kIdMapNoPartition 导出面已转 None）
            return None
        xy = self._partition_xy(pid)
        if xy is None:
            return None
        return pid, xy

    def _to_id(self, kind, value):
        """debug API 入参 id/name 双形态归一：int 原样；str 经转换（inst/
        net 走层级路径 mapper、其余走 hasher）。转换失败返回 None。"""
        if isinstance(value, int):
            return value
        return self.convert_to_id(kind=kind, name=value)

    def _conn_summary(self, conn, want_detail):
        """连接 flags 概要（统计数位口径：driver/receiver/hybrid 三分类
        互斥单列，power/ground/clock 端点直接数位——零推导）。"""
        summary = {
            "is_port": conn.is_port, "is_driver": conn.is_driver,
            "is_receiver": conn.is_receiver, "is_power": conn.is_power,
            "is_ground": conn.is_ground, "is_clock": conn.is_clock,
        }
        if want_detail:
            summary["pin_id"] = conn.pin_id
        return summary

    def get_cell(self, cell, with_pins: bool = True):
        """查 cell 全部信息（入参 id 或 name 自动判别；未命中返回 None）。

        返回 dict：id/name/来源（library_name/def_path）/bbox 与 origin
        （DBU）/尺寸（bbox 派生 width/height）/种类 flags 名单/pin 数；
        with_pins=True 时附 pins 列表——每 pin name/type/direction/port
        位（pin 名经容器 pin hasher 组合键反查，R7 ㊱）。
        """
        design = self._cached_design()
        if isinstance(cell, int):
            cid = cell
            if not 0 <= cid < design.cell_count:
                return None
            name = design.cell_name_by_id(cid)
            if name is None:
                return None  # 稀疏落位空洞（fake cell id 间隙）
        else:
            cid = design.cell_id_by_name(cell)
            if cid is None:
                return None
            name = cell
        c = design.get_cell(cid)
        # 种类 flags → 业务名（绑定面 is_* 为只读 property/bool）
        flags = [flag for flag, on in zip(
            self._CELL_FLAG_NAMES,
            (c.is_fake_cell, c.is_std_cell, c.is_lef_cell, c.is_lib_cell,
             c.is_macro_cell, c.is_block_cell, c.is_polygon)) if on]
        info = {
            "id": cid, "name": name,
            "library_name": c.library_name, "def_path": c.def_path,
            "bbox": c.bbox, "origin": (c.origin_x, c.origin_y),
            "width": c.width, "height": c.height,
            "flags": flags, "pin_count": c.pin_count,
        }
        if with_pins:
            pins = []
            for i in range(c.pin_count):
                p = c.pin_at(i)
                pins.append({
                    "pin_id": p.pin_id,
                    "name": design.pin_name_of(p.pin_id),
                    "type": self._PIN_TYPE_NAMES[p.type],
                    "direction": self._PIN_DIRECTION_NAMES[p.direction],
                    "is_port": p.is_port,
                })
            info["pins"] = pins
        return info

    def get_instance(self, inst):
        """查 instance（入参 global id 或层级全路径名自动判别；未命中
        返回 None——无 primary 分区副本的实例（UNPLACED / root 占位 id 0）
        无分区数据可读，同样返回 None）。

        返回 dict：instance id/层级全路径名/全局坐标 pos 与 orient/
        placement_status/primary partition id/来源 cell（id + name）/
        instance connections（端点 net id + 层级名 + flags 概要——跟随
        本分区 INST_CONNECTIONS 副本）。
        """
        design, inst_mapper, net_mapper = self._ensure_mappers()
        gid = self._to_id("inst", inst)
        if gid is None:
            return None
        located = self._locate_partition("INST", gid)
        if located is None:
            return None
        partition_id, (xp, yp) = located
        name = inst_mapper.get_full_name(gid)
        instances = self._load_partition_obj(xp, yp, "INSTANCES")
        try:
            inst_obj = instances.get(gid)
        except KeyError:
            return None  # 映射与分区产物失配的防御（正常建库不触发）
        cell_id = inst_obj.cell_id
        connections = []
        inst_conns = self._load_partition_obj(xp, yp, "INST_CONNECTIONS")
        for conn in inst_conns.connections_of(gid):
            connections.append({
                "net_id": conn.net_global_id,
                "net_name": net_mapper.get_full_name(conn.net_global_id),
                "pin_id": conn.pin_id,
                "pin_name": design.pin_name_of(conn.pin_id),
                **self._conn_summary(conn, want_detail=False),
            })
        return {
            "id": gid, "name": name,
            "pos": (inst_obj.pos_x, inst_obj.pos_y),
            "orient": inst_obj.orient,
            "placement_status": self._PLACEMENT_NAMES[
                inst_obj.placement_status],
            "primary_partition_id": partition_id,
            "cell_id": cell_id,
            "cell_name": design.cell_name_by_id(cell_id),
            "connections": connections,
        }

    def get_net(self, net):
        """查 net（入参 global id 或层级路径名自动判别；未命中——无几何
        副本不落分区的网（无几何——含 root 首网），或**有几何但无任何
        连接的悬浮网**（无连接则无聚合对象，几何仍在 GEOMETRY 对
        象）、id 0（OBS 桶专属位、非真网 id——2026-09-14 裁定）、
        或 special 无 USE 网（入 NETS_PG 但全局 pg 集不含——is_pg
        路由至信号侧不命中）——一律返回显式 None（2026-09-13 review 修正：旧兜底
        曾静默降级为空概要；这些网的真实连接见 S5b 产物与
        net_union）。

        返回 dict：net id/层级名/use 属性（DEF USE 八值字符串，缺省
        SIGNAL——2026-09-13 重组裁定随 DSNet.use_ 直取）/connections 数
        量/driver/receiver/hybrid 数（三分类互斥单列）与 power/ground/
        clock/port 端点计数——统计直接数连接 flags 位（S9 已存位，零推
        导）。**pg 网（全局 pg 网 id 集命中）不返回 connections 明细**
        （数据量保护——只返回属性与计数概要）；非 pg 网附 connections 列
        表：端点实例 id + 层级名 + pin id + pin 名 + flags 概要（port 条
        目如实呈现——端点实例 = 块实例层级名 + port 名）。is_pg 判定经
        全局 pg 网 id 集（O(1) 快速路径，DSPgNetSet），并据此路由加载
        NETS_PG / NETS 对应侧对象单表查（2026-09-14 拆分裁定）。
        """
        _, _, net_mapper = self._ensure_mappers()
        gid = self._to_id("net", net)
        if gid is None:
            return None
        located = self._locate_partition("NET", gid)
        if located is None:
            return None
        partition_id, (xp, yp) = located
        name = net_mapper.get_full_name(gid)
        # is_pg 经全局 pg 网 id 集（2026-09-13 重组裁定：快速路径——原
        # use 字符串比较口径与集内容一致：pg 集 = use POWER/GROUND 的网
        # ；2026-09-14 拆分裁定：is_pg 即路由键——pg 网读 NETS_PG 对象、
        # 信号网读 NETS 对象，各自单表查）
        is_pg = self._cached_pg_nets().is_pg(gid)
        net_conns = self._load_partition_obj(
            xp, yp, "NETS_PG" if is_pg else "NETS")
        net_obj = net_conns.net_of(gid)
        if net_obj is None:
            # 显式口径（review 2026-09-13 修正）：NETS 产物无此网记录
            # = 有几何但无任何连接的悬浮网（DSNet 不落表——无连接则无
            # 聚合对象；几何仍在 GEOMETRY 对象）或映射键混叠——返回
            # None 而非静默降级（旧兜底会把无连接 POWER 网误报为
            # SIGNAL 非 pg）
            return None
        use = net_obj.use
        conns = net_obj.connections
        counts = {"driver": 0, "receiver": 0, "hybrid": 0, "power": 0,
                  "ground": 0, "clock": 0, "port": 0}
        detail = []
        # pg 网（pg 集命中）不返回 connections 明细（数据量保护红线）——
        # 只数位出概要；非 pg 附明细（端点实例名 + pin 名 name 化）
        show_detail = not is_pg
        if show_detail:
            design, inst_mapper, _ = self._ensure_mappers()
        for conn in conns:
            s = self._conn_summary(conn, want_detail=False)
            if s["is_driver"] and s["is_receiver"]:
                counts["hybrid"] += 1
            elif s["is_driver"]:
                counts["driver"] += 1
            elif s["is_receiver"]:
                counts["receiver"] += 1
            if s["is_power"]:
                counts["power"] += 1
            if s["is_ground"]:
                counts["ground"] += 1
            if s["is_clock"]:
                counts["clock"] += 1
            if s["is_port"]:
                counts["port"] += 1
            if show_detail:
                detail.append({
                    "instance_id": conn.inst_id,
                    "instance_name": inst_mapper.get_full_name(
                        conn.inst_id),
                    "pin_id": conn.pin_id,
                    "pin_name": design.pin_name_of(conn.pin_id),
                    **s,
                })
        info = {
            "id": gid, "name": name, "use": use,
            "is_pg": is_pg,
            "primary_partition_id": partition_id,
            "connection_count": len(conns), **{f"{k}_count": v
                                               for k, v in counts.items()},
        }
        if show_detail:
            info["connections"] = detail
        return info

    def get_layer(self, layer):
        """查层（入参 id 或 name 自动判别；未命中返回 None）。

        返回 dict：id/name/type/direction/width（缺省宽）/pitch/spacing
        表/min_area（DBU）。
        """
        stack = self._cached_stack()
        if isinstance(layer, int):
            lid = layer
            if not 0 <= lid < stack.layer_count:
                return None
        else:
            lid = stack.find_layer(layer)  # 未命中 None（导出面已转换）
            if lid is None:
                return None
        lay = stack.layer_by_id(lid)
        return {
            "id": lid, "name": lay.name,
            "type": self._LAYER_TYPE_NAMES[lay.type],
            "direction": self._LAYER_DIRECTION_NAMES[lay.direction],
            "width": lay.default_width, "pitch": lay.pitch,
            "spacing": [lay.spacing_at(i)
                        for i in range(lay.spacing_count)],
            "min_area": lay.min_area,
        }

    _KIND_KEYS = ("cell", "pin", "layer", "via_cell", "inst", "net")

    def convert_to_id(self, kind=None, name=None, **kwargs):
        """name → id（双向转换的正向；未命中返回 None）。

        两种形态：convert_to_id("cell", "INV_X1") /
        convert_to_id(kind="cell", name="INV_X1")，或 kwargs 直写形态
        convert_to_id(cell="INV_X1") / convert_to_id(inst="top/i1/u1")。
        kind ∈ {cell, pin, layer, via_cell, inst, net}：inst/net 走层级
        路径 name mapper（'/' 分隔全路径），cell/layer/via_cell 走各自
        hasher，pin 走组合键 "cell_name/pin_name"（D1）。
        """
        if kind is not None:
            if name is None or kwargs:
                raise TypeError(
                    "convert_to_id: kind/name 与 kwargs 形态不可混用")
            kwargs = {kind: name}
        elif len(kwargs) != 1:
            raise TypeError(
                "convert_to_id: 需要一个 kind 键（cell/pin/layer/via_cell/"
                f"inst/net）或 kind+name 对，got {kwargs!r}")
        (k, v), = kwargs.items()
        if k not in self._KIND_KEYS:
            raise TypeError(f"convert_to_id: unknown kind '{k}'")
        if v is None:
            return None
        if k == "cell":
            return self._cached_design().cell_id_by_name(v)
        if k == "pin":
            # pin 组合键（D1）拆分直查（name = "cell_name/pin_name"）
            cell_name, _, pin_name = str(v).rpartition("/")
            if not cell_name:
                raise ValueError(
                    f"convert_to_id: pin name must be 'cell_name/pin_name'"
                    f" combined key, got {v!r}")
            return self._cached_design().pin_id_by_name(cell_name, pin_name)
        if k == "layer":
            return self._cached_stack().find_layer(v)  # 未命中 None
        if k == "via_cell":
            return self._cached_design().via_cell_id_by_name(v)
        _, inst_mapper, net_mapper = self._ensure_mappers()
        mapper = inst_mapper if k == "inst" else net_mapper
        # 未命中 None（导出面已将 kInvalidId 哨兵转换）
        return mapper.get_global_id(v)

    def convert_to_name(self, kind=None, id=None, **kwargs):
        """id → name（双向转换的反向；未命中返回 None）。

        与 convert_to_id 对称：convert_to_name("cell", 0) /
        convert_to_name(kind="cell", id=0)，或用户示例 kwargs 直写形态
        convert_to_name(inst=10) / convert_to_name(net=0)。inst/net 返回
        层级全路径名，pin 返回组合键全名 "cell_name/pin_name"，其余返回
        登记名。
        """
        if kind is not None:
            if id is None or kwargs:
                raise TypeError(
                    "convert_to_name: kind/id 与 kwargs 形态不可混用")
            kwargs = {kind: id}
        elif len(kwargs) != 1:
            raise TypeError(
                "convert_to_name: 需要一个 kind 键（cell/pin/layer/"
                f"via_cell/inst/net）或 kind+id 对，got {kwargs!r}")
        (k, v), = kwargs.items()
        if k not in self._KIND_KEYS:
            raise TypeError(f"convert_to_name: unknown kind '{k}'")
        if v is None:
            return None
        if k == "cell":
            return self._cached_design().cell_name_by_id(v)
        if k == "pin":
            # pin 组合键全名（R7 ㊱：pin 自身不存名，与组合键入参对称）
            key = self._cached_design().pin_key_by_id(v)
            return key or None
        if k == "layer":
            stack = self._cached_stack()
            if not 0 <= v < stack.layer_count:
                return None
            return stack.layer_by_id(v).name
        if k == "via_cell":
            return self._cached_design().via_cell_name_by_id(v)
        _, inst_mapper, net_mapper = self._ensure_mappers()
        mapper = inst_mapper if k == "inst" else net_mapper
        name = mapper.get_full_name(v)
        return name or None


build_design_db_doc = UserDoc(
    "构建 design db：解析 tech lef（层堆叠/DBU 基准/通孔定义）+ 多份 cell "
    "lef（macro/简化 pin/禁布区/pin 几何）+ 多份 DEF（DIEAREA/port/通孔"
    "定义），并与 lib 库 db 按 cell 名 merge（填 lib 字段与关联、提取功耗/"
    "时序表）。lef_paths[0] 按 tech lef 解析，其余按 cell lef 解析。")
build_design_db_doc.add_param("name",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc="db 子目录名 + Project 内部 key（重名自动递增）")
build_design_db_doc.add_param("def_paths",
    schema=Schema(list, check=lambda ps: all(isinstance(p, str) and p for p in ps),
                  error="must be a list of non-empty file paths"),
    required=True, desc="DEF 文件路径列表（每文件一独立头扫描任务；可为空列表）")
build_design_db_doc.add_param("lef_paths",
    schema=Schema(list, check=lambda ps: len(ps) > 0 and all(
        isinstance(p, str) and p for p in ps),
        error="must be a non-empty list of non-empty file paths"),
    required=True, desc="lef 文件路径列表；首元素为 tech lef，其余为 cell lef")
build_design_db_doc.add_param("lib_db",
    schema=Schema(object, check=lambda v: hasattr(v, "LIBRARY_OBJ"),
                  error="must be a LibDb instance"),
    required=True, desc="lib 库 db（LibDb 实例，S3 merge 的直接前驱）")
build_design_db_doc.add_param("settings",
    schema=Schema(dict), required=False, default=None, none_ok=True,
    desc="稳定配置项（dict）；首版无激活键，保留参数位")
build_design_db_doc.add_param("alpha",
    schema=Schema(dict), required=False, default=None, none_ok=True,
    desc="未稳定配置项（dict）；键经 DSAlphaSettings 声明式定义（五要素："
         "src/emir/design/py/alpha_settings.py，2026-09-13 裁定）："
         "density_bin_size（密度采样格边长，µm，≥1，缺省 10）、"
         "net_batch_size（网内容批界网数，≥1，缺省 1000）、lcp_name_arena"
         "（R8d 裁定 55：bool，缺省 False——名字伴生对象 instance/net 两 "
         "hasher id→name 侧 LCP 后缀压缩封口，容量换内存的 alpha 路径）、"
         "S8 分区决策四键：target_partitions（'{x}x{y}' 直切，如 '4x3'，"
         "None=未设置）、partition_count（总分区数，0=未设置）、"
         "partition_target_density（目标合成负载，缺省 150000——N = "
         "ceil(总负载/目标)）、density_channel_weights（通道比重 dict "
         "{'instance': 6, 'metal': 2, 'via': 2}，缺 key 用默认）；"
         "S9 小 DEF 聚合阈值 def_aggregate_threshold（字节，≥1，缺省 "
         "64 MiB——预估展开数据规模 = DEF 文件大小 × 实例化次数，低于阈值"
         "的多个小 block 定义聚合到同一展开任务）；优先级 "
         "target_partitions > partition_count > partition_target_density；"
         "非法值/未知键 DSGN::0013 一次汇总提醒后回退默认/忽略，不 raise")
build_design_db_doc.add_example("构建 design db",
    code='''design_db = proj.build_design_db(
    name="design", def_paths=["block.def"], lef_paths=["tech.lef", "cells.lef"],
    lib_db=lib_db)
proj.wait_frozen("design", timeout=600)
design = design_db.load_design()   # EXDSDesign 容器''',
    desc="lef/def 解析 + lib merge → 冻结后读容器")
build_design_db_doc.add_keyword(["design", "def", "lef", "stack", "via",
                                 "block", "port", "emir"])


@register_flow(EMIRProject)
@document(build_design_db_doc)
def build_design_db(self, name: str, def_paths: list, lef_paths: list,
                    lib_db, settings: dict = None, alpha: dict = None):
    """构建 design db：lef/def 解析 + lib merge + 冻结。

    异步 4 步：检查输入 → 建库（DesignDb，role="design"）→ 阶段链提交
    （S1 tech lef → S2 每 cell lef 一任务 + 汇总 → S3 lib merge ∥ S4+S4b
    每 DEF 一任务 + 汇总 → freeze，语义见 ds_flow.run_design_flow）。
    重名 macro/port/via 保留首份抛弃后续 + DSGN 消息提醒（不抛异常）。

    Args:
        self: 自动绑定的 EMIRProject 实例。
        name: db 子目录名 + Project 内部 key。
        def_paths: DEF 文件路径列表。
        lef_paths: lef 文件路径列表（首元素 tech lef，其余 cell lef）。
        lib_db: LibDb 实例（S3 merge 的直接前驱）。
        settings: 稳定配置项（首版无激活键）。
        alpha: 未稳定配置项（八键经 DSAlphaSettings 声明式定义，见
            UserDoc；非法值/未知键 DSGN::0013 提醒后回退/忽略）。

    Returns:
        ``DesignDb`` 句柄（freeze 异步进行中，可用 wait_frozen 等待）。
    """
    import os

    # ── Step 1: 检查输入（文件存在性 + LEF/DEF 形态嗅探，schema 无法覆盖；
    #    master 侧前置）──
    from .ds_utils import sniff_def_header, sniff_lef_header
    for p in lef_paths:
        if not os.path.isfile(p):
            raise FileNotFoundError(f"build_design_db: lef file not found: {p}")
        sniff_lef_header(p)
    for p in def_paths:
        if not os.path.isfile(p):
            raise FileNotFoundError(f"build_design_db: def file not found: {p}")
        sniff_def_header(p)

    # ── Step 2: 建库（DesignDb，role="design"）──
    db = self._create_db(name, db_cls=DesignDb)

    # ── Step 2.5: alpha 设置（声明式五要素，2026-09-13 裁定）：逐键校验
    #    覆盖 → 问题一次汇总 DSGN::0013（user warn，不 raise）→ settings
    #    对象随建库写入 db（消费点 read_object 读回 + normalize 兜底）──
    from .alpha_settings import get_default_alpha_settings
    alpha_settings = get_default_alpha_settings()
    apply_result = alpha_settings.apply(alpha)
    detail = alpha_settings.format_apply_result(apply_result)
    if detail:
        from fly import message
        message("DSGN::0013", 0, f"invalid design alpha settings — {detail}")
    db.write_object(DesignDb.ALPHA_SETTINGS_OBJ, alpha_settings)

    # ── Step 3 + 4: 阶段链提交 + freeze 提交 ──
    from .ds_flow import run_design_flow
    run_design_flow(db, lef_paths, def_paths, lib_db)

    INFO(f"build_design_db: '{name}' submitted "
         f"({len(lef_paths)} lef files [1 tech + {len(lef_paths) - 1} cell], "
         f"{len(def_paths)} def files)")
    return db
