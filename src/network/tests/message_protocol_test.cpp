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
    msg.hostname = "gpu-node-1";
    msg.ip_address = "10.0.1.5";
    msg.attributes = {"has_gpu", "large_memory"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    RegisterMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id, 42);
    EXPECT_EQ(decoded.hostname, "gpu-node-1");
    EXPECT_EQ(decoded.ip_address, "10.0.1.5");
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
    uint32_t payload_size = MessageProtocol::get_payload_size(encoded);
    
    EXPECT_EQ(payload_size, encoded.size() - 5);
}

TEST(MessageProtocolTest, GetTypeFromHeader) {
    HeartbeatMessage msg;
    msg.worker_id = 100;
    
    CMString encoded = MessageProtocol::encode(msg);
    MessageType type = MessageProtocol::get_type(encoded);
    
    EXPECT_EQ(type, MessageType::HEARTBEAT);
    
    RegisterMessage reg_msg;
    reg_msg.worker_id = 1;
    
    CMString encoded_reg = MessageProtocol::encode(reg_msg);
    MessageType reg_type = MessageProtocol::get_type(encoded_reg);
    
    EXPECT_EQ(reg_type, MessageType::REGISTER);
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

TEST(MessageProtocolTest, RegisterAckMessage) {
    RegisterAckMessage msg;
    msg.header.type = MessageType::REGISTER_ACK;
    msg.header.message_id = 2;
    msg.worker_id = 42;
    msg.master_address = "192.168.1.1";
    msg.master_port = 8000;
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    RegisterAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.header.type, MessageType::REGISTER_ACK);
    EXPECT_EQ(decoded.worker_id, 42u);
    EXPECT_EQ(decoded.master_address, "192.168.1.1");
    EXPECT_EQ(decoded.master_port, 8000);
}

TEST(MessageProtocolTest, TaskAssignMessage) {
    TaskAssignMessage msg;
    msg.header.type = MessageType::TASK_ASSIGN;
    msg.header.message_id = 10;
    msg.task_id = 100;
    msg.task_name = "process_data";
    msg.task_module = "tasks.etl";
    msg.args = {"arg1", "arg2"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskAssignMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id, 100u);
    EXPECT_EQ(decoded.task_name, "process_data");
    EXPECT_EQ(decoded.task_module, "tasks.etl");
    EXPECT_EQ(decoded.args.size(), 2u);
}

TEST(MessageProtocolTest, TaskCompleteMessage) {
    TaskCompleteMessage msg;
    msg.header.type = MessageType::TASK_COMPLETE;
    msg.task_id = 50;
    msg.worker_id = 1;
    msg.written_objects = {"obj1", "obj2"};
    msg.frozen_dbs = {"db_a"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskCompleteMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id, 50u);
    EXPECT_EQ(decoded.worker_id, 1u);
    EXPECT_EQ(decoded.written_objects.size(), 2u);
    EXPECT_EQ(decoded.frozen_dbs.size(), 1u);
}

TEST(MessageProtocolTest, TaskFailedMessage) {
    TaskFailedMessage msg;
    msg.header.type = MessageType::TASK_FAILED;
    msg.task_id = 99;
    msg.worker_id = 3;
    msg.recoverable = true;
    msg.error_message = "connection lost";
    msg.error_type = TaskErrorType::EXECUTION_ERROR;
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskFailedMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id, 99u);
    EXPECT_TRUE(decoded.recoverable);
    EXPECT_EQ(decoded.error_message, "connection lost");
}

TEST(MessageProtocolTest, DataReadyMessage) {
    DataReadyMessage msg;
    msg.header.type = MessageType::DATA_READY;
    msg.worker_id = 5;
    msg.object_name = "db_abc:result/output";
    msg.db_id = "db_abc";
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    DataReadyMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id, 5u);
    EXPECT_EQ(decoded.object_name, "db_abc:result/output");
    EXPECT_EQ(decoded.db_id, "db_abc");
}

TEST(MessageProtocolTest, DataLocationMessage) {
    DataLocationMessage msg;
    msg.header.type = MessageType::DATA_LOCATION;
    msg.worker_id = 7;
    msg.file_path = "/data/worker7/output.bin";
    msg.object_name = "task_result";
    msg.data_host = "10.0.0.7";
    msg.data_port = 9001;
    msg.success = true;
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    DataLocationMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id, 7u);
    EXPECT_EQ(decoded.file_path, "/data/worker7/output.bin");
    EXPECT_EQ(decoded.data_host, "10.0.0.7");
    EXPECT_EQ(decoded.data_port, 9001);
    EXPECT_TRUE(decoded.success);
}

TEST(MessageProtocolTest, DataQueryMessage) {
    DataQueryMessage msg;
    msg.header.type = MessageType::DATA_QUERY;
    msg.header.message_id = 55;
    msg.object_name = "db_xyz:intermediate";
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    DataQueryMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.object_name, "db_xyz:intermediate");
}

TEST(MessageProtocolTest, ShutdownMessage) {
    ShutdownMessage msg;
    msg.header.type = MessageType::SHUTDOWN;
    msg.header.message_id = 999;
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    ShutdownMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.header.type, MessageType::SHUTDOWN);
    EXPECT_EQ(decoded.header.message_id, 999u);
}

TEST(MessageProtocolTest, TaskSubmitMessage) {
    TaskSubmitMessage msg;
    msg.header.type = MessageType::TASK_SUBMIT;
    msg.task_name = "my_task";
    msg.task_module = "my_module";
    msg.args = {"a1", "a2"};
    msg.inputs = {"input1", "input2"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskSubmitMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_name, "my_task");
    EXPECT_EQ(decoded.task_module, "my_module");
    EXPECT_EQ(decoded.args.size(), 2u);
    EXPECT_EQ(decoded.inputs.size(), 2u);
}

TEST(MessageProtocolTest, DbPathRequestResponseMessages) {
    DbPathRequestMessage req;
    req.header.type = MessageType::DB_PATH_REQUEST;
    req.db_id = "my_database";
    
    CMString encoded_req = MessageProtocol::encode(req);
    CMString buffer = encoded_req;
    
    DbPathRequestMessage decoded_req;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded_req));
    EXPECT_EQ(decoded_req.db_id, "my_database");
    
    DbPathResponseMessage resp;
    resp.header.type = MessageType::DB_PATH_RESPONSE;
    resp.db_id = "my_database";
    resp.base_path = "/data/base";
    resp.data_path = "/data/base/data";
    resp.success = true;
    
    CMString encoded_resp = MessageProtocol::encode(resp);
    CMString buffer_resp = encoded_resp;
    
    DbPathResponseMessage decoded_resp;
    EXPECT_TRUE(MessageProtocol::decode(buffer_resp, decoded_resp));
    EXPECT_EQ(decoded_resp.db_id, "my_database");
    EXPECT_EQ(decoded_resp.base_path, "/data/base");
    EXPECT_EQ(decoded_resp.data_path, "/data/base/data");
    EXPECT_TRUE(decoded_resp.success);
}

TEST(MessageProtocolTest, WriteRegisterAndAckMessages) {
    WriteRegisterMessage wr;
    wr.header.type = MessageType::WRITE_REGISTER;
    wr.worker_id = 10;
    wr.object_name = "obj_key";
    wr.db_id = "db1";
    
    CMString encoded_wr = MessageProtocol::encode(wr);
    CMString buffer_wr = encoded_wr;
    
    WriteRegisterMessage decoded_wr;
    EXPECT_TRUE(MessageProtocol::decode(buffer_wr, decoded_wr));
    EXPECT_EQ(decoded_wr.worker_id, 10u);
    EXPECT_EQ(decoded_wr.object_name, "obj_key");
    EXPECT_EQ(decoded_wr.db_id, "db1");
    
    WriteRegisterAckMessage ack;
    ack.header.type = MessageType::WRITE_REGISTER_ACK;
    ack.object_name = "obj_key";
    ack.db_id = "db1";
    ack.success = true;
    ack.error_message = "";
    
    CMString encoded_ack = MessageProtocol::encode(ack);
    CMString buffer_ack = encoded_ack;
    
    WriteRegisterAckMessage decoded_ack;
    EXPECT_TRUE(MessageProtocol::decode(buffer_ack, decoded_ack));
    EXPECT_TRUE(decoded_ack.success);
    EXPECT_EQ(decoded_ack.object_name, "obj_key");
    EXPECT_EQ(decoded_ack.db_id, "db1");
}

TEST(MessageProtocolTest, WriteRegisterAckFailure) {
    WriteRegisterAckMessage ack;
    ack.header.type = MessageType::WRITE_REGISTER_ACK;
    ack.object_name = "bad_obj";
    ack.db_id = "bad_db";
    ack.success = false;
    ack.error_message = "database is frozen";
    ack.error_type = TaskErrorType::WRITE_TO_FROZEN_DB;
    
    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;
    
    WriteRegisterAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_FALSE(decoded.success);
    EXPECT_EQ(decoded.error_message, "database is frozen");
}

}  // namespace fly