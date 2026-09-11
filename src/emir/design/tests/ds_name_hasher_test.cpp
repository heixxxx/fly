// DSNameHasherT 单测（R7 name 体系收敛 ㊱㊲㊳㊴㊸ + R8b backend 化，裁定
// 52/㊶㊷㊸）：
//   1. 双向一致性：emplace/assign 双写，get_id ↔ get_name 闭环；
//   2. 哨兵（㊴）：get_id 未命中返回 kInvalidId = id 类型最大值，
//      is_valid_id 纯值判定（static，不依赖实例）；
//   3. 空洞容忍：assign 指定 id 稀疏落位（resize 空洞为空名占位），
//      id→name 反查不受空洞影响；
//   4. 位宽分组（㊳）+ 实体别名（㊸ 用户钦定清单）+ R8b backend 默认
//      差异化（32 位组 = hash、64 位组 = hat-trie）——编译断言；
//   5. 序列化 round-trip：双段制（权威段 arena+偏移+计数 + 加速段），
//      纯通用（关加速段）/ 双段两形态 + 空/非空/空洞；
//   6. backend 替换性（裁定 52）：极简 dummy backend（std::map 包装，
//      仅测试用）实例化 DSNameHasherT——抽象面完备性证明；
//   7. htrie backend 一致性：与 unordered_map 参照的随机名集对比
//      （R8a 语义等价红线）；for_each 遍历导出；
//   8. DSBlockNames 伴生对象（㊵②）：instance/net 两 hasher 一体
//      序列化 round-trip。
// R8d（裁定 54/55，id→name 侧 LCP 后缀共享双形态，alpha 键
// lcp_name_arena）：
//   9. 形态二正确性：随机名集（层次前缀/总线位 + 稀疏空洞 assign）与
//      形态一全量 get_name/get_id 对比；长名簇回退链逐级截断（基准
//      报告 §4 坑：中间名尾部残留）；空 hasher/纯空洞域；
//  10. for_each_name_by_rank 两形态产出同一 rank 序（名字典序）；
//  11. 封口点语义：finalize_for_save 幂等/false 零变化/封口后
//      emplace-assign 拒绝；形态一默认路径回归（lcp_form_ = 0）；
//  12. 形态二序列化 round-trip（标记位自识别 + 直载/重建两路径 +
//      空封口形态）；轻量性能观测（10 万名 get_name 随机采样 +
//      内存自算对照，供 alpha 文档参考）。
#include <emir/design/cpp/ds_name_hasher.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace fly;

// ── 6. dummy backend（std::map 包装，仅测试用——裁定 52 替换性验证：
//         Backend 概念 insert/find/size/clear 的最小实现即可实例化）──
template <typename IdT>
class DSDummyNameBackend {
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
        for (const auto& [n, id] : map_) {
            fn(n, id);
        }
    }

private:
    CMUnorderedMap<CMString, IdT> map_;
};

// dummy 无原生持久化形态——外接桥恒写空段（同 hash backend 形态；
// 序列化字段集与 backend 种类无关，裁定 52）
FLY_SERIALIZE_EXTERNAL_BEGIN(DSDummyNameBackend<IdT>, typename IdT)
    (void)o;
    CMString segment;
    fly_ser::text(s, segment);
FLY_SERIALIZE_EXTERNAL_END

// dummy 实例化别名（类型含逗号——FLY_DECODE 宏参数须走别名）
using DummyHasher = DSNameHasherT<uint64_t, DSDummyNameBackend<uint64_t>>;

// ── 1/2. 双向一致性 + 哨兵（uint64 实例，显式模板参数为多实例化
//         能力验证，§2.2 例外条款）────────────────────────────────────

