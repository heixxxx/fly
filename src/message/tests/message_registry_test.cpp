#include <gtest/gtest.h>
#include <message/cpp/message_registry.h>

namespace fly {

class MessageRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // MessageRegistry 是单例，每个测试前重置全部状态（白名单/计数/配额），
        // 避免跨测试污染（如 set_id_limit 的全局副作用）。
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

// id 配额：默认 20，前 20 次通过，第 21 次起丢弃，但计数仍累加。
TEST_F(MessageRegistryTest, IdLimitDefault20) {
    reg.register_id("TEST_LIM20::0001", fly::LogLevel::INFO);
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(reg.try_consume("TEST_LIM20::0001")) << "第 " << i << " 次应通过";
    }
    // 第 21、22 次超限丢弃
    EXPECT_FALSE(reg.try_consume("TEST_LIM20::0001"));
    EXPECT_FALSE(reg.try_consume("TEST_LIM20::0001"));
    // 但计数累加到 22
    auto snapshot = reg.id_counts_snapshot();
    EXPECT_EQ(snapshot["TEST_LIM20::0001"], 22u);
}

// id 配额：-1 = 不限制。
TEST_F(MessageRegistryTest, IdLimitUnlimited) {
    reg.set_id_limit(-1);
    reg.register_id("TEST_UNLIM::0001", fly::LogLevel::INFO);
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(reg.try_consume("TEST_UNLIM::0001"));
    }
    auto snapshot = reg.id_counts_snapshot();
    EXPECT_EQ(snapshot["TEST_UNLIM::0001"], 100u);
}

// id 配额：0 = 完全禁止（第一次就丢弃，但仍计数 1）。
TEST_F(MessageRegistryTest, IdLimitZero) {
    reg.set_id_limit(0);
    reg.register_id("TEST_ZERO::0001", fly::LogLevel::INFO);
    EXPECT_FALSE(reg.try_consume("TEST_ZERO::0001"));
    auto snapshot = reg.id_counts_snapshot();
    EXPECT_EQ(snapshot["TEST_ZERO::0001"], 1u);
}

// id 配额：显式设置 N。
TEST_F(MessageRegistryTest, IdLimitCustom) {
    reg.set_id_limit(5);
    reg.register_id("TEST_CUSTOM::0001", fly::LogLevel::INFO);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(reg.try_consume("TEST_CUSTOM::0001"));
    }
    EXPECT_FALSE(reg.try_consume("TEST_CUSTOM::0001"));
    auto snapshot = reg.id_counts_snapshot();
    EXPECT_EQ(snapshot["TEST_CUSTOM::0001"], 6u);
}

// domain 配额：默认 -1 不生效（同 domain 多个 id 各自独立，不受 domain 配额约束）。
TEST_F(MessageRegistryTest, DomainLimitDefaultUnlimited) {
    // 不设 domain limit，domain 配额默认 -1，不生效。
    reg.register_id("TEST_DOMDEF::0001", fly::LogLevel::INFO);
    reg.register_id("TEST_DOMDEF::0002", fly::LogLevel::INFO);
    EXPECT_TRUE(reg.try_consume("TEST_DOMDEF::0001"));
    EXPECT_TRUE(reg.try_consume("TEST_DOMDEF::0002"));
    // domain 计数累加到 2
    auto dom = reg.domain_counts_snapshot();
    EXPECT_EQ(dom["TEST_DOMDEF"], 2u);
}

// domain 配额：显式设置后，该 domain 下所有 id 共享。
// 场景：domain 配额 3，发 id1 2次 + id2 2次，共 4 次，第 4 次超限丢弃。
TEST_F(MessageRegistryTest, DomainLimitSharedAcrossIds) {
    reg.set_domain_limit("TEST_DOMSHARE", 3);
    reg.register_id("TEST_DOMSHARE::0001", fly::LogLevel::INFO);
    reg.register_id("TEST_DOMSHARE::0002", fly::LogLevel::INFO);

    EXPECT_TRUE(reg.try_consume("TEST_DOMSHARE::0001"));   // domain count=1
    EXPECT_TRUE(reg.try_consume("TEST_DOMSHARE::0001"));   // domain count=2
    EXPECT_TRUE(reg.try_consume("TEST_DOMSHARE::0002"));   // domain count=3
    EXPECT_FALSE(reg.try_consume("TEST_DOMSHARE::0002"));  // domain count=4 → 超限

    auto dom = reg.domain_counts_snapshot();
    EXPECT_EQ(dom["TEST_DOMSHARE"], 4u);  // 触发 4 次（含超限丢弃的）
}

// 两层配额同时检查：domain 配额先超限，id 配额未超限 → 仍丢弃。
TEST_F(MessageRegistryTest, DomainLimitOverrulesIdLimit) {
    reg.set_id_limit(100);  // id 配额很宽松
    reg.set_domain_limit("TEST_DOMOVER", 2);
    reg.register_id("TEST_DOMOVER::0001", fly::LogLevel::INFO);
    reg.register_id("TEST_DOMOVER::0002", fly::LogLevel::INFO);

    EXPECT_TRUE(reg.try_consume("TEST_DOMOVER::0001"));   // ok
    EXPECT_TRUE(reg.try_consume("TEST_DOMOVER::0002"));   // ok (domain=2)
    EXPECT_FALSE(reg.try_consume("TEST_DOMOVER::0001"));  // domain 超限 → 丢弃（id 配额没用满）

    auto id = reg.id_counts_snapshot();
    EXPECT_EQ(id["TEST_DOMOVER::0001"], 2u);
    auto dom = reg.domain_counts_snapshot();
    EXPECT_EQ(dom["TEST_DOMOVER"], 3u);
}

// extract_domain 工具函数。
TEST_F(MessageRegistryTest, ExtractDomain) {
    EXPECT_EQ(MessageRegistry::extract_domain("SOLVER::0047"), "SOLVER");
    EXPECT_EQ(MessageRegistry::extract_domain("SYS::0001"), "SYS");
    EXPECT_EQ(MessageRegistry::extract_domain("NOSEPARATOR"), "NOSEPARATOR");  // 容错
}

}  // namespace fly
