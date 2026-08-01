#include <gtest/gtest.h>
#include <network/cpp/metadata_client.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>

namespace fly {

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

}  // namespace fly