TEST(DSNameHasherTest, EmplaceBidirectionalAndSentinel) {
    DSNameHasherT<uint64_t> h;
    EXPECT_EQ(h.size(), 0u);
    EXPECT_TRUE(h.name_table_.empty());
    EXPECT_TRUE(h.name_arena_.empty());

    // emplace：新名登记双写，id = 当前规模
    const uint64_t a = h.emplace("alpha");
    const uint64_t b = h.emplace("beta");
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    EXPECT_EQ(h.size(), 2u);

    // 正向：命中返回登记 id（hatrie backend）
    EXPECT_EQ(h.get_id("alpha"), 0u);
    EXPECT_EQ(h.get_id("beta"), 1u);
    // 反向：已登记域内必达（arena 拼接无损）
    EXPECT_EQ(h.get_name(0), "alpha");
    EXPECT_EQ(h.get_name(1), "beta");

    // 哨兵（㊴）：未命中 = id 类型最大值；is_valid_id 纯值判定
    EXPECT_EQ(h.get_id("missing"), DSNameHasherT<uint64_t>::kInvalidId);
    EXPECT_EQ(DSNameHasherT<uint64_t>::kInvalidId, UINT64_MAX);
    EXPECT_FALSE(DSNameHasherT<uint64_t>::is_valid_id(
        DSNameHasherT<uint64_t>::kInvalidId));
    EXPECT_TRUE(DSNameHasherT<uint64_t>::is_valid_id(0u));
    // static：零实例可用（不依赖 mapper/hasher 加载，㊵②）
    EXPECT_FALSE(DSNameHasherT<uint64_t>::is_valid_id(UINT64_MAX));

    // arena/偏移表形态：两名字节拼一池，槽位各自定位
    EXPECT_EQ(h.name_arena_, "alphabeta");
    EXPECT_EQ(h.name_table_.size(), 2u);
    EXPECT_EQ(h.name_table_[0].offset, 0u);
    EXPECT_EQ(h.name_table_[0].length, 5u);
    EXPECT_EQ(h.name_table_[1].offset, 5u);
    EXPECT_EQ(h.name_table_[1].length, 4u);
}

TEST(DSNameHasherTest, EmplaceDuplicateKeepsFirst) {
    // 重名保留首份返回既有 id（幂等登记，与 register_net 兜底同语义）
    DSNameHasherT<uint64_t> h;
    const uint64_t first = h.emplace("dup");
    const uint64_t second = h.emplace("dup");
    EXPECT_EQ(first, second);
    EXPECT_EQ(h.size(), 1u);
    EXPECT_EQ(h.get_name(second), "dup");
}

// ── 3. assign 指定 id 双写 + 空洞容忍 ────────────────────────────────

TEST(DSNameHasherTest, AssignSparseIdsTolerateHoles) {
    DSNameHasherT<uint64_t> h;
    // 指定 id 稀疏落位（instance id 从 1 起、fake cell 稀疏 id 场景）：
    // resize 容忍空洞——id 0 空置（空洞 = 空名占位）
    h.assign("one", 1);
    h.assign("three", 3);
    EXPECT_EQ(h.size(), 2u);
    EXPECT_EQ(h.get_id("one"), 1u);
    EXPECT_EQ(h.get_id("three"), 3u);
    EXPECT_EQ(h.get_name(1), "one");
    EXPECT_EQ(h.get_name(3), "three");
    // 空洞：id 0/2 未登记——域内（已 resize）返回空名占位，可判别
    EXPECT_EQ(h.get_name(0), "");
    EXPECT_EQ(h.get_name(2), "");
    // emplace 在空洞之后追加：id = 当前规模（偏移表尾部）
    const uint64_t next = h.emplace("four");
    EXPECT_EQ(next, 4u);
    EXPECT_EQ(h.get_name(next), "four");
    // assign 同名重挂：覆盖指向新 id（register_pin 重挂语义）；旧 id
    // 槽位清空洞（偏移表与 backend 保持一致——序列化重建无损）
    h.assign("one", 9);
    EXPECT_EQ(h.get_id("one"), 9u);
    EXPECT_EQ(h.get_name(9), "one");
    EXPECT_EQ(h.get_name(1), "");  // 旧槽位清空
    EXPECT_EQ(h.size(), 3u);       // 重挂不增规模
}

// ── 4. 位宽分组 + 实体别名 + backend 默认差异化（编译断言；㊳㊸52）──

static_assert(
    std::is_same_v<DSCellNameHasher,
                   DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>>);
static_assert(
    std::is_same_v<DSPinNameHasher,
                   DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>>);
static_assert(std::is_same_v<
              DSViaCellNameHasher,
              DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>>);
static_assert(
    std::is_same_v<DSLayerNameHasher,
                   DSNameHasherT<uint32_t, DSHasherBackendHash<uint32_t>>>);
static_assert(std::is_same_v<
              DSInstanceNameHasher,
              DSNameHasherT<uint64_t, DSHasherBackendHatrie<uint64_t>>>);
