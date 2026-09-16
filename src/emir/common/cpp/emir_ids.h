#pragma once

// =============================================================================
// emir 模块族（common 子基座）的实体强类型 id（单一权威定义点，2026-09-16 裁定）。
//
// 分层（框架层不感知业务）：StrongIdT 模板机器在 common/types（全仓公
// 用）；本头 = emir 族的**具体 id 类型实例化**——design/timing 等 emir
// 子模块一律直接引此处类型（include <emir/common/cpp/emir_ids.h>），
// 禁止模块内重复定义；新增实体 id（如将来 timing 的 CMClockId）也在
// 本处新增。
//
// 命名 = 前缀 CM（随所在 emir/common 模块，§2.2 模
// 块前缀体系对本头的适用形态）+ 实体 + Id；Tag 结构体同名族（仅强类型
// 分类的编译期标记，无数据）。
//
// 前缀并存说明（2026-09-16 裁定）：CM 前缀与 container 模块族的容器别
// 名（CMVector/CMString/CMLookupTable 等）同字——此处 CM*Id 是 emir 业
// 务公共件，彼处是框架容器别名层，双归属以后缀可辨（id 族一律 Id 后缀
// 值类型，容器族一律容器类型名），dev-rules §5 前缀登记表已登记双归属。
//
// 位宽分组（id 数量级：32 位族十万级以内、64 位族 instance 可达 10⁹ 级，
// 同 design db 裁定 ㊳ 口径）：
//   32 位族：CMCellId / CMPinId / CMViaCellId / CMLayerId /
//            CMPartitionId
//   64 位族：CMInstanceId / CMNetId / CMViaInstanceId（层级树区间
//            起始同族；区间「长度」是计数不是编号，保持裸 uint64_t）
//
// 无效哨兵 = 内部整型最大值（CMCellId::kInvalid 形态，随模板定义），
// 与 design db 既有 kInvalidId/kNoLayer 哨兵值同口径。
// =============================================================================

#include <common/types/cpp/strong_id.h>

#include <cstdint>

namespace fly {

// cell（lef macro 与 block cell 同一编号空间）
struct CMCellIdTag {};
using CMCellId = StrongIdT<CMCellIdTag, uint32_t>;

// pin（design db 全局 pin 名字空间 id；2026-09-16 裁定 3）
struct CMPinIdTag {};
using CMPinId = StrongIdT<CMPinIdTag, uint32_t>;

// via cell
struct CMViaCellIdTag {};
using CMViaCellId = StrongIdT<CMViaCellIdTag, uint32_t>;

// layer（DSStack 层表）
struct CMLayerIdTag {};
using CMLayerId = StrongIdT<CMLayerIdTag, uint32_t>;

// 分区（分区表下标）
struct CMPartitionIdTag {};
using CMPartitionId = StrongIdT<CMPartitionIdTag, uint32_t>;

// instance（全局；per-block local id 同型）
struct CMInstanceIdTag {};
using CMInstanceId = StrongIdT<CMInstanceIdTag, uint64_t>;

// net（全局）
struct CMNetIdTag {};
using CMNetId = StrongIdT<CMNetIdTag, uint64_t>;

// via instance
struct CMViaInstanceIdTag {};
using CMViaInstanceId = StrongIdT<CMViaInstanceIdTag, uint64_t>;

}  // namespace fly
