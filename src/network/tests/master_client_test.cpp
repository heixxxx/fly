#include <gtest/gtest.h>
#include <network/cpp/master_client.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>

namespace fly {

TEST(MasterClientTest, DataLocationDefaults) {
    MasterClient::DataLocation loc;
    EXPECT_FALSE(loc.found);
    EXPECT_EQ(loc.worker_id, 0u);
    EXPECT_EQ(loc.host, "");
    EXPECT_EQ(loc.port, 0);
    EXPECT_EQ(loc.error, "");
}

TEST(MasterClientTest, QueryFailsWhenNoServer) {
    MasterClient::DataLocation result =
        MasterClient::query_data_location("127.0.0.1", 59999, "test/object");

    EXPECT_FALSE(result.found);
    EXPECT_FALSE(result.error.empty());
}

TEST(MasterClientTest, QueryFailsWithInvalidHost) {
    MasterClient::DataLocation result =
        MasterClient::query_data_location("0.0.0.0", 59999, "test/object");

    EXPECT_FALSE(result.found);
    EXPECT_FALSE(result.error.empty());
}

TEST(MasterClientTest, DataLocationMessageEncodeDecode) {
    DataLocationMessage msg;
    msg.header.type = MessageType::DATA_LOCATION;
    msg.header.message_id = 42;
    msg.header.timestamp = 1234567890;
    msg.worker_id = 100;
    msg.file_path = "/data/worker100/db/object.bin";
    msg.object_name = "test/object";
    msg.data_host = "192.168.1.5";
    msg.data_port = 9001;
    msg.success = true;

    CMString encoded = MessageProtocol::encode(msg);
    EXPECT_GT(encoded.size(), 5u);

    CMString buffer = encoded;
    DataLocationMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));

    EXPECT_EQ(decoded.header.message_id, 42u);
    EXPECT_EQ(decoded.worker_id, 100u);
    EXPECT_EQ(decoded.file_path, "/data/worker100/db/object.bin");
    EXPECT_EQ(decoded.object_name, "test/object");
    EXPECT_EQ(decoded.data_host, "192.168.1.5");
    EXPECT_EQ(decoded.data_port, 9001);
    EXPECT_TRUE(decoded.success);
}

TEST(MasterClientTest, DataLocationMessageFailure) {
    DataLocationMessage msg;
    msg.header.type = MessageType::DATA_LOCATION;
    msg.object_name = "nonexistent/object";
    msg.success = false;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    DataLocationMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_FALSE(decoded.success);
    EXPECT_EQ(decoded.object_name, "nonexistent/object");
}

TEST(MasterClientTest, DataQueryMessageEncodeDecode) {
    DataQueryMessage req;
    req.header.type = MessageType::DATA_QUERY;
    req.header.message_id = 55;
    req.object_name = "db_id:some/key";

    CMString encoded = MessageProtocol::encode(req);
    CMString buffer = encoded;

    DataQueryMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.object_name, "db_id:some/key");
}

}  // namespace fly