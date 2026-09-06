#include <gtest/gtest.h>
#include <message/cpp/message_registry.h>
#include <message/cpp/message_macros.h>
#include <log/cpp/logger.h>

#include <common/testing/cpp/test_helpers.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fly {

class MessageRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // MessageRegistry 是单例，每个测试前重置全部状态（白名单/计数/配额）。
        reg.reset_for_testing();
    }
    MessageRegistry& reg = MessageRegistry::instance();
};

// 白名单 + 级别绑定：未注册的 id 查不到级别（get_level 返回 false）。
TEST_F(MessageRegistryTest, UnregisteredIdHasNoLevel) {
    EXPECT_FALSE(reg.is_registered("TEST_UNREG::0001"));
    fly::LogLevel lvl;
    EXPECT_FALSE(reg.get_level("TEST_UNREG::0001", lvl));
}

// MSG 宏对未注册 id：必须打 WARN 提示后丢弃（用户裁定：不可默认丢弃且无
// 提示——未注册是编程错误，与配额超限的设计性静默不同）。WARN 通道立即
// flush，经 Logger 落盘后断言。Logger init 到临时目录（单测进程默认无文件
// 通道，走 cerr 无法断言）。
TEST_F(MessageRegistryTest, UnregisteredMsgEmitsWarning) {
    namespace fs = std::filesystem;
    CMString dir = fly::test::qa_tmp_dir("fly_msg_test");
    fs::create_directories(dir);
    Logger::init(dir, 0);

    MSG("TEST_UNREG::0099", 1, "this message will be dropped");

    bool found = false;
    std::ifstream ifs(dir + "/master.log");
    CMString line;
    while (std::getline(ifs, line)) {
        if (line.find("unregistered message id 'TEST_UNREG::0099'") != CMString::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    fs::remove_all(dir);
}

// 白名单 + 级别绑定：注册后可查询，级别正确。
TEST_F(MessageRegistryTest, RegisteredIdReturnsBoundLevel) {
    reg.register_id("TEST_REG::0001", fly::LogLevel::WARN);
    EXPECT_TRUE(reg.is_registered("TEST_REG::0001"));
    fly::LogLevel lvl = fly::LogLevel::INFO;
    ASSERT_TRUE(reg.get_level("TEST_REG::0001", lvl));
    EXPECT_EQ(lvl, fly::LogLevel::WARN);
}

// 级别绑定：重复注册以最后一次为准。
TEST_F(MessageRegistryTest, ReregisterOverridesLevel) {
    reg.register_id("TEST_REREG::0001", fly::LogLevel::INFO);
    reg.register_id("TEST_REREG::0001", fly::LogLevel::ERROR);
    fly::LogLevel lvl = fly::LogLevel::INFO;
    ASSERT_TRUE(reg.get_level("TEST_REREG::0001", lvl));
    EXPECT_EQ(lvl, fly::LogLevel::ERROR);
}

// 两套计数模型：trigger 计数 = 调用次数（含丢弃），emit 计数 = 成功输出次数。
TEST_F(MessageRegistryTest, TwoCountModelTriggerVsEmit) {
    reg.register_id("TEST_TC::0001", fly::LogLevel::INFO);
    // global 默认 20，发 22 次：前 20 次输出（emit），后 2 次丢弃。
    for (int i = 0; i < 22; ++i) {
        bool emitted = reg.try_emit("TEST_TC::0001");
        EXPECT_EQ(emitted, i < 20) << "第 " << i << " 次 emit 判定错误";
    }
    // trigger 计数 = 22（总调用，含丢弃）→ summary
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_TC::0001"], 22u);
}

// global 配额：默认 20，前 20 次 emit 通过，第 21 次起丢弃。
TEST_F(MessageRegistryTest, GlobalLimitDefault20) {
    reg.register_id("TEST_LIM20::0001", fly::LogLevel::INFO);
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(reg.try_emit("TEST_LIM20::0001")) << "第 " << i << " 次应通过";
    }
    EXPECT_FALSE(reg.try_emit("TEST_LIM20::0001"));
    EXPECT_FALSE(reg.try_emit("TEST_LIM20::0001"));
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_LIM20::0001"], 22u);  // trigger 含丢弃
}

// global 配额：-1 = 不限制。
TEST_F(MessageRegistryTest, GlobalLimitUnlimited) {
    reg.set_global_limit(-1);
    reg.register_id("TEST_UNLIM::0001", fly::LogLevel::INFO);
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(reg.try_emit("TEST_UNLIM::0001"));
    }
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_UNLIM::0001"], 100u);
}