static_assert(
    std::is_same_v<DSNetNameHasher,
                   DSNameHasherT<uint64_t, DSHasherBackendHatrie<uint64_t>>>);
static_assert(
    std::is_same_v<decltype(DSCellNameHasher::kInvalidId), const uint32_t>);
static_assert(DSCellNameHasher::kInvalidId == UINT32_MAX);
static_assert(DSInstanceNameHasher::kInvalidId == UINT64_MAX);
static_assert(DSCellNameHasher::is_valid_id(0));
static_assert(!DSCellNameHasher::is_valid_id(UINT32_MAX));
// Backend 概念（裁定 52）编译期约束：两实现 + dummy 均满足
static_assert(DSNameBackendConcept<DSDummyNameBackend<uint64_t>, uint64_t>);
static_assert(DSNameBackendConcept<DSHasherBackendHash<uint32_t>, uint32_t>);
static_assert(
    DSNameBackendConcept<DSHasherBackendHatrie<uint64_t>, uint64_t>);

TEST(DSNameHasherTest, BothWidthGroupsWork) {
    // 32 位组（layer 场景直书实体别名，hash backend）
    DSLayerNameHasher h32;
    EXPECT_EQ(h32.emplace("M1"), 0u);
    EXPECT_EQ(h32.emplace("M2"), 1u);
    EXPECT_EQ(h32.get_id("M1"), 0u);
    EXPECT_EQ(h32.get_name(1), "M2");
    EXPECT_EQ(h32.get_id("nope"), DSLayerNameHasher::kInvalidId);

    // 64 位组（instance 场景直书实体别名，hat-trie backend）
    DSInstanceNameHasher h64;
    h64.assign("u1", 1);
    h64.assign("u2", 2);
    EXPECT_EQ(h64.get_id("u2"), 2u);
    EXPECT_EQ(h64.get_name(1), "u1");
    EXPECT_EQ(h64.get_id("nope"), DSInstanceNameHasher::kInvalidId);
}

// ── 6. backend 替换性（裁定 52）：dummy backend 实例化 + 基本增查 ──

TEST(DSNameHasherTest, DummyBackendReplaceability) {
    // 极简 backend 替换默认 backend——抽象面完备性证明（对外接口不变）
    DummyHasher h;
    const uint64_t a = h.emplace("alpha");
    h.assign("beta", 7);
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(h.get_id("alpha"), 0u);
    EXPECT_EQ(h.get_id("beta"), 7u);
    EXPECT_EQ(h.get_name(7), "beta");
    EXPECT_EQ(h.get_id("nope"), DummyHasher::kInvalidId);
    EXPECT_EQ(h.size(), 2u);

    // 序列化 round-trip：dummy 无原生加速段（恒空段）——读回经计数
    // 校验走权威段重建，行为一致
    CMString blob;
    FLY_ENCODE(h, blob);
    DummyHasher back;
    FLY_DECODE(blob, DummyHasher, back);
    EXPECT_EQ(back.size(), 2u);
    EXPECT_EQ(back.get_id("alpha"), 0u);
    EXPECT_EQ(back.get_name(7), "beta");
}

// ── 5. 序列化 round-trip（双段制：纯通用 / 双段两形态 + 空/空洞）────

TEST(DSNameHasherTest, SerializeRoundTrip) {
    DSNetNameHasher h;  // net 场景：local id 从 1 起、0 空置
    h.assign("n1", 1);
    h.assign("n2", 2);
    h.assign("VDD", 3);

    CMString blob;
    FLY_ENCODE(h, blob);
    DSNetNameHasher back;
    FLY_DECODE(blob, DSNetNameHasher, back);

    EXPECT_EQ(back.size(), 3u);
    EXPECT_EQ(back.get_id("n1"), 1u);
    EXPECT_EQ(back.get_id("VDD"), 3u);
    EXPECT_EQ(back.get_id("missing"), DSNetNameHasher::kInvalidId);
    EXPECT_EQ(back.get_name(1), "n1");
    EXPECT_EQ(back.get_name(3), "VDD");
    EXPECT_EQ(back.get_name(0), "");  // 空洞随序列化保留
}

TEST(DSNameHasherTest, SerializeRoundTripRebuildPath) {
    // 纯通用名集格式（加速段开关关闭——重建式读回路径）
    DSNetNameHasher h;
    h.backend_.native_cache_on_save_ = false;
    h.assign("n1", 1);
    h.assign("VDD", 2);

    CMString blob;
    FLY_ENCODE(h, blob);
    DSNetNameHasher back;
    FLY_DECODE(blob, DSNetNameHasher, back);

    EXPECT_EQ(back.size(), 2u);
    EXPECT_EQ(back.get_id("n1"), 1u);
    EXPECT_EQ(back.get_name(2), "VDD");
}

