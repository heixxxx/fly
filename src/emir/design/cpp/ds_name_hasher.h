#pragma once

// =============================================================================
// DSNameHasherT — design db 的 name ↔ id 双向映射底座（R7 name 体系收敛，
// 裁定 ㊱㊲㊳㊴㊸；R8b backend 化改造，裁定 52/㊶㊷㊸；R8d id→name 侧
// LCP 后缀共享双形态，裁定 54/55）。
//
// 三层职责的第一层（㊹ 架构终局）：**Hasher 家族 = block 级 local 查询**
// ——一律双向（㊲：单向为缺陷）；无效 id 哨兵 = id 类型最大值（㊴，
// is_valid_id 为 static 纯值判定，零状态零依赖——不加载 hasher 也可用）。
//
// —— 内部结构（R8b，裁定 52：抽象包装先行，编译期模板策略）——
//
//   name→id 侧：BackendT（可替换 backend，见 DSNameBackendConcept 契约）
//               首实现 DSHasherBackendHatrie（tsl::htrie_map 压缩前缀树，
//               R8a 基准胜出：内存 1.98x 压缩 / get_id 1.08x 劣化 / 支持
//               增量注册；报告 .work/bench_name_mapper/REPORT.md）；
//               32 位组维持 hash backend（DSHasherBackendHash，即原
//               unordered_map 形态抽为 backend——裁定 ㊷ 十万级以内不树化）
//   id→name 侧：R8d 双形态（裁定 54/55，alpha 键 lcp_name_arena 控制，
//               默认 false = 形态一现状零变化）：
//               形态一（默认）= char arena（name_arena_）+ {offset,len}
//               偏移表（name_table_）——通用件放 hasher 本体，不进
//               backend，换 backend 不动该侧；
//               形态二（LCP 压缩）= finalize_for_save 封口构建：后缀
//               arena + 偏移表 {suffix_off, suffix_len, lcp_len} +
//               id→rank（4B/名）+ checkpoint 表（每 64 rank 存全量名），
//               构建后释放全名 arena/偏移表（基准 54：arena 实省 63.7%
//               /get_name 随机 4.1x 亚微秒可接受/注册序 27.8x 劣化——
//               故配套 rank 序批量遍历 for_each_name_by_rank）
//
//   **替换点 = 六实体别名单处**：未来换 backend（marisa 等）只改别名
//   定义处的 BackendT 模板实参，业务零感知（对外接口不变）。
//
//   **序列化 = 双段制**（R8b 对比实验形态，最终取择依数据由用户裁定）：
//   权威段 = backend 无关的通用名集格式（按形态标记位区分两形态——
//   LCP on/off 为运行时 alpha 选择、两形态均活跃使用，读回按标记自
//   识别重建对应形态；标记位是功能区分字段、非版本兼容机制——早期
//   开发阶段不考虑版本兼容，P6：工作区未 commit 无历史数据包袱）；
//   加速段 = backend 原生序列化字节流（经 FLY_SERIALIZE_EXTERNAL 宏桥
//   进 fly 框架，全 backend 统一占位、无原生形态者恒空段——字段集与
//   backend 种类无关，与 id→name 形态无关）。读回优先直载，空段/
//   段被拒/计数不符 → 权威段重建兜底。
//
// 实体语义别名（㊸ 用户钦定清单，别名不带位宽标识——位宽切换只改别名
// 定义一处；id 位宽按实体数量级分组，裁定 ㊳；backend 默认按 ㊷/52
// 差异化：32 位组 hash、64 位组 hat-trie）：
//   32 位组（十万级以内）：DSCellNameHasher / DSPinNameHasher /
//                          DSViaCellNameHasher / DSLayerNameHasher
//   64 位组（instance 可达 10⁹ 级）：DSInstanceNameHasher / DSNetNameHasher
//
// 与 DSNameMapperT（全局组装层，见 ds_name_mapper.h）的边界（㊺）：本类
// 只做 flat 的 name ↔ id 查询，不理解层级；cell/pin/via cell/layer 的
// id 天然全局，hasher 即完整查询结构。DSInstanceNameHasher/
// DSNetNameHasher 是 block 级 local 查询（local id 空间），全局组装由
// DSNameMapperT 完成。
//
// DSBlockNames（㊵②）：per-DEF local 名空间伴生对象——DSBlock_<i>
// （instances/density/stats）与 DSBlockNames_<i>（instance/net 两 hasher）
// 分开落盘，业务按需加载其一（⑰/⑱ 按需加载原则；对「不拿 name」的
// 场景不加载本对象）。
// =============================================================================

#include <tsl/htrie_map.h>

#include <common/serialization/cpp/serialization_macros.h>
#include <common/types/cpp/property_macro.h>
#include <container/cpp/container_aliases.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fly {