// global 配额：0 = 完全禁止（第一次就丢弃，trigger 仍计 1）。
TEST_F(MessageRegistryTest, GlobalLimitZero) {
    reg.set_global_limit(0);
    reg.register_id("TEST_ZERO::0001", fly::LogLevel::INFO);
    EXPECT_FALSE(reg.try_emit("TEST_ZERO::0001"));
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_ZERO::0001"], 1u);  // trigger 仍计
}

// global 配额：显式设置 N。
TEST_F(MessageRegistryTest, GlobalLimitCustom) {
    reg.set_global_limit(5);
    reg.register_id("TEST_CUSTOM::0001", fly::LogLevel::INFO);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(reg.try_emit("TEST_CUSTOM::0001"));
    }
    EXPECT_FALSE(reg.try_emit("TEST_CUSTOM::0001"));
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_CUSTOM::0001"], 6u);  // 5 输出 + 1 丢弃
}

// domain 配额：未设 domain 时，同 domain 多个 id 各自独立走 global。
TEST_F(MessageRegistryTest, DomainLimitDefaultUnlimited) {
    reg.register_id("TEST_DOMDEF::0001", fly::LogLevel::INFO);
    reg.register_id("TEST_DOMDEF::0002", fly::LogLevel::INFO);
    EXPECT_TRUE(reg.try_emit("TEST_DOMDEF::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMDEF::0002"));
    auto dom = reg.trigger_domain_counts_snapshot();
    EXPECT_EQ(dom["TEST_DOMDEF"], 2u);
}

// domain 配额：语义同 global（每 id 独立计数），仅对该 domain 内 id 生效。
TEST_F(MessageRegistryTest, DomainLimitPerIdIndependent) {
    reg.set_domain_limit("TEST_DOMSHARE", 3);
    reg.register_id("TEST_DOMSHARE::0001", fly::LogLevel::INFO);
    reg.register_id("TEST_DOMSHARE::0002", fly::LogLevel::INFO);

    // id1 独立 3 次：前 3 次 emit 通过，第 4 次丢弃。
    EXPECT_TRUE(reg.try_emit("TEST_DOMSHARE::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMSHARE::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMSHARE::0001"));
    EXPECT_FALSE(reg.try_emit("TEST_DOMSHARE::0001"));
    // id2 同样独立 3 次（与 id1 互不影响）。
    EXPECT_TRUE(reg.try_emit("TEST_DOMSHARE::0002"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMSHARE::0002"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMSHARE::0002"));
    EXPECT_FALSE(reg.try_emit("TEST_DOMSHARE::0002"));

    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_DOMSHARE::0001"], 4u);  // 含丢弃
    EXPECT_EQ(trigger["TEST_DOMSHARE::0002"], 4u);
}

// per-id 配额：覆盖 global。
TEST_F(MessageRegistryTest, PerIdLimitOverridesGlobal) {
    reg.set_global_limit(100);
    reg.set_id_limit("TEST_PERID1::0001", 2);
    reg.register_id("TEST_PERID1::0001", fly::LogLevel::INFO);

    EXPECT_TRUE(reg.try_emit("TEST_PERID1::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_PERID1::0001"));
    EXPECT_FALSE(reg.try_emit("TEST_PERID1::0001"));
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_PERID1::0001"], 3u);
}

// 链式优先级：per-id > domain。设了 per-id 的 id 只看 per-id。
TEST_F(MessageRegistryTest, PerIdLimitShadowsDomain) {
    reg.set_domain_limit("TEST_SHADOW", 100);
    reg.set_id_limit("TEST_SHADOW::0001", 2);
    reg.register_id("TEST_SHADOW::0001", fly::LogLevel::INFO);

    EXPECT_TRUE(reg.try_emit("TEST_SHADOW::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_SHADOW::0001"));
    EXPECT_FALSE(reg.try_emit("TEST_SHADOW::0001"));
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_SHADOW::0001"], 3u);
}

// 链式优先级：per-id 未设时 domain 接管（domain > global）。
TEST_F(MessageRegistryTest, DomainShadowsGlobalWhenNoPerId) {
    reg.set_global_limit(2);
    reg.set_domain_limit("TEST_DOMFIRST", 4);
    reg.register_id("TEST_DOMFIRST::0001", fly::LogLevel::INFO);

    // domain=4 覆盖 global=2，前 4 次 emit 通过（若走 global=2 第 3 次就应超限）。
    EXPECT_TRUE(reg.try_emit("TEST_DOMFIRST::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMFIRST::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMFIRST::0001"));
    EXPECT_TRUE(reg.try_emit("TEST_DOMFIRST::0001"));
    EXPECT_FALSE(reg.try_emit("TEST_DOMFIRST::0001"));
}