TEST(DSNameHasherTest, SerializeRoundTripU32HashBackend) {
    // 32 位组（hash backend）：同一路径 round-trip
    DSCellNameHasher h;
    h.emplace("INV_X1");
    h.emplace("BUF_X1");

    CMString blob;
    FLY_ENCODE(h, blob);
    DSCellNameHasher back;
    FLY_DECODE(blob, DSCellNameHasher, back);

    EXPECT_EQ(back.size(), 2u);
    EXPECT_EQ(back.get_id("INV_X1"), 0u);
    EXPECT_EQ(back.get_name(1), "BUF_X1");
}

TEST(DSNameHasherTest, SerializeRoundTripEmpty) {
    // 空 hasher round-trip（空 arena + 空偏移表 + 计数 0 + 空加速段）
    DSNetNameHasher h;
    CMString blob;
    FLY_ENCODE(h, blob);
    DSNetNameHasher back;
    FLY_DECODE(blob, DSNetNameHasher, back);
    EXPECT_EQ(back.size(), 0u);
    EXPECT_EQ(back.get_id("x"), DSNetNameHasher::kInvalidId);
}

// ── 7. htrie backend 一致性 + for_each 遍历导出 ──────────────────────

TEST(DSNameHasherTest, HatrieConsistencyAgainstReference) {
    // R8a 语义等价红线：hat-trie backend 与 unordered_map 参照在随机
    // 名集（层次前缀/总线位模式 + 稀疏空洞 assign）上行为一致
    struct Lcg {
        uint64_t s = 20260912u;
        uint64_t operator()() {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            return s >> 33;
        }
    } rng;

    DSNetNameHasher h;
    std::unordered_map<std::string, uint64_t> ref;
    auto make_name = [&rng](uint64_t i) {
        return "u" + std::to_string(rng() % 64) + "/data_[" +
               std::to_string(i % 1024) + "]_reg";
    };
    // 构造：emplace 700（重名保留首份语义）
    for (int i = 0; i < 700; ++i) {
        const std::string n = make_name(static_cast<uint64_t>(i));
        const uint64_t got = h.emplace(n);
        // 参照：已存在取既有 id；新名 id = emplace 后规模-1
        const uint64_t expect =
            ref.emplace(n, static_cast<uint64_t>(h.size() - 1)).first->second;
        EXPECT_EQ(got, expect);
    }
    // 构造：assign 稀疏 300（id 1000 起步长 3——域内大量空洞）
    for (int i = 0; i < 300; ++i) {
        const std::string n = make_name(static_cast<uint64_t>(1000 + i));
        const uint64_t id = 1000 + static_cast<uint64_t>(i) * 3;
        h.assign(n, id);
        ref[n] = id;
    }
    ASSERT_EQ(h.size(), ref.size());

    // 正向全量比对
    for (const auto& [n, id] : ref) {
        EXPECT_EQ(h.get_id(n), id) << n;
    }
    EXPECT_EQ(h.get_id("no_such_name"), DSNetNameHasher::kInvalidId);

    // 反向抽样比对：域内（含空洞）——非空洞槽位名必须在参照表且指向
    // 回该 id；空洞返回空名
    const uint64_t domain = h.name_table_.size();
    for (int i = 0; i < 500; ++i) {
        const uint64_t id = rng() % domain;
        if (h.name_table_[id].length == 0) {
            EXPECT_EQ(h.get_name(id), "");
        } else {
            const std::string n = h.get_name(id);
            const auto it = ref.find(n);
            ASSERT_NE(it, ref.end()) << "id=" << id;
            EXPECT_EQ(it->second, id) << n;
        }
    }

    // for_each 遍历导出：与参照集全量一致（backend 遍历契约）
    std::unordered_map<std::string, uint64_t> exported;
    h.for_each([&exported](const CMString& n, uint64_t id) {
        exported[n] = id;
    });
    EXPECT_EQ(exported.size(), ref.size());
    for (const auto& [n, id] : ref) {
        EXPECT_EQ(exported.at(n), id);
    }
}

// ── 8. DSBlockNames 伴生对象（㊵②：DSBlockNames_<i> 落盘形态锚定）───

TEST(DSBlockNamesTest, SerializeRoundTripBothHashers) {
    DSBlockNames names;  // 成员构造即非空（CMSharedPtr）
    names.block_name_ = "block_a";
    names.instance_names_->assign("u1", 1);
    names.instance_names_->assign("u2", 2);
    names.net_names_->assign("n1", 1);
    names.net_names_->assign("n2", 2);

    CMString blob;
    FLY_ENCODE(names, blob);
    DSBlockNames back;
    FLY_DECODE(blob, DSBlockNames, back);

    EXPECT_EQ(back.block_name_, "block_a");
    // 成员为 CMSharedPtr（构造即非空）——框架 shared_ptr 分支按值落盘
    ASSERT_TRUE(back.instance_names_ != nullptr);
    ASSERT_TRUE(back.net_names_ != nullptr);
    EXPECT_EQ(back.instance_names_->get_id("u1"), 1u);
    EXPECT_EQ(back.instance_names_->get_name(2), "u2");
    EXPECT_EQ(back.net_names_->get_id("n1"), 1u);
    EXPECT_EQ(back.net_names_->get_name(2), "n2");
    EXPECT_EQ(back.net_names_->get_id("ghost"), DSNetNameHasher::kInvalidId);
}

// ── 9-12. R8d：id→name 侧 LCP 后缀共享双形态（裁定 54/55）─────────────

// 层次前缀 + 总线位客户分布名（前缀冗余供 LCP 共享；i 内嵌保证唯一——
// LCG 每名独立子序列，形态与 HatrieConsistencyAgainstReference 同源）
static std::string make_hier_name(uint64_t i) {
    struct Lcg {
        uint64_t s;
        uint64_t operator()() {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            return s >> 33;
        }
    } rng{0x5eed1234u ^ (i * 2654435761ULL)};
    static const char* kScopes[8] = {"core", "soc", "ddr", "pcie",
                                     "usb",  "gpu", "npu", "top"};
    return std::string("top/") + kScopes[rng() % 8] + "/u_" +
           std::to_string(rng() % 4096) + "/inst_[" + std::to_string(i) +
           "]_reg_q_out";
}

TEST(DSNameHasherR8dTest, LcpFormRandomNamesMatchForm1) {
    // 随机名集（emplace 连续 + assign 稀疏空洞）双 hasher 同构注册：
    // 形态一保持 vs 封口形态二——全量域 get_name/get_id 逐位对比
    DSNetNameHasher form1;
    DSNetNameHasher sealed_lcp;
    for (int i = 0; i < 2000; ++i) {
        const std::string n = make_hier_name(static_cast<uint64_t>(i));
        ASSERT_EQ(form1.emplace(n), sealed_lcp.emplace(n));
    }
    // assign 稀疏落位（空洞域，id 5000 起步长 3）
    for (int i = 0; i < 800; ++i) {
        const std::string n =
            "sparse/" + make_hier_name(static_cast<uint64_t>(10000 + i * 3));
        const uint64_t id = 5000 + static_cast<uint64_t>(i) * 3;
        form1.assign(n, id);
        sealed_lcp.assign(n, id);
    }
    ASSERT_EQ(form1.size(), sealed_lcp.size());
    sealed_lcp.finalize_for_save(true);
    ASSERT_TRUE(sealed_lcp.is_lcp_form());
    // 形态一存储已释放、域保持（空洞占位随 id→rank 表保留）
    EXPECT_TRUE(sealed_lcp.name_arena_.empty());
    EXPECT_TRUE(sealed_lcp.name_table_.empty());
    EXPECT_EQ(sealed_lcp.name_domain(), form1.name_domain());

    // 全量域对比：get_name 逐位相等（空洞 = 空串两形态一致）；rank 序
    // 命中 checkpoint（rem=0 直取）与回退链（rem≠0 逐级截断）全覆盖
    const uint64_t domain = form1.name_domain();
    for (uint64_t id = 0; id < domain; ++id) {
        EXPECT_EQ(sealed_lcp.get_name(id), form1.get_name(id)) << "id=" << id;
    }
    // 正向：全部登记名 get_id 相等（backend 与 id→name 形态无关）
    sealed_lcp.for_each([&](const CMString& n, uint64_t id) {
        EXPECT_EQ(form1.get_id(n), id) << n;
        EXPECT_EQ(sealed_lcp.get_id(n), id) << n;
    });
}

TEST(DSNameHasherR8dTest, LcpFormHolesAndEmptyDomain) {
    // 空洞占位语义封口后不变：未登记 id 域内返回空串
    DSNetNameHasher h;
    h.assign("one", 1);
    h.assign("three", 3);
    h.finalize_for_save(true);
    EXPECT_EQ(h.get_name(0), "");
    EXPECT_EQ(h.get_name(2), "");
    EXPECT_EQ(h.get_name(1), "one");
    EXPECT_EQ(h.get_name(3), "three");
    EXPECT_EQ(h.name_domain(), 4u);

    // 空 hasher 封口：形态置位、零结构、backend 可查（空）
    DSNetNameHasher empty;
    empty.finalize_for_save(true);
    EXPECT_TRUE(empty.is_lcp_form());
    EXPECT_EQ(empty.size(), 0u);
    EXPECT_EQ(empty.name_domain(), 0u);
    EXPECT_EQ(empty.get_id("x"), DSNetNameHasher::kInvalidId);
}

TEST(DSNameHasherR8dTest, LcpFormLongClusterTruncation) {
    // 回退链逐级截断坑（基准报告 §4）：中间名 append 后必须 resize 截断
    // ——构造相邻名高 LCP 且尾长参差的长名簇（尾字节远长于后继 LCP），
    // 200 名跨 3 个 checkpoint 窗口（直取/回退两路径全覆盖）
    DSNetNameHasher form1;
    DSNetNameHasher sealed_lcp;
    std::vector<std::string> names;
    for (int i = 0; i < 200; ++i) {
        // 同 scope 长前缀 + 序号 + 长度参差尾（i%13+5 个 'x'）——相邻名
        // LCP 停在序号/尾内，中间名尾部残留量大（截断路径必经）
        names.push_back("top/scope_cluster_a/module_long_prefix_name_" +
                        std::to_string(i) + "_tail_pad_" +
                        std::string(static_cast<size_t>(i % 13) + 5, 'x'));
    }
    // 前缀链（p/pp/ppp…）：lcp = 短名全长、后缀长 0 边界
    for (int i = 1; i <= 10; ++i) {
        names.push_back(std::string(static_cast<size_t>(i), 'p'));
    }
    // 注：空名不入用例——形态一 len 0 即空洞哨兵，空名与空洞不可区分
    //（名字域契约非空，DEF 解析不产生空名），形态二沿用同一判别。
    for (const auto& n : names) {
        form1.emplace(n);
        sealed_lcp.emplace(n);
    }
    sealed_lcp.finalize_for_save(true);
    ASSERT_TRUE(sealed_lcp.is_lcp_form());
    // 逐 id 与形态一/原始名集双比对（emplace 序 = id 序）
    for (size_t i = 0; i < names.size(); ++i) {
        EXPECT_EQ(sealed_lcp.get_name(static_cast<uint64_t>(i)), names[i])
            << "name#" << i;
        EXPECT_EQ(sealed_lcp.get_name(static_cast<uint64_t>(i)),
                  form1.get_name(static_cast<uint64_t>(i)))
            << "name#" << i;
    }
    EXPECT_EQ(sealed_lcp.lcp_suffix_table_.size(), names.size());
    // checkpoint 表规模：ceil(n/64) 全量名偏移 + 尾哨兵
    const size_t n = names.size();
    EXPECT_EQ(sealed_lcp.lcp_ckpt_offsets_.size(), (n + 63) / 64 + 1);
}

TEST(DSNameHasherR8dTest, ForEachNameByRankBothFormsSameOrder) {
    // rank 序批量遍历（裁定 55③）：两形态产出同一 rank 序（名字典序）
    DSNetNameHasher form1;
    DSNetNameHasher sealed_lcp;
    for (int i = 0; i < 500; ++i) {
        const std::string n =
            make_hier_name(static_cast<uint64_t>(7000 + i));
        form1.emplace(n);
        sealed_lcp.emplace(n);
    }
    for (int i = 0; i < 150; ++i) {
        const std::string n = "sparse/" +
            make_hier_name(static_cast<uint64_t>(30000 + i * 7));
        const uint64_t id = 2000 + static_cast<uint64_t>(i) * 5;
        form1.assign(n, id);
        sealed_lcp.assign(n, id);
    }

    using Entry = std::pair<uint64_t, std::string>;
    auto collect = [](const DSNetNameHasher& h) {
        std::vector<Entry> out;
        h.for_each_name_by_rank(
            [&out](uint64_t id, const CMString& name) {
                out.emplace_back(id, name);
            });
        return out;
    };

    const auto form1_seq = collect(form1);
    ASSERT_EQ(form1_seq.size(), form1.size());
    // 名字典序严格递增（rank 序语义）且 id 指向与 backend 全集一致
    std::unordered_map<std::string, uint64_t> ref;
    form1.for_each([&ref](const CMString& n, uint64_t id) { ref[n] = id; });
    for (size_t i = 1; i < form1_seq.size(); ++i) {
        EXPECT_LT(form1_seq[i - 1].second, form1_seq[i].second)
            << "rank=" << i;
    }
    for (const auto& e : form1_seq) {
        EXPECT_EQ(ref.at(e.second), e.first);
    }

    sealed_lcp.finalize_for_save(true);
    const auto lcp_seq = collect(sealed_lcp);
    ASSERT_EQ(lcp_seq.size(), form1_seq.size());
    for (size_t i = 0; i < form1_seq.size(); ++i) {
        EXPECT_EQ(lcp_seq[i].first, form1_seq[i].first) << "rank=" << i;
        EXPECT_EQ(lcp_seq[i].second, form1_seq[i].second) << "rank=" << i;
    }
}

TEST(DSNameHasherR8dTest, SealedReadOnlyAndFalseNoop) {
    DSNetNameHasher h;
    h.emplace("b");
    h.emplace("a");
    // false 恒无操作（形态一默认路径零变化——arena/偏移表原样）
    h.finalize_for_save(false);
    EXPECT_FALSE(h.is_lcp_form());
    EXPECT_EQ(h.name_arena_, "ba");
    EXPECT_EQ(h.name_table_.size(), 2u);

    h.finalize_for_save(true);
    EXPECT_TRUE(h.is_lcp_form());
    // 封口后构建期接口拒绝（快速失败防半更新态）
    EXPECT_THROW(h.emplace("c"), std::logic_error);
    EXPECT_THROW(h.assign("d", 5), std::logic_error);
    // 查询面封口后可用
    EXPECT_EQ(h.get_id("a"), 1u);
    EXPECT_EQ(h.get_name(0), "b");
    // 幂等：重复封口无操作（结构不重建）
    const size_t suffix_bytes = h.lcp_suffix_arena_.size();
    h.finalize_for_save(true);
    EXPECT_EQ(h.lcp_suffix_arena_.size(), suffix_bytes);
    EXPECT_TRUE(h.is_lcp_form());
}

TEST(DSNameHasherR8dTest, LcpFormSerializeRoundTrip) {
    // 形态二落盘 round-trip：标记位自识别读回封口形态（空洞域保留）
    DSNetNameHasher h;
    h.emplace("top/mod_a/inst_1_reg");
    h.emplace("top/mod_a/inst_12_reg");
    h.assign("sparse/x_long_tail_name", 500);
    h.finalize_for_save(true);

    CMString blob;
    FLY_ENCODE(h, blob);
    DSNetNameHasher back;
    FLY_DECODE(blob, DSNetNameHasher, back);
    EXPECT_TRUE(back.is_lcp_form());
    EXPECT_EQ(back.size(), 3u);
    EXPECT_EQ(back.name_domain(), 501u);
    EXPECT_EQ(back.get_name(0), "top/mod_a/inst_1_reg");
    EXPECT_EQ(back.get_name(1), "top/mod_a/inst_12_reg");
    EXPECT_EQ(back.get_name(500), "sparse/x_long_tail_name");
    EXPECT_EQ(back.get_name(250), "");  // 空洞随形态二保留
    EXPECT_EQ(back.get_id("top/mod_a/inst_12_reg"), 1u);
    EXPECT_EQ(back.get_id("sparse/x_long_tail_name"), 500u);
}

TEST(DSNameHasherR8dTest, LcpFormSerializeRoundTripRebuildPath) {
    // 形态二 + 纯通用格式（加速段空 → 计数校验触发 LCP 形态 backend
    // 重建路径）
    DSNetNameHasher h;
    h.backend_.native_cache_on_save_ = false;
    h.emplace("aa/bb_x1");
    h.emplace("aa/bb_x2");
    h.finalize_for_save(true);

    CMString blob;
    FLY_ENCODE(h, blob);
    DSNetNameHasher back;
    FLY_DECODE(blob, DSNetNameHasher, back);
    EXPECT_TRUE(back.is_lcp_form());
    EXPECT_EQ(back.size(), 2u);
    EXPECT_EQ(back.get_id("aa/bb_x1"), 0u);
    EXPECT_EQ(back.get_id("aa/bb_x2"), 1u);
    EXPECT_EQ(back.get_name(0), "aa/bb_x1");
    EXPECT_EQ(back.get_name(1), "aa/bb_x2");
}

TEST(DSNameHasherR8dTest, LcpFormSerializeRoundTripEmpty) {
    // 空封口形态 round-trip
    DSNetNameHasher h;
    h.finalize_for_save(true);
    CMString blob;
    FLY_ENCODE(h, blob);
    DSNetNameHasher back;
    FLY_DECODE(blob, DSNetNameHasher, back);
    EXPECT_TRUE(back.is_lcp_form());
    EXPECT_EQ(back.size(), 0u);
    EXPECT_EQ(back.get_id("x"), DSNetNameHasher::kInvalidId);
}

TEST(DSNameHasherR8dTest, LightweightPerfObservation) {
    // 轻量性能观测（不强制基准——裁定 54 全量基准存档
    // .work/bench_lcp/；供 alpha 文档参考）：10 万名客户分布名集，
    // 内存自算对照形态一 + 随机 get_name 采样计时。断言仅正确性，
    // 性能数字只打印。
    struct Lcg {
        uint64_t s = 20260912u;
        uint64_t operator()() {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            return s >> 33;
        }
    } rng;
    static const char* kScopes[8] = {"core", "soc", "ddr", "pcie",
                                     "usb",  "gpu", "npu", "top"};
    constexpr size_t kN = 100000;
    DSNetNameHasher h;
    std::vector<std::string> names;
    names.reserve(kN);
    for (size_t i = 0; i < kN; ++i) {
        names.push_back(std::string("top/") + kScopes[rng() % 8] + "/u_" +
                        std::to_string(rng() % 4096) + "/inst_[" +
                        std::to_string(i) + "]_reg_q_out");
        h.assign(names.back(), static_cast<uint64_t>(i + 1));  // id 1..kN
    }

    // 计时辅助：随机 id get_name（结果累计防消除；返回毫秒 + 吞吐字节）
    auto time_random_get = [&h, &rng](int iters, uint64_t* sink) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) {
            *sink += h.get_name(static_cast<uint64_t>(rng() % kN) + 1).size();
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };
    uint64_t sink1 = 0;
    const double form1_ms = time_random_get(100000, &sink1);

    // 形态一内存自算（capacity 口径）后封口
    const size_t form1_bytes =
        h.name_arena_.capacity() +
        h.name_table_.capacity() * sizeof(DSNameSlot);
    h.finalize_for_save(true);
    ASSERT_TRUE(h.is_lcp_form());
    const size_t lcp_bytes =
        h.lcp_suffix_arena_.capacity() +
        h.lcp_suffix_table_.capacity() * sizeof(DSSuffixSlot) +
        h.id_to_rank_.capacity() * sizeof(uint32_t) +
        h.lcp_ckpt_arena_.capacity() +
        h.lcp_ckpt_offsets_.capacity() * sizeof(uint64_t);
    uint64_t sink2 = 0;
    const double lcp_ms = time_random_get(100000, &sink2);

    // 正确性红线（观测用例同样保真）：随机 1000 id 与注册 oracle 全等
    for (int i = 0; i < 1000; ++i) {
        const size_t k = static_cast<size_t>(rng() % kN);
        EXPECT_EQ(h.get_name(static_cast<uint64_t>(k) + 1), names[k]);
    }
    EXPECT_GT(sink1 + sink2, 0u);

    std::printf(
        "R8D-LCP-PERF names=%zu form1_bytes=%zu lcp_bytes=%zu "
        "saving=%.1f%% form1_ms=%.0f lcp_ms=%.0f degradation=%.1fx\n",
        kN, form1_bytes, lcp_bytes,
        100.0 * (1.0 - static_cast<double>(lcp_bytes) /
                           static_cast<double>(form1_bytes)),
        form1_ms, lcp_ms, lcp_ms / form1_ms);
}

}  // namespace