// ── Backend 概念（裁定 52：编译期模板策略——非虚函数接口，查询热路径
//    零开销）────────────────────────────────────────────────────────────
//
// BackendT = name→id 索引操作的契约。操作集（未来候选实现按同一契约
// 新增即可；id→name 侧不在契约内——通用件放 hasher 本体）：
//
//   void   insert(const CMString& name, IdT id)
//       增量登记 + 覆盖赋值（新键存入、旧键覆盖——assign 重挂语义；
//       序列化读回的 backend 重建走同一路径）；
//   IdT    find(const CMString& name, IdT invalid_id) const
//       查询，未命中返回调用方传入的 invalid 哨兵（backend 不自带哨兵
//       常量——哨兵口径统一由 DSNameHasherT 持有）；
//   size_t size() const
//       已登记名字数（distinct name 数）；
//   void   clear()
//       清空（重建前复位）；
//   void   for_each(Fn&& fn) const
//       遍历导出：fn(name, id) 回调逐对产出（诊断/导出用，迭代序无
//       业务含义，消费方不得依赖顺序）。
//
// 全部操作进编译期约束（for_each 以探针函子校验回调形态）。另：backend
// 须可被 FLY_SERIALIZE_EXTERNAL 宏桥为序列化加速段（无原生持久化形态者
// 桥体恒写空段——序列化字段集因此与 backend 种类无关，裁定 52；桥体
// 随 backend 实现一并提供）。
template <typename IdT>
struct DSNameBackendProbeFn {
    void operator()(const CMString&, IdT) const {}
};

template <typename B, typename IdT>
concept DSNameBackendConcept = requires(B& b, const B& cb, const CMString& name, IdT id) {
    b.insert(name, id);
    { cb.find(name, id) } -> std::same_as<IdT>;
    { cb.size() } -> std::same_as<size_t>;
    b.clear();
    cb.for_each(DSNameBackendProbeFn<IdT>{});
};

// ── 实现一：hash backend（原 R7 unordered_map 形态抽为 backend）──────
// 32 位组默认（cell/pin/via cell/layer，十万级以内——裁定 ㊷ 不树化，
// 行为与内存形态与 R7 现状一致）。
template <typename IdT>
class DSHasherBackendHash {
public:
    void insert(const CMString& name, IdT id) { map_[name] = id; }

    IdT find(const CMString& name, IdT invalid_id) const {
        auto it = map_.find(name);
        return it == map_.end() ? invalid_id : it->second;
    }

    size_t size() const { return map_.size(); }

    void clear() { map_.clear(); }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (const auto& [name, id] : map_) {
            fn(name, id);
        }
    }

private:
    CMUnorderedMap<CMString, IdT> map_;
};

// ── 实现二：hat-trie backend（压缩前缀树，64 位组默认——裁定 52）────
// tsl::htrie_map：增量 insert 兼容现行「解析回调逐名注册」模式；前缀
// 共享压缩（EDA 名冗余大头：层次前缀/总线位/系统名，R8a 实测 2~3 倍）。
// 原生持久化（serialize/deserialize，协议版本头自校验）经 FLY_SERIALIZE_
// EXTERNAL 宏桥接为序列化加速段（save_native/load_native）——backend
// 契约外的可选能力，不进 DSNameBackendConcept 核心操作集。
template <typename IdT>
class DSHasherBackendHatrie {
public:
    void insert(const CMString& name, IdT id) { trie_[name] = id; }

    IdT find(const CMString& name, IdT invalid_id) const {
        auto it = trie_.find(name);
        return it == trie_.end() ? invalid_id : it.value();
    }

    size_t size() const { return trie_.size(); }

    void clear() { trie_.clear(); }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        // htrie 迭代器序 = 字典序（前缀树拓扑序）；it.key() 返回
        // std::string 临时，回调以 const 引用接（非热路径，可接受）
        for (auto it = trie_.begin(); it != trie_.end(); ++it) {
            fn(it.key(), it.value());
        }
    }

    // —— 原生序列化加速段（FLY_SERIALIZE_EXTERNAL 桥用；基准/诊断可
    //    显式调用测段大小/直载耗时）——

    // 原生字节流导出（htrie serialize 函子两种调用形态：单值写 +
    // (ptr,len) 块写——统一收进 out 尾部；htrie 形参为非常量左值引用，
    // 函子先落左值）
    void save_native(CMString& out) const {
        auto writer = [&out](const auto&... args) { native_append(out, args...); };
        trie_.serialize(writer);
    }

    // 原生字节流导入（读回直载）。空段/协议版本不符/损坏一律静默拒绝
    // （保持空态返回 false，不抛——调用方以计数校验兜底走权威段重建，
    // 异常穿透会中断整个 FLY_DECODE）。同二进制保存/读回场景
    // hash_compatible = true（复用存储哈希、校验 GrowthPolicy 一致）。
    // 读侧协议 = htrie deserialize_value 的零参显式模板调用
    // operator()<U>() 返回值 + (ptr,len) 块读——泛型 lambda 不匹配，
    // 用专用函子。
    bool load_native(const CMString& data) {
        if (data.empty()) {
            return false;
        }
        size_t cursor = 0;
        NativeReader reader{data, cursor};
        try {
            // htrie_map::deserialize 是 static 工厂（返回新 map——经实例
            // 调用会把结果填进临时对象并丢弃，编译期不报错），必须以
            // 返回值赋回
            trie_ = tsl::htrie_map<char, IdT>::deserialize(reader,
                                                           /*hash_compatible=*/true);
        } catch (const std::exception&) {
            clear();
            return false;
        }
        return true;
    }

    // 加速段写盘开关（运行时字段，不入任何序列化流；R8b 双格式对比
    // 用——false = 纯通用名集格式（重建式），true（默认）= 双段制）
    bool native_cache_on_save_ = true;

private:
    // 原生流读侧函子（htrie Deserializer 协议）：operator()<U>() 单值读
    // + (ptr,len) 块读；越界抛 std::runtime_error（load_native 捕获）
    struct NativeReader {
        const CMString& data;
        size_t& cursor;

        template <typename U>
        U operator()() {
            U v{};
            native_read(data, cursor, v);
            return v;
        }
        void operator()(char* p, size_t n) {
            native_read(data, cursor, p, n);
        }
    };

    // 原生流写侧：两种调用形态（单值 / (ptr,len) 块）统一追加
    static void native_append(CMString& out, const char* p, size_t n) {
        out.append(p, n);
    }
    template <typename T>
    static void native_append(CMString& out, const T& v) {
        out.append(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    // 原生流读侧实现：越界抛（load_native 捕获）
    template <typename T>
    static void native_read(const CMString& data, size_t& cursor, T& v) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "htrie native value read requires trivially copyable");
        if (cursor + sizeof(T) > data.size()) {
            throw std::runtime_error("htrie native segment truncated");
        }
        std::memcpy(&v, data.data() + cursor, sizeof(T));
        cursor += sizeof(T);
    }
    static void native_read(const CMString& data, size_t& cursor, char* p,
                            size_t n) {
        if (cursor + n > data.size()) {
            throw std::runtime_error("htrie native segment truncated");
        }
        std::memcpy(p, data.data() + cursor, n);
        cursor += n;
    }

    tsl::htrie_map<char, IdT> trie_;
};

// ── 原生加速段外接桥（FLY_SERIALIZE_EXTERNAL 宏包装——补充裁定：禁止
//    裸 ADL serialize 函数散落；本节须位于 fly 命名空间内供 ADL 命中）──
//
// 段格式 = fly_ser::text（长度前缀 + 原生字节），全 backend 统一占位
// （无原生形态的 backend 恒写空段——字段集与 backend 种类无关，换
// backend 不改变文件布局，裁定 52；读侧空段/段被拒由 hasher 计数校验
// 兜底走权威段重建）。
// hash backend：无原生持久化形态（重建式权威段足够——32 位组十万级
// 以内，裁定 ㊷），恒空段。
FLY_SERIALIZE_EXTERNAL_BEGIN(DSHasherBackendHash<IdT>, typename IdT)
    (void)o;
    CMString segment;
    fly_ser::text(s, segment);  // 写空段 / 读侧消耗等长空段
FLY_SERIALIZE_EXTERNAL_END

// hat-trie backend：htrie 原生 serialize/deserialize 字节流桥进 fly 框架
// （读侧直载失败静默拒绝 → hasher 计数校验兜底重建）
FLY_SERIALIZE_EXTERNAL_BEGIN(DSHasherBackendHatrie<IdT>, typename IdT)
    if constexpr (fly_ser::is_deserializer_v<S>) {
        CMString segment;
        fly_ser::text(s, segment);
        o.load_native(segment);
    } else {
        CMString segment;
        if (o.native_cache_on_save_) {
            o.save_native(segment);
        }
        fly_ser::text(s, segment);
    }
FLY_SERIALIZE_EXTERNAL_END

// ── 偏移表元素：id → arena 名字段定位 ───────────────────────────────
// 下标 = id；{offset, len} 对。len 0 = 空洞（assign 稀疏落位的未登记
// id 占位，get_name 返回空视图——R7 空名占位语义不变）。
struct DSNameSlot {
    // 名字在 arena 的起始字节
    uint64_t offset = 0;
    // 名字字节长
    uint32_t length = 0;

    FLY_SERIALIZE(offset, length)
};

// ── 后缀偏移表元素：rank → LCP 后缀字段定位（R8d 形态二，裁定 54/55）──
// 下标 = rank（名集字典序序号）。lcp_len = 与前一 rank 名的公共前缀长
//（rank 0 = 0；2B 容量——基准原型同款，EDA 名远低于 64KB，超限在封口
// 点显式报错防静默截断）。
struct DSSuffixSlot {
    // 后缀在后缀 arena 的起始字节
    uint64_t offset = 0;
    // 后缀字节长（= 名长 − lcp_len）
    uint32_t length = 0;
    // 与前一 rank 名的公共前缀长（2B）
    uint16_t lcp_len = 0;

    FLY_SERIALIZE(offset, length, lcp_len)
};