// 链式优先级：三层都设时只看 per-id。
TEST_F(MessageRegistryTest, PerIdWinsWhenAllThreeSet) {
    reg.set_global_limit(50);
    reg.set_domain_limit("TEST_ALL", 30);
    reg.set_id_limit("TEST_ALL::0001", 3);
    reg.register_id("TEST_ALL::0001", fly::LogLevel::INFO);

    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(reg.try_emit("TEST_ALL::0001"));
    }
    EXPECT_FALSE(reg.try_emit("TEST_ALL::0001"));
}

// 【关键】动态修改配额：小→大，emit 计数解耦，调大后可继续输出（不因 trigger 过大失效）。
TEST_F(MessageRegistryTest, DynamicLimitSmallToLarge) {
    reg.set_global_limit(2);
    reg.register_id("TEST_DYN1::0001", fly::LogLevel::INFO);
    // 触发 5 次：前 2 次 emit（emit_count=2），后 3 次丢弃。trigger=5。
    for (int i = 0; i < 5; ++i) {
        reg.try_emit("TEST_DYN1::0001");
    }
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_DYN1::0001"], 5u);

    // 调大配额到 4：emit_count=2 < 4，应能继续 emit 2 次（不会因 trigger=5 失效）。
    reg.set_global_limit(4);
    EXPECT_TRUE(reg.try_emit("TEST_DYN1::0001"));   // emit_count=3
    EXPECT_TRUE(reg.try_emit("TEST_DYN1::0001"));   // emit_count=4
    EXPECT_FALSE(reg.try_emit("TEST_DYN1::0001"));  // emit_count=4 >= 4，超限
}

// 【关键】动态修改配额：大→小，emit 计数已超新配额，立即受限。
TEST_F(MessageRegistryTest, DynamicLimitLargeToSmall) {
    reg.set_global_limit(10);
    reg.register_id("TEST_DYN2::0001", fly::LogLevel::INFO);
    // 触发 3 次：全部 emit（emit_count=3，未超 10）。
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(reg.try_emit("TEST_DYN2::0001"));
    }
    // 调小配额到 2：emit_count=3 >= 2，立即超限，后续不输出。
    reg.set_global_limit(2);
    EXPECT_FALSE(reg.try_emit("TEST_DYN2::0001"));
    EXPECT_FALSE(reg.try_emit("TEST_DYN2::0001"));
    // trigger 仍累加（summary 反映真实触发次数）。
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_DYN2::0001"], 5u);  // 3 + 2 丢弃
}

// 配额快照：get_all_limits 反映当前设置。
TEST_F(MessageRegistryTest, GetAllLimitsSnapshot) {
    reg.set_global_limit(15);
    reg.set_domain_limit("DOMA", 100);
    reg.set_domain_limit("DOMB", 200);
    reg.set_id_limit("DOMA::0001", 5);
    reg.set_id_limit("DOMA::0002", 6);

    int32_t global;
    CMVector<CMString> dom_keys, id_keys;
    CMVector<int32_t> dom_vals, id_vals;
    reg.get_all_limits(global, dom_keys, dom_vals, id_keys, id_vals);

    EXPECT_EQ(global, 15);
    EXPECT_EQ(dom_keys.size(), 2u);
    EXPECT_EQ(id_keys.size(), 2u);
}

// 配额快照：apply_limits_snapshot 整体替换，不清零计数。
TEST_F(MessageRegistryTest, ApplyLimitsSnapshotKeepsCounts) {
    reg.set_global_limit(3);
    reg.register_id("TEST_SNAP::0001", fly::LogLevel::INFO);
    // 触发 2 次（emit_count=2, trigger=2）。
    reg.try_emit("TEST_SNAP::0001");
    reg.try_emit("TEST_SNAP::0001");

    // 应用新快照：global=5, per-id=1。
    CMVector<CMString> dom_keys, id_keys{"TEST_SNAP::0001"};
    CMVector<int32_t> dom_vals, id_vals{1};
    reg.apply_limits_snapshot(5, dom_keys, dom_vals, id_keys, id_vals);

    // 计数未清零：trigger 仍为 2。
    auto trigger = reg.trigger_id_counts_snapshot();
    EXPECT_EQ(trigger["TEST_SNAP::0001"], 2u);
    // 新配额 per-id=1，emit_count=2 >= 1，应超限（保留 emit 计数）。
    EXPECT_FALSE(reg.try_emit("TEST_SNAP::0001"));
}

// extract_domain 工具函数。
TEST_F(MessageRegistryTest, ExtractDomain) {
    EXPECT_EQ(MessageRegistry::extract_domain("SOLVER::0047"), "SOLVER");
    EXPECT_EQ(MessageRegistry::extract_domain("SYS::0001"), "SYS");
    EXPECT_EQ(MessageRegistry::extract_domain("NOSEPARATOR"), "NOSEPARATOR");
}

}  // namespace fly
