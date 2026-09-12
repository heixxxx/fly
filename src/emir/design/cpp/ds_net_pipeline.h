#pragma once

// =============================================================================
// S5b 网内容责任链（裁定 ③④⑨⑩⑪⑫；仿 S5a 责任链形态，方案
// design-db-phase2-plan.md §2.3）——一个网的内容顺序流经链节点，每节点
// 处理相应部分并落批追加产物；新功能 = 在链中登记新节点，不改既有节点。
//
// 链组织（同 DSInstancePipeline）：
//   DSNetContext    链上传递的可变上下文（单网原解析数据 + 环境引用 +
//                   节点产出 + 错误标记）
//   DSNetHandler    抽象节点（name + handle）
//   DSNetPipeline   固定顺序执行，节点置 error 即停
//
// S5b 三节点（首批）：
//   DSNetConnectionParseNode  连接项解析：网名对齐 S5a local net id（⑨）
//                             + 连接表保留（S7 并查集输入）+ block port
//                             引用判别（defi instance() = "PIN"）；
//   DSNetGeometryExpandNode   路由几何展开：wire 段（layer id + 宽度——
//                             special 显式 / 普通 net 回填 stack 层缺省
//                             宽）与 rect 项（层引用未定义 → 条目级丢弃
//                             + 计数，DSGN::0010）；via 命名引用解析
//                             （⑪ via cell 权威表：⑫ design:: 前缀名
//                             优先 → plain 名回退；未定义跳过 + 计数，
//                             DSGN::0008 数据源）；VIADATA 阵列展开为
//                             多个 via instance（⑩ 专用 id 空间从 1 起、
//                             无 name）；
//   DSNetDensityNode          金属/通孔计数进 DSDensityGrid 逐层分列通
//                             道（⑥ 分类分层保存）：wire 段按宽度展开
//                             为段矩形、rect 原样、via instance 按 via
//                             cell 的 cut 图形平移（分层键 = cut 层 id，
//                             未判定回退 bottom 层）。
//
// 批处理（裁定 ③ 控内存峰值）：适配层（ds_parse_def_nets）在 Si2 流式
// 单遍回调中按网收集 DSNetContext，达到批界（alpha 键 net_batch_size）
// 即走一遍链并将产物落批（追加进 DSNetBuildData 后释放批缓冲）；单流
// 语法约束不变，批内链复用（一次构建多次 run）。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_types.h>
#include <geometry/cpp/geometry_types.h>

#include <memory>

namespace fly {

// —— 单网原解析数据（适配层从 defiNet 摘取；坐标已换算全局 DBU）——

struct DSNetRawConnection {
    CMString instance_name;  // "PIN" = block 级 port 引用
    CMString pin_name;
};

struct DSNetRawWire {
    CMString layer_name;
    // 全局 DBU；0 = 未给宽度（普通 net），由节点回填 stack 层缺省宽
    int32_t width_dbu = 0;
    CMVector<GEOPoint> points;
};

struct DSNetRawRect {
    CMString layer_name;
    GEORect rect;
};

// via 引用：num_x * num_y > 1 = VIADATA 阵列（special net），展开为
// (x + i·step_x, y + j·step_y) 网格位置的多个 via instance（⑩）
struct DSNetRawVia {
    CMString via_name;
    int32_t x = 0;
    int32_t y = 0;
    int32_t num_x = 1;
    int32_t num_y = 1;
    int32_t step_x = 0;
    int32_t step_y = 0;
};

// 链上传递的可变上下文：一个网 + 环境观察引用 + 节点产出。环境引用为
// 非拥有观察（裸指针仅限非拥有观察，DEVELOPMENT_GUIDELINES §16）；
// 生命周期由调用方保证覆盖 run。
struct DSNetContext {
    // —— 输入（单网原解析数据）——
    CMString net_name;
    bool is_special = false;
    CMVector<DSNetRawConnection> connections;
    CMVector<DSNetRawWire> wires;
    CMVector<DSNetRawRect> rects;
    CMVector<DSNetRawVia> vias;

    // —— 环境（非拥有观察）——
    const DSStack* stack = nullptr;                  // 层名解析 + 缺省宽
    const DSDesign* design = nullptr;                // via cell 权威表（⑪）
    const DSBlockBuildData* block_data = nullptr;    // local net namemap（⑨）
    DSNetBuildData* net_data = nullptr;              // 产物容器（落批追加）
    // DEF DESIGN 名（⑫ 前缀 via 解析回退用）
    CMString design_name;

    // —— 节点产出 ——
    // DSNetConnectionParseNode：对齐到的 local net id；0 = 网名未收录
    //（防御兜底，后续节点跳过该网；R7 ㊳ 64 位）
    uint64_t local_net_id = 0;

    // 错误标记：任一节点置位后 run 即停（环境缺失等调用方契约错误）
    bool error = false;
};

// 抽象链节点
class DSNetHandler {
public:
    virtual ~DSNetHandler() = default;
    // 节点名（日志/诊断用）
    virtual const char* name() const = 0;
    // 处理一个网的上下文（就地修改收集）
    virtual void handle(DSNetContext& ctx) = 0;
};

// 链组织：固定顺序执行；节点置 error 后停止
class DSNetPipeline {
public:
    // 登记节点（链序 = 登记序；节点所有权归管道）
    void add(CMUniquePtr<DSNetHandler> handler);
    size_t handler_count() const { return handlers_.size(); }
    // 顺序执行（可重复 run：批内链复用）；ctx.error 置位即停
    void run(DSNetContext& ctx) const;

private:
    CMVector<CMUniquePtr<DSNetHandler>> handlers_;
};

// —— S5b 三节点（首批）———————————————————————————————

// 节点 1：连接项解析（⑨ local net id 对齐 + 连接表保留 + port 判别）
class DSNetConnectionParseNode : public DSNetHandler {
public:
    const char* name() const override { return "NetConnectionParseNode"; }
    void handle(DSNetContext& ctx) override;
};

// 节点 2：路由几何展开（wire/rect/via 引用解析与 via instance 生成）
class DSNetGeometryExpandNode : public DSNetHandler {
public:
    const char* name() const override { return "NetGeometryExpandNode"; }
    void handle(DSNetContext& ctx) override;
};

// 节点 3：金属/通孔计数密度通道（⑥ 逐层分列）
class DSNetDensityNode : public DSNetHandler {
public:
    const char* name() const override { return "NetDensityNode"; }
    void handle(DSNetContext& ctx) override;
};

// S5b 默认链装配（三节点按序）；导出面供 Python 编排取用
DSNetPipeline ds_make_nets_pipeline();

// via 名解析（⑪/⑫）：⑫ design:: 前缀名优先（本 DEF VIAS 段登记条目，
// 防 tech/cell lef 同名 via 遮蔽），未命中回退 plain 名；双未命中返回
// DSDesign::kInvalidId（调用方兜底跳过 + 计数）
uint32_t ds_resolve_via_cell(const DSDesign& design,
                             const CMString& design_name,
                             const CMString& via_name);

}  // namespace fly