// name ↔ id 双向映射模板底座（㊲：所有 name mapper 共享同一底层结构，
// 只有保存数据类型不同——id 类型即模板参数 IdT；BackendT 为 name→id
// 索引实现，默认按位宽组差异化，见文件头注释）。
template <typename IdT, typename BackendT = DSHasherBackendHatrie<IdT>>
class DSNameHasherT {
    // BackendT 满足 Backend 概念（裁定 52 抽象面——编译期即检查，防止
    // 不完整实现静默实例化）
    static_assert(DSNameBackendConcept<BackendT, IdT>,
                  "BackendT must satisfy DSNameBackendConcept "
                  "(insert/find/size/clear)");

public:
    // 无效 id 哨兵（㊴）= id 类型最大值（32 位组 = 0xFFFFFFFF、64 位组 =
    // 0xFFFFFFFFFFFFFFFF）。有效 id 空间因此排除 max 值（与空洞容忍不
    // 冲突：现有哨兵 DSDesign::kInvalidId / DSStack::kNoLayer 值不变、
    // 已收编到本口径）。
    static constexpr IdT kInvalidId = std::numeric_limits<IdT>::max();

    // 哨兵判定（㊴/㊵②）：static constexpr 纯值函数，零状态零依赖——
    // 不加载 hasher/mapper 时同样可用（编译期可判定）。
    static constexpr bool is_valid_id(IdT id) { return id != kInvalidId; }

    // —— R8d 形态二常量（裁定 54 定案数据）——
    // checkpoint 间隔（54：64 定案——128 仅再省 0.5MB 换 36% 劣化；
    // 回退步数封顶 = 间隔−1 = 63，实测 max 63/均值 31.5 与理论一致）
    static constexpr uint32_t kLcpCheckpoint = 64;
    // rank 空洞哨兵：形态二 id→rank 表中未登记 id 的占位（形态一空槽
    // len 0 的 rank 空间对应物）
    static constexpr uint32_t kNoRank = std::numeric_limits<uint32_t>::max();

    // 形态判定（R8d）：true = 形态二 LCP 压缩（finalize_for_save 封口
    // 后）；false = 形态一全名 arena（默认）
    bool is_lcp_form() const { return lcp_form_ != 0; }

    // id→name 域规模（越界判别用）：形态一 = 偏移表规模；形态二 =
    // id→rank 表规模（空洞占位含内——域语义两形态一致）
    size_t name_domain() const {
        return lcp_form_ != 0 ? id_to_rank_.size() : name_table_.size();
    }

    // 正向查询：name → id，委托 backend；未命中返回 kInvalidId（返回值
    // 哨兵语义，替代迭代器风格；裁定 ㊴ 查询接口命名 get_id/get_name
    // 统一 get_ 前缀）
    IdT get_id(const CMString& name) const {
        return backend_.find(name, kInvalidId);
    }

    // 反向查询：id → name。已登记域内必达（越界为调用方契约错误，
    // debug 断言）；空洞（assign 稀疏落位的未登记下标）返回空串。
    // 返回 CMString 按值（R7 const 引用签名的调用面零改动等效形态——
    // arena 化后无稳定 string 可引；get_name 走解析边界/用户输出，
    // 非热路径，拷贝成本可接受）。
    // R8d 双形态分派：形态二按 id→rank 经 checkpoint 封顶回溯（≤ 63 步
    // 逐级截断拼接——基准 54 随机访问 4.1x 劣化的载体，亚微秒可接受；
    // 批量场景请走 for_each_name_by_rank 规避注册序 27.8x 劣化）
    CMString get_name(IdT id) const {
        if (lcp_form_ != 0) {
            assert(static_cast<size_t>(id) < id_to_rank_.size());
            const uint32_t rank = id_to_rank_[static_cast<size_t>(id)];
            if (rank == kNoRank) {
                return CMString{};  // 空洞占位（形态一空名占位语义不变）
            }
            return name_by_rank(rank);
        }
        assert(id < name_table_.size());
        const DSNameSlot& slot = name_table_[static_cast<size_t>(id)];
        return CMString(name_arena_.data() + slot.offset, slot.length);
    }

    // 新名登记双写，id = 当前规模（偏移表尾部下标）。重名保留首份返回
    // 既有 id（幂等登记——解析边界重名兜底同 register_net 语义）。
    // 封口后（LCP 形态）拒绝——封口 hasher 只读。
    IdT emplace(const CMString& name) {
        check_not_sealed();
        const IdT existing = backend_.find(name, kInvalidId);
        if (is_valid_id(existing)) {
            return existing;
        }
        const IdT id = static_cast<IdT>(name_table_.size());
        name_table_.push_back(append_name(name));
        backend_.insert(name, id);
        return id;
    }

    // 指定 id 双写（id 由外部编号体系分配：instance/net local id 从 1
    // 起、fake cell 稀疏 id、pin 全局平铺 id——均非「当前规模」序）。
    // resize 容忍空洞（未落位下标为空洞占位）；同名重挂覆盖指向新 id
    // （register_pin 重挂语义）并清空旧 id 槽位——偏移表与 backend 保持
    // 一致（名字至多出现在权威槽位），序列化读回重建才能无损还原
    // name→id 指向。封口后（LCP 形态）拒绝——封口 hasher 只读。
    void assign(const CMString& name, IdT id) {
        check_not_sealed();
        const IdT old = backend_.find(name, kInvalidId);
        if (is_valid_id(old) && old != id &&
            static_cast<size_t>(old) < name_table_.size()) {
            name_table_[static_cast<size_t>(old)] = DSNameSlot{};
        }
        if (id >= name_table_.size()) {
            name_table_.resize(static_cast<size_t>(id) + 1);
        }
        name_table_[static_cast<size_t>(id)] = append_name(name);
        backend_.insert(name, id);
    }

    // 已登记名字数（= backend 规模；不含 assign 稀疏落位产生的空洞——
    // 空洞只是偏移表的占位下标，未登记不算数。计数语义消费方：
    // DSBlockBuildData::net_count 与伴生对象规模面）
    size_t size() const { return backend_.size(); }

    // 遍历导出：委托 backend 逐对产出 fn(name, id)（诊断/导出用，
    // 迭代序无业务含义——htrie 为字典序、hash 为哈希序）
    template <typename Fn>
    void for_each(Fn&& fn) const {
        backend_.for_each(fn);
    }

    // —— R8d 封口点（裁定 55：id→name 侧双形态构建——落盘前一次性
    //    转换；构建期决定形态，取显式封口而非构造期开关：alpha 在运行
    //    期抵达建库流程、解析 hasher 已在逐名增量登记中，封口 = 单一
    //    写定点（DSBlockNames_<i> 落盘前）单调用点，构造期开关则需把
    //    开关穿透解析链每一处 hasher 构造位）——

    // 形态封口：lcp_enabled = false 恒无操作（形态一默认路径零变化）；
    // true = 按字典序排序名集构建形态二（后缀 arena + 偏移表 + id→rank
    // + checkpoint 表，checkpoint=64），随后释放全名 arena/偏移表（压缩
    // 收益本体——基准 54：arena 实省 63.7%）。幂等（已封口再调无操作）。
    // 封口后 hasher id→name 侧只读：emplace/assign 拒绝
    //（std::logic_error 快速失败）；查询面 get_id/get_name/for_each/
    // for_each_name_by_rank 全部可用。相邻名公共前缀超 2B 容量（>64KB，
    // EDA 名不现实）显式报错防静默截断。
    void finalize_for_save(bool lcp_enabled) {
        if (!lcp_enabled || lcp_form_ != 0) {
            return;
        }
        build_lcp_form();
        lcp_form_ = 1;
        // 释放形态一存储（与空临时 swap 保证容量归还——arena 是 64 位
        // 组存储大头，clear 不缩容等于白做）
        CMString().swap(name_arena_);
        CMVector<DSNameSlot>().swap(name_table_);
    }

    // rank 序批量遍历（R8d 裁定 55③）：按名字典序（= 形态二 rank 序）
    // 回调 fn(id, name)。批量导出/消息格式化用——逐 id get_name 在注册
    // 序下的 id→rank 乱序跳转 cache 劣化实测 27.8x（裁定 54），本接口
    // 连续 rank 序访问规避之。两形态都提供（产出同一序同一内容——名字
    // 典序唯一确定 rank 序）：
    //   形态一：登记 id 按名排序后遍历（空洞跳过——未登记无产出）；
    //   形态二：rank 0..n−1 直接回溯（rank→id 逆表一次构建，O(n) 临时）。
    template <typename Fn>
    void for_each_name_by_rank(Fn&& fn) const {
        if (lcp_form_ != 0) {
            CMVector<IdT> rank_to_id(lcp_suffix_table_.size(), kInvalidId);
            for (size_t id = 0; id < id_to_rank_.size(); ++id) {
                const uint32_t r = id_to_rank_[id];
                if (r != kNoRank) {
                    rank_to_id[r] = static_cast<IdT>(id);
                }
            }
            for (size_t r = 0; r < rank_to_id.size(); ++r) {
                fn(rank_to_id[r], name_by_rank(static_cast<uint32_t>(r)));
            }
            return;
        }
        CMVector<IdT> order;
        order.reserve(backend_.size());
        for (size_t id = 0; id < name_table_.size(); ++id) {
            if (name_table_[id].length != 0) {
                order.push_back(static_cast<IdT>(id));
            }
        }
        std::sort(order.begin(), order.end(), [this](IdT a, IdT b) {
            return name_view(a) < name_view(b);
        });
        for (const IdT id : order) {
            const DSNameSlot& slot = name_table_[static_cast<size_t>(id)];
            fn(id, CMString(name_arena_.data() + slot.offset, slot.length));
        }
    }

    // backend 重建（权威段读回路径——加速段缺失/被拒/计数不符时自动
    // 调用；公开供基准/诊断工具显式触发）。形态感知：
    //   形态一：偏移表 id 升序逐条 insert 覆盖式登记（空洞占位跳过）；
    //   形态二：rank→id 逆表（栈上临时）→ rank 序回溯全名逐条登记。
    void rebuild_backend() {
        backend_.clear();
        if (lcp_form_ != 0) {
            CMVector<IdT> rank_to_id(lcp_suffix_table_.size(), kInvalidId);
            for (size_t id = 0; id < id_to_rank_.size(); ++id) {
                const uint32_t r = id_to_rank_[id];
                if (r != kNoRank) {
                    rank_to_id[r] = static_cast<IdT>(id);
                }
            }
            for (size_t r = 0; r < rank_to_id.size(); ++r) {
                backend_.insert(name_by_rank(static_cast<uint32_t>(r)),
                                rank_to_id[r]);
            }
            return;
        }
        for (size_t id = 0; id < name_table_.size(); ++id) {
            const DSNameSlot& slot = name_table_[id];
            if (slot.length == 0) {
                continue;  // 空洞占位不重建
            }
            backend_.insert(
                CMString(name_arena_.data() + slot.offset, slot.length),
                static_cast<IdT>(id));
        }
    }

    // —— id→name 通用侧（公开成员，同 DSDesign 字段风格，构建期与
    //    测试直书）——
    // 名字字符池（连续 arena，消逐名堆分配；前缀冗余的压缩由 backend
    // 侧承担，arena 存原始名）。形态二封口后释放（置空 + 容量归还）
    CMString name_arena_;
    // {offset,len} 偏移表（下标 = id，空洞 len 0）
    CMVector<DSNameSlot> name_table_;

    // —— R8d 形态二成员（LCP 后缀共享，裁定 54/55；finalize_for_save
    //    构建填充，公开成员同本类字段风格，基准/测试直书）——
    // 形态标记位：0 = 形态一全名 arena+偏移表；1 = 形态二 LCP 压缩。
    // 随权威段落盘（读回自识别重建对应形态；两形态均活跃使用——alpha
    // 运行期选择——此为功能区分字段、非版本兼容机制）
    uint8_t lcp_form_ = 0;
    // 后缀字符池：rank 序相邻名公共前缀之后的后缀连续拼接
    CMString lcp_suffix_arena_;
    // 后缀偏移表（下标 = rank：{后缀定位, 与前名公共前缀长}）
    CMVector<DSSuffixSlot> lcp_suffix_table_;
    // id → rank（4B/名；下标 = id，空洞 = kNoRank）——形态一偏移表的
    // rank 空间对应物（域规模不变：空洞占位保留）
    CMVector<uint32_t> id_to_rank_;
    // checkpoint 全量名池 + 偏移（每 kLcpCheckpoint rank 存全量名——
    // 回退链起点封顶步数 ≤ 63；偏移表 n_ckpt+1 项含尾哨兵 = 池长）
    CMString lcp_ckpt_arena_;
    CMVector<uint64_t> lcp_ckpt_offsets_;

    // name→id 索引 backend（R8b 裁定 52 可替换；默认按位宽组差异化：
    // 32 位组 = hash、64 位组 = hat-trie。公开成员——替换性单测/基准/
    // 加速段开关直书，同本类字段风格）
    BackendT backend_;

    // 序列化 = 双段制（R8b 定型；R8d 权威段按形态标记位分派——两形态
    // 均活跃使用，标记位是功能区分字段非版本兼容机制，早期开发阶段
    // 不考虑版本兼容 P6：布局直接变更，旧布局不留存）：
    //   权威段（backend 无关通用名集格式）：
    //     形态一 = 全名 arena + 偏移表；形态二 = 后缀 arena + 偏移表 +
    //     id→rank + checkpoint 表；段首形态标记位读回自识别。
    //   加速段：backend 原生序列化字节流（FLY_SERIALIZE_EXTERNAL 桥，
    //   无原生形态的 backend 恒空段——字段集与 backend 种类无关、与
    //   id→name 形态无关）。
    // 读回优先直载（加速段命中），空段/段被拒/计数不符 → 权威段重建
    // 兜底；重建后仍不符 = 落盘数据损坏（FLY_DECODE 同类文件不可读
    // 异常）。32/64 位组同一路径。
    FLY_SERIALIZE_BEGIN(1)
        // 宏体 lambda 无 this 捕获——保存/加载两侧统一经 o 访问
        uint64_t fly_count_ = static_cast<uint64_t>(o.backend_.size());
        // 形态标记位（1B）：读侧先读后按标记分派权威段布局
        fly_ser::value(s, o.lcp_form_);
        if (o.lcp_form_ == 0) {
            // 权威段·形态一：全名 arena + {off,len} 偏移表
            FLY_FIELD(name_arena_);
            FLY_FIELD(name_table_);
        } else {
            // 权威段·形态二：后缀 arena + 偏移表 + id→rank + checkpoint
            FLY_FIELD(lcp_suffix_arena_);
            FLY_FIELD(lcp_suffix_table_);
            FLY_FIELD(id_to_rank_);
            FLY_FIELD(lcp_ckpt_arena_);
            FLY_FIELD(lcp_ckpt_offsets_);
        }
        // 计数（已登记名字数）：保存侧 = backend 规模；加载侧读入后
        // 与直载/重建结果比对
        fly_ser::value(s, fly_count_);
        FLY_FIELD(backend_);
        if constexpr (fly_ser::is_deserializer_v<S>) {
            if (fly_count_ != o.backend_.size()) {
                o.rebuild_backend();
                if (fly_count_ != o.backend_.size()) {
                    throw std::runtime_error(
                        "DSNameHasherT: registered name count mismatch "
                        "after backend rebuild (corrupt data)");
                }
            }
            if (o.lcp_form_ != 0 &&
                o.id_to_rank_.size() < o.backend_.size()) {
                throw std::runtime_error(
                    "DSNameHasherT: LCP rank domain smaller than "
                    "registered name count (corrupt data)");
            }
        }
    FLY_SERIALIZE_END

private:
    // 封口守卫（emplace/assign 共用）：LCP 形态 hasher 只读——构建期
    // 登记接口拒绝（快速失败，防半更新态：backend 有名而 rank 空间
    // 不知情）
    void check_not_sealed() const {
        if (lcp_form_ != 0) {
            throw std::logic_error(
                "DSNameHasherT: emplace/assign after finalize_for_save "
                "(sealed read-only LCP form)");
        }
    }

    // 名视图（形态一 arena 偏移表切片——排序比较/封口构建共用）
    std::string_view name_view(IdT id) const {
        const DSNameSlot& slot = name_table_[static_cast<size_t>(id)];
        return std::string_view(name_arena_.data() + slot.offset,
                                slot.length);
    }

    // 形态二构建（finalize_for_save 主体）：名集字典序排序 → 相邻 LCP
    // 切后缀 → checkpoint 表（基准原型 .work/bench_lcp 同构）
    void build_lcp_form() {
        const size_t domain = name_table_.size();
        // 登记 id 按名字典序排序（rank 序 = 排序序；空洞不入 rank 空间）
        CMVector<IdT> order;
        order.reserve(backend_.size());
        for (size_t id = 0; id < domain; ++id) {
            if (name_table_[id].length != 0) {
                order.push_back(static_cast<IdT>(id));
            }
        }
        if (order.size() > static_cast<size_t>(kNoRank)) {
            throw std::logic_error(
                "DSNameHasherT: name count exceeds uint32 rank space");
        }
        // 字典序排序（rank 序 = 名字典序）——LCP 压缩前提（相邻名高公共
        // 前缀）与 for_each_name_by_rank 的序语义共同依此
        std::sort(order.begin(), order.end(), [this](IdT a, IdT b) {
            return name_view(a) < name_view(b);
        });
        const size_t n = order.size();
        id_to_rank_.assign(domain, kNoRank);
        lcp_suffix_table_.resize(n);
        size_t suffix_total = 0;
        for (size_t r = 0; r < n; ++r) {
            id_to_rank_[static_cast<size_t>(order[r])] =
                static_cast<uint32_t>(r);
            const std::string_view cur = name_view(order[r]);
            uint32_t l = 0;
            if (r > 0) {
                const std::string_view prev = name_view(order[r - 1]);
                const size_t m = std::min(prev.size(), cur.size());
                while (l < m && prev[l] == cur[l]) {
                    ++l;
                }
                if (l > std::numeric_limits<uint16_t>::max()) {
                    throw std::logic_error(
                        "DSNameHasherT: adjacent-name LCP exceeds uint16 "
                        "capacity (name cluster > 64KB)");
                }
            }
            DSSuffixSlot& slot = lcp_suffix_table_[r];
            slot.offset = suffix_total;
            slot.length = static_cast<uint32_t>(cur.size() - l);
            slot.lcp_len = static_cast<uint16_t>(l);
            suffix_total += cur.size() - l;
        }
        lcp_suffix_arena_.reserve(suffix_total);
        for (size_t r = 0; r < n; ++r) {
            lcp_suffix_arena_.append(
                name_view(order[r]).substr(lcp_suffix_table_[r].lcp_len));
        }
        // checkpoint 表：每 kLcpCheckpoint rank 存全量名（尾哨兵 = 池长
        // ——便于取 checkpoint 名长）
        for (size_t r = 0; r < n; r += kLcpCheckpoint) {
            lcp_ckpt_offsets_.push_back(
                static_cast<uint64_t>(lcp_ckpt_arena_.size()));
            lcp_ckpt_arena_.append(name_view(order[r]));
        }
        lcp_ckpt_offsets_.push_back(
            static_cast<uint64_t>(lcp_ckpt_arena_.size()));
    }

    // 形态二 rank → 全名回溯。checkpoint 命中（rank % 64 = 0）零步直取；
    // 否则自 checkpoint 名前缀起步，逐级 append 中间名后缀 + resize 截断
    //（基准 54 报告 §4 正确性坑：append 总字节含中间名被截掉的尾部，
    // 尾部必须逐级截断、不得一次区间拷贝——残留尾名即错名）。
    CMString name_by_rank(uint32_t r) const {
        const uint32_t rem = r % kLcpCheckpoint;
        const uint32_t k0 = r / kLcpCheckpoint;
        if (rem == 0) {
            // checkpoint 命中：全量名直取（回退步数 0）
            return CMString(
                lcp_ckpt_arena_.data() + lcp_ckpt_offsets_[k0],
                static_cast<size_t>(lcp_ckpt_offsets_[k0 + 1] -
                                    lcp_ckpt_offsets_[k0]));
        }
        const uint32_t c = r - rem;  // 起点 checkpoint rank（向下对齐）
        // 起步 = checkpoint c 全量名的 [0, lcp(c,c+1)) 公共前缀
        CMString out(
            lcp_ckpt_arena_.data() + lcp_ckpt_offsets_[k0],
            lcp_suffix_table_[c + 1].lcp_len);
        for (uint32_t i = c + 1; i < r; ++i) {
            const DSSuffixSlot& slot = lcp_suffix_table_[i];
            out.append(lcp_suffix_arena_.data() + slot.offset, slot.length);
            out.resize(lcp_suffix_table_[i + 1].lcp_len);  // 逐级截断
        }
        const DSSuffixSlot& last = lcp_suffix_table_[r];
        out.append(lcp_suffix_arena_.data() + last.offset, last.length);
        return out;
    }

    // 名字追加进 arena，返回其槽位定位（emplace/assign 共用登记路径）
    DSNameSlot append_name(const CMString& name) {
        DSNameSlot slot;
        slot.offset = static_cast<uint64_t>(name_arena_.size());
        slot.length = static_cast<uint32_t>(name.size());
        name_arena_.append(name);
        return slot;
    }
};

// —— 实体语义别名（㊸ 用户钦定清单；别名不带位宽标识，裁定 ㊳ 位宽
//    分组：32 位组十万级以内、64 位组 instance 可达 10⁹ 级。BackendT
//    实参 = R8b 替换点——未来换 backend（marisa 等）只改此处）——

// cell 名 ↔ 全局 cell id（DSDesign；block cell 与 macro 同一编号空间）
using DSCellNameHasher = DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>;
// pin 组合键（"cell_name/pin_name"）↔ 全局平铺 pin id（DSDesign；D1）
using DSPinNameHasher = DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>;
// via cell 名 ↔ via cell id（DSDesign；⑫ DEF 来源带 design:: 前缀）
using DSViaCellNameHasher =
    DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>;
// 层名 ↔ 层 id（DSStack；layer_index_ 惰性索引的序列化替代）
using DSLayerNameHasher = DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>;
// block 内实例名 ↔ local instance id（DSBlockBuildData；local id 从 1
// 起、local 0 = block 自身占位不入表，⑧）
using DSInstanceNameHasher =
    DSNameHasherT<uint64_t, DSHasherBackendHatrie<uint64_t>>;
// block 内网名 ↔ local net id（DSBlockBuildData；local id 从 1 起、
// 0 保留未用，⑨）
using DSNetNameHasher = DSNameHasherT<uint64_t, DSHasherBackendHatrie<uint64_t>>;

// —— per-DEF local 名空间伴生对象（㊵②）——

// DSBlockNames：DSBlockBuildData 的名字伴生对象（DSBlockNames_<i> 独立
// 落盘）——instance/net 两 hasher + block 名冗余（与 DSBlockBuildData
// 对齐校验用）。DSBlock_<i>（instances/density/stats）与本对象分开
// write_object，业务按需加载其一（⑰/⑱ 按需加载原则：只读实例/密度
// 数据时不拿 name，名字是百万级存储大头；㊶/52 的 hat-trie 压缩只作用
// 于本对象承载的 64 位组 hasher）。
class DSBlockNames {
public:
    // block 名（DEF DESIGN 语句；与 DSBlockBuildData::block_name_ 对齐）
    CMString block_name_;
    // block 内实例名 ↔ local id（⑧；全局组装由 DSNameMapperT 经层级树
    // + 区间 start 完成）。CMSharedPtr 持有（序列化经框架 shared_ptr
    // 分支按值落盘所指对象；注入 DSNameMapperT 时 const 化共享、零拷贝）
    CMSharedPtr<DSInstanceNameHasher> instance_names_ =
        CMMakeShared<DSInstanceNameHasher>();
    // block 内网名 ↔ local id（⑨）
    CMSharedPtr<DSNetNameHasher> net_names_ = CMMakeShared<DSNetNameHasher>();

    CM_PROPERTY(block_name)

    FLY_SERIALIZE(block_name_, instance_names_, net_names_)
};

}  // namespace fly
