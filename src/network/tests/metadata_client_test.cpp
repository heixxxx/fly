#include <gtest/gtest.h>
#include <network/cpp/metadata_client.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <thread>
#include <atomic>
#include <functional>
#include <cstring>
#include <poll.h>
#include <unistd.h>

namespace fly {

// ---- e2e test helpers: lightweight mock master server ----
//
// MetadataClient 走同步 TCP（不经 reactor），帧格式 [4B BE total_len][1B type][payload]。
// MockServer 在后台线程 accept 一个连接，读 DataQueryMessage，用用户提供的 handler
// 构造 DataLocationMessage 回复。handler 可控制 success_/locations_/can_still_produce_，
// 覆盖成功、多副本、对象不存在等路径。
//
// 模板抄自 data_transfer_test.cpp 的 frame 读写骨架，BUILD 无需新增依赖。
class MockMetadataServer {
public:
    using ResponseBuilder = std::function<DataLocationMessage(const CMString& object_name)>;

    explicit MockMetadataServer(ResponseBuilder builder)
        : builder_(std::move(builder))
        , transport_(create_tcp_transport()) {
        listen_fd_ = transport_->create_listen_socket("127.0.0.1", 0);
        // 构造函数里不能用 ASSERT（会生成 return），用 EXPECT + 标记失败。
        // serve() 会检查 listen_fd_ < 0 直接返回。
        EXPECT_GE(listen_fd_, 0);
        port_ = (listen_fd_ >= 0) ? transport_->get_port(listen_fd_) : 0;
        // accept 会阻塞直到 client 连接，放后台线程。
        server_thread_ = std::thread([this] { serve(); });
    }

    ~MockMetadataServer() {
        transport_->close(listen_fd_);
        if (server_thread_.joinable()) server_thread_.join();
    }

    int port() const { return port_; }

private:
    void serve() {
        // listen socket 是 SOCK_NONBLOCK 的（tcp_socket.cpp:15），accept4 在无连接时
        // 立即返回 EAGAIN。先 poll 等 client 连接到达再 accept，避免时序竞态。
        if (listen_fd_ < 0) return;
        struct pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pret = ::poll(&pfd, 1, 10000);  // 10s 等 client 连接
        if (pret <= 0 || !(pfd.revents & POLLIN)) return;

        int fd = transport_->accept_connection(listen_fd_);
        if (fd < 0) return;
        transport_->set_recv_timeout(fd, 5000);
        transport_->set_send_timeout(fd, 5000);

        // Read 5B frame header.
        char header[5];
        if (!recv_exact(transport_.get(), fd, header, 5)) {
            transport_->close(fd);
            return;
        }
        uint32_t total_len = read_be32(header);
        if (total_len < 1) {
            transport_->close(fd);
            return;
        }

        // Read payload, reassemble frame, decode DataQueryMessage.
        uint32_t payload_len = total_len - 1;
        CMString frame;
        frame.resize(4 + total_len);
        std::memcpy(&frame[0], header, 5);
        if (payload_len > 0) {
            if (!recv_exact(transport_.get(), fd, frame.data() + 5, payload_len)) {
                transport_->close(fd);
                return;
            }
        }
        DataQueryMessage query;
        if (MessageProtocol::decode(frame, query)) {
            last_query_object_ = query.object_name_;
        }

        // Build + send response.
        DataLocationMessage resp = builder_(query.object_name_);
        resp.header_.type_ = MessageType::DATA_LOCATION;
        CMString encoded = MessageProtocol::encode(resp);
        transport_->send_all(fd, encoded.data(), encoded.size());

        transport_->close(fd);
    }

public:
    CMString last_query_object_;  // server 收到的 object_name（往返一致性校验用）

private:
    ResponseBuilder builder_;
    CMSharedPtr<Transport> transport_;
    int listen_fd_ = -1;
    int port_ = 0;
    std::thread server_thread_;
};

TEST(MetadataClientTest, DataLocationDefaults) {
    MetadataClient::DataLocation loc;
    EXPECT_FALSE(loc.found_);
    EXPECT_EQ(loc.worker_id_, 0u);
    EXPECT_EQ(loc.host_, "");
    EXPECT_EQ(loc.port_, 0);
    EXPECT_EQ(loc.error_, "");
}

TEST(MetadataClientTest, QueryFailsWhenNoServer) {
    MetadataClient client;
    MetadataClient::DataLocation result =
        client.query_data_location("127.0.0.1", 59999, "test/object");

    EXPECT_FALSE(result.found_);
    EXPECT_FALSE(result.error_.empty());
}

TEST(MetadataClientTest, QueryFailsWithInvalidHost) {
    MetadataClient client;
    MetadataClient::DataLocation result =
        client.query_data_location("0.0.0.0", 59999, "test/object");

    EXPECT_FALSE(result.found_);
    EXPECT_FALSE(result.error_.empty());
}

TEST(MetadataClientTest, DataLocationMessageEncodeDecode) {
    DataLocationMessage msg;
    msg.header_.type_ = MessageType::DATA_LOCATION;
    msg.header_.message_id_ = 42;
    msg.header_.timestamp_ = 1234567890;
    msg.file_path_ = "/data/worker100/db/object.bin";
    msg.object_name_ = "test/object";
    DataLocation dl1;
    dl1.object_name = "test/object";
    dl1.worker_id = 100;
    dl1.host = "192.168.1.5";
    dl1.port = 9001;
    DataLocation dl2;
    dl2.object_name = "test/object";
    dl2.worker_id = 200;
    dl2.host = "192.168.1.6";
    dl2.port = 9002;
    msg.locations_.push_back(dl1);
    msg.locations_.push_back(dl2);
    msg.success_ = true;

    CMString encoded = MessageProtocol::encode(msg);
    EXPECT_GT(encoded.size(), 5u);

    CMString buffer = encoded;
    DataLocationMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));

    EXPECT_EQ(decoded.header_.message_id_, 42u);
    EXPECT_EQ(decoded.file_path_, "/data/worker100/db/object.bin");
    EXPECT_EQ(decoded.object_name_, "test/object");
    ASSERT_EQ(decoded.locations_.size(), 2u);
    EXPECT_EQ(decoded.locations_[0].worker_id, 100u);
    EXPECT_EQ(decoded.locations_[0].host, "192.168.1.5");
    EXPECT_EQ(decoded.locations_[0].port, 9001);
    EXPECT_EQ(decoded.locations_[1].worker_id, 200u);
    EXPECT_EQ(decoded.locations_[1].host, "192.168.1.6");
    EXPECT_EQ(decoded.locations_[1].port, 9002);
    EXPECT_TRUE(decoded.success_);
}

TEST(MetadataClientTest, DataLocationMessageFailure) {
    DataLocationMessage msg;
    msg.header_.type_ = MessageType::DATA_LOCATION;
    msg.object_name_ = "nonexistent/object";
    msg.success_ = false;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    DataLocationMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_FALSE(decoded.success_);
    EXPECT_EQ(decoded.object_name_, "nonexistent/object");
}

TEST(MetadataClientTest, DataQueryMessageEncodeDecode) {
    DataQueryMessage req;
    req.header_.type_ = MessageType::DATA_QUERY;
    req.header_.message_id_ = 55;
    req.object_name_ = "db_path:some/key";

    CMString encoded = MessageProtocol::encode(req);
    CMString buffer = encoded;

    DataQueryMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.object_name_, "db_path:some/key");
}

// ---- e2e success-path tests (P3-19: was only failure + encoding tested) ----

// 多副本成功路径：server 回 success_=true + 2 个副本，验证 all_locations_ 填充
// 和便捷字段（worker_id_/host_/port_）镜像第一个副本。覆盖 metadata_client.cpp:96-109。
TEST(MetadataClientTest, E2EQuerySuccessMultiReplica) {
    MockMetadataServer server([](const CMString& obj) {
        DataLocationMessage msg;
        msg.object_name_ = obj;
        msg.success_ = true;
        DataLocation dl1;
        dl1.object_name = obj;
        dl1.worker_id = 101;
        dl1.host = "192.168.1.10";
        dl1.port = 9001;
        DataLocation dl2;
        dl2.object_name = obj;
        dl2.worker_id = 202;
        dl2.host = "192.168.1.20";
        dl2.port = 9002;
        msg.locations_.push_back(dl1);
        msg.locations_.push_back(dl2);
        return msg;
    });

    MetadataClient client;
    CMString obj = "test_db:obj_multi";
    MetadataClient::DataLocation result =
        client.query_data_location("127.0.0.1", server.port(), obj);

    EXPECT_TRUE(result.found_);
    EXPECT_TRUE(result.error_.empty());
    // all_locations_ 应包含全部 2 个副本
    ASSERT_EQ(result.all_locations_.size(), 2u);
    EXPECT_EQ(result.all_locations_[0].worker_id_, 101u);
    EXPECT_EQ(result.all_locations_[0].host_, "192.168.1.10");
    EXPECT_EQ(result.all_locations_[0].port_, 9001);
    EXPECT_EQ(result.all_locations_[1].worker_id_, 202u);
    EXPECT_EQ(result.all_locations_[1].host_, "192.168.1.20");
    EXPECT_EQ(result.all_locations_[1].port_, 9002);
    // 便捷字段应镜像第一个副本
    EXPECT_EQ(result.worker_id_, 101u);
    EXPECT_EQ(result.host_, "192.168.1.10");
    EXPECT_EQ(result.port_, 9001);
    // 往返一致性：server 收到的 object_name 应与 client 发送的一致
    EXPECT_EQ(server.last_query_object_, obj);
}

// server 主动回 success_=false（对象不存在）：覆盖 metadata_client.cpp:93-94。
// 这是现有失败测试（连不上 server）未覆盖的分支 —— "连上了但 server 说找不到"。
TEST(MetadataClientTest, E2EQueryObjectNotFound) {
    MockMetadataServer server([](const CMString& obj) {
        DataLocationMessage msg;
        msg.object_name_ = obj;
        msg.success_ = false;
        msg.can_still_produce_ = false;  // 没有 pending task 可能产出
        return msg;
    });

    MetadataClient client;
    MetadataClient::DataLocation result =
        client.query_data_location("127.0.0.1", server.port(), "test_db:nonexistent");

    EXPECT_FALSE(result.found_);
    EXPECT_FALSE(result.error_.empty());  // "Master has no location for ..."
    EXPECT_FALSE(result.can_still_produce_);
    EXPECT_TRUE(result.all_locations_.empty());
}

// can_still_produce_ 透传（成功 + 未决路径）：成功时为 false；
// 对象不存在但有 pending task 时 can_still_produce_=true。
TEST(MetadataClientTest, E2ECanStillProducePassthrough) {
    // 成功路径：can_still_produce_ = false（已找到，无需再等）
    MockMetadataServer server_found([](const CMString& obj) {
        DataLocationMessage msg;
        msg.object_name_ = obj;
        msg.success_ = true;
        msg.can_still_produce_ = false;
        DataLocation dl;
        dl.object_name = obj;
        dl.worker_id = 1;
        dl.host = "10.0.0.1";
        dl.port = 5000;
        msg.locations_.push_back(dl);
        return msg;
    });
    MetadataClient client;
    auto r1 = client.query_data_location("127.0.0.1", server_found.port(), "db:found");
    EXPECT_TRUE(r1.found_);
    EXPECT_FALSE(r1.can_still_produce_);

    // 未决路径：未找到但 can_still_produce_ = true（有 task 正在产出）
    MockMetadataServer server_pending([](const CMString& obj) {
        DataLocationMessage msg;
        msg.object_name_ = obj;
        msg.success_ = false;
        msg.can_still_produce_ = true;
        return msg;
    });
    auto r2 = client.query_data_location("127.0.0.1", server_pending.port(), "db:pending");
    EXPECT_FALSE(r2.found_);
    EXPECT_TRUE(r2.can_still_produce_);
}

}  // namespace fly