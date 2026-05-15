#include <gtest/gtest.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>

namespace fly {

TEST(MessageProtocolTest, EncodeDecodeHeartbeat) {
    HeartbeatMessage msg;
    msg.header.type = MessageType::HEARTBEAT;
    msg.header.message_id = 1;
    msg.header.timestamp = 1234567890;
    msg.worker_id = 100;
    msg.running_tasks = {1, 2, 3};
    msg.attributes = {"gpu", "ssd"};
    
    CMString encoded = MessageProtocol::encode(msg);
    EXPECT_GT(encoded.size(), 4);
    
    CMString buffer = encoded;
    HeartbeatMessage decoded;
    
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.header.type, MessageType::HEARTBEAT);
    EXPECT_EQ(decoded.header.message_id, 1);
    EXPECT_EQ(decoded.worker_id, 100);
    EXPECT_EQ(decoded.running_tasks.size(), 3);
    EXPECT_EQ(decoded.attributes.size(), 2);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, PartialBufferReturnsFalse) {
    HeartbeatMessage msg;
    msg.worker_id = 123;
    
    CMString encoded = MessageProtocol::encode(msg);
    
    CMString partial(encoded.substr(0, 2));
    
    HeartbeatMessage decoded;
    EXPECT_FALSE(MessageProtocol::decode(partial, decoded));
    EXPECT_EQ(partial.size(), 2);
}

TEST(MessageProtocolTest, MultipleMessagesInBuffer) {
    HeartbeatMessage msg1, msg2;
    msg1.worker_id = 1;
    msg2.worker_id = 2;
    
    CMString encoded1 = MessageProtocol::encode(msg1);
    CMString encoded2 = MessageProtocol::encode(msg2);
    CMString buffer = encoded1 + encoded2;
    
    HeartbeatMessage decoded1;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded1));
    EXPECT_EQ(decoded1.worker_id, 1);
    
    HeartbeatMessage decoded2;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded2));
    EXPECT_EQ(decoded2.worker_id, 2);
    
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, RegisterMessage) {
    RegisterMessage msg;
    msg.header.type = MessageType::REGISTER;
    msg.worker_id = 42;
    msg.role = "hybrid";
    msg.attributes = {"has_gpu", "large_memory"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    RegisterMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id, 42);
    EXPECT_EQ(decoded.role, "hybrid");
    EXPECT_EQ(decoded.attributes.size(), 2);
}

TEST(MessageProtocolTest, DataRequestResponse) {
    DataRequestMessage req;
    req.header.type = MessageType::DATA_REQUEST;
    req.object_name = "test/object";
    req.requesting_worker_id = 10;
    
    CMString encoded_req = MessageProtocol::encode(req);
    CMString buffer_req = encoded_req;
    
    DataRequestMessage decoded_req;
    EXPECT_TRUE(MessageProtocol::decode(buffer_req, decoded_req));
    EXPECT_EQ(decoded_req.object_name, "test/object");
    EXPECT_EQ(decoded_req.requesting_worker_id, 10);
    
    DataResponseMessage resp;
    resp.header.type = MessageType::DATA_RESPONSE;
    resp.object_name = "test/object";
    resp.data = "binary_payload_data";
    
    CMString encoded_resp = MessageProtocol::encode(resp);
    CMString buffer_resp = encoded_resp;
    
    DataResponseMessage decoded_resp;
    EXPECT_TRUE(MessageProtocol::decode(buffer_resp, decoded_resp));
    EXPECT_EQ(decoded_resp.object_name, "test/object");
    EXPECT_EQ(decoded_resp.data, "binary_payload_data");
}

TEST(MessageProtocolTest, GetPayloadSize) {
    HeartbeatMessage msg;
    msg.worker_id = 999;
    
    CMString encoded = MessageProtocol::encode(msg);
    uint32_t size = MessageProtocol::get_payload_size(encoded);
    
    EXPECT_EQ(size, encoded.size() - 4);
}

TEST(MessageProtocolTest, EmptyAttributes) {
    HeartbeatMessage msg;
    msg.worker_id = 1;
    msg.running_tasks.clear();
    msg.attributes.clear();
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    HeartbeatMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id, 1);
    EXPECT_TRUE(decoded.running_tasks.empty());
    EXPECT_TRUE(decoded.attributes.empty());
}

}  // namespace fly