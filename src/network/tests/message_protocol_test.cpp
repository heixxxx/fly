#include <gtest/gtest.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>

namespace fly {

TEST(MessageProtocolTest, EncodeDecodeHeartbeat) {
    HeartbeatMessage msg;
    msg.header_.type_ = MessageType::HEARTBEAT;
    msg.header_.message_id_ = 1;
    msg.header_.timestamp_ = 1234567890;
    msg.worker_id_ = 100;
    msg.running_tasks_ = {1, 2, 3};
    msg.attributes_ = {"gpu", "ssd"};
    
    CMString encoded = MessageProtocol::encode(msg);
    EXPECT_GT(encoded.size(), 4);
    
    CMString buffer = encoded;
    HeartbeatMessage decoded;
    
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.header_.type_, MessageType::HEARTBEAT);
    EXPECT_EQ(decoded.header_.message_id_, 1);
    EXPECT_EQ(decoded.worker_id_, 100);
    EXPECT_EQ(decoded.running_tasks_.size(), 3);
    EXPECT_EQ(decoded.attributes_.size(), 2);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, PartialBufferReturnsFalse) {
    HeartbeatMessage msg;
    msg.worker_id_ = 123;
    
    CMString encoded = MessageProtocol::encode(msg);
    
    CMString partial(encoded.substr(0, 2));
    
    HeartbeatMessage decoded;
    EXPECT_FALSE(MessageProtocol::decode(partial, decoded));
    EXPECT_EQ(partial.size(), 2);
}

TEST(MessageProtocolTest, MultipleMessagesInBuffer) {
    HeartbeatMessage msg1, msg2;
    msg1.worker_id_ = 1;
    msg2.worker_id_ = 2;
    
    CMString encoded1 = MessageProtocol::encode(msg1);
    CMString encoded2 = MessageProtocol::encode(msg2);
    CMString buffer = encoded1 + encoded2;
    
    HeartbeatMessage decoded1;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded1));
    EXPECT_EQ(decoded1.worker_id_, 1);
    
    HeartbeatMessage decoded2;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded2));
    EXPECT_EQ(decoded2.worker_id_, 2);
    
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, RegisterMessage) {
    RegisterMessage msg;
    msg.header_.type_ = MessageType::REGISTER;
    msg.worker_id_ = 42;
    msg.hostname_ = "gpu-node-1";
    msg.ip_address_ = "10.0.1.5";
    msg.attributes_ = {"has_gpu", "large_memory"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    RegisterMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 42);
    EXPECT_EQ(decoded.hostname_, "gpu-node-1");
    EXPECT_EQ(decoded.ip_address_, "10.0.1.5");
    EXPECT_EQ(decoded.attributes_.size(), 2);
}

TEST(MessageProtocolTest, DataRequestResponse) {
    DataRequestMessage req;
    req.header_.type_ = MessageType::DATA_REQUEST;
    req.object_name_ = "test/object";
    req.requesting_worker_id_ = 10;
    
    CMString encoded_req = MessageProtocol::encode(req);
    CMString buffer_req = encoded_req;
    
    DataRequestMessage decoded_req;
    EXPECT_TRUE(MessageProtocol::decode(buffer_req, decoded_req));
    EXPECT_EQ(decoded_req.object_name_, "test/object");
    EXPECT_EQ(decoded_req.requesting_worker_id_, 10);
    
    DataResponseMessage resp;
    resp.header_.type_ = MessageType::DATA_RESPONSE;
    resp.object_name_ = "test/object";
    
    CMString encoded_resp = MessageProtocol::encode(resp);
    CMString buffer_resp = encoded_resp;
    
    DataResponseMessage decoded_resp;
    EXPECT_TRUE(MessageProtocol::decode(buffer_resp, decoded_resp));
    EXPECT_EQ(decoded_resp.object_name_, "test/object");
}

TEST(MessageProtocolTest, GetPayloadSize) {
    HeartbeatMessage msg;
    msg.worker_id_ = 999;
    
    CMString encoded = MessageProtocol::encode(msg);
    uint32_t payload_size = MessageProtocol::get_payload_size(encoded);
    
    EXPECT_EQ(payload_size, encoded.size() - 5);
}

TEST(MessageProtocolTest, GetTypeFromHeader) {
    HeartbeatMessage msg;
    msg.worker_id_ = 100;
    
    CMString encoded = MessageProtocol::encode(msg);
    MessageType type = MessageProtocol::get_type(encoded);
    
    EXPECT_EQ(type, MessageType::HEARTBEAT);
    
    RegisterMessage reg_msg;
    reg_msg.worker_id_ = 1;
    
    CMString encoded_reg = MessageProtocol::encode(reg_msg);
    MessageType reg_type = MessageProtocol::get_type(encoded_reg);
    
    EXPECT_EQ(reg_type, MessageType::REGISTER);
}

TEST(MessageProtocolTest, EmptyAttributes) {
    HeartbeatMessage msg;
    msg.worker_id_ = 1;
    msg.running_tasks_.clear();
    msg.attributes_.clear();
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    HeartbeatMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 1);
    EXPECT_TRUE(decoded.running_tasks_.empty());
    EXPECT_TRUE(decoded.attributes_.empty());
}

TEST(MessageProtocolTest, RegisterAckMessage) {
    RegisterAckMessage msg;
    msg.header_.type_ = MessageType::REGISTER_ACK;
    msg.header_.message_id_ = 2;
    msg.worker_id_ = 42;
    msg.master_address_ = "192.168.1.1";
    msg.master_port_ = 8000;
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    RegisterAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.header_.type_, MessageType::REGISTER_ACK);
    EXPECT_EQ(decoded.worker_id_, 42u);
    EXPECT_EQ(decoded.master_address_, "192.168.1.1");
    EXPECT_EQ(decoded.master_port_, 8000);
}

TEST(MessageProtocolTest, TaskAssignMessage) {
    TaskAssignMessage msg;
    msg.header_.type_ = MessageType::TASK_ASSIGN;
    msg.header_.message_id_ = 10;
    msg.task_id_ = 100;
    msg.task_name_ = "process_data";
    msg.task_module_ = "tasks.etl";
    msg.args_ = {"arg1", "arg2"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskAssignMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id_, 100u);
    EXPECT_EQ(decoded.task_name_, "process_data");
    EXPECT_EQ(decoded.task_module_, "tasks.etl");
    EXPECT_EQ(decoded.args_.size(), 2u);
}

TEST(MessageProtocolTest, TaskCompleteMessage) {
    TaskCompleteMessage msg;
    msg.header_.type_ = MessageType::TASK_COMPLETE;
    msg.task_id_ = 50;
    msg.worker_id_ = 1;
    msg.written_objects_ = {WrittenObject{"obj1", 100}, WrittenObject{"obj2", 200}};
    msg.frozen_dbs_ = {"db_a"};
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskCompleteMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id_, 50u);
    EXPECT_EQ(decoded.worker_id_, 1u);
    EXPECT_EQ(decoded.written_objects_.size(), 2u);
    EXPECT_EQ(decoded.frozen_dbs_.size(), 1u);
}

TEST(MessageProtocolTest, TaskFailedMessage) {
    TaskFailedMessage msg;
    msg.header_.type_ = MessageType::TASK_FAILED;
    msg.task_id_ = 99;
    msg.worker_id_ = 3;
    msg.recoverable_ = true;
    msg.error_message_ = "connection lost";
    msg.error_type_ = TaskErrorType::EXECUTION_ERROR;
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskFailedMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id_, 99u);
    EXPECT_TRUE(decoded.recoverable_);
    EXPECT_EQ(decoded.error_message_, "connection lost");
}

TEST(MessageProtocolTest, WriteRegisterMessageWithSizeAndWriter) {
    WriteRegisterMessage msg;
    msg.header_.type_ = MessageType::WRITE_REGISTER;
    msg.worker_id_ = 5;
    msg.object_name_ = "db_abc:result/output";
    msg.db_path_ = "db_abc";
    msg.write_context_hash_ = "hash123";
    msg.writer_id_ = "w_abc";
    msg.size_bytes_ = 1234567;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    WriteRegisterMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 5u);
    EXPECT_EQ(decoded.object_name_, "db_abc:result/output");
    EXPECT_EQ(decoded.db_path_, "db_abc");
    EXPECT_EQ(decoded.write_context_hash_, "hash123");
    EXPECT_EQ(decoded.writer_id_, "w_abc");
    EXPECT_EQ(decoded.size_bytes_, 1234567);
}

TEST(MessageProtocolTest, TaskCompleteWrittenObjects) {
    TaskCompleteMessage msg;
    msg.header_.type_ = MessageType::TASK_COMPLETE;
    msg.task_id_ = 10;
    msg.worker_id_ = 3;
    msg.written_objects_.push_back(WrittenObject{"db1:obj_a", 4096});
    msg.written_objects_.push_back(WrittenObject{"db1:obj_b", 1048576});

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    TaskCompleteMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id_, 10u);
    EXPECT_EQ(decoded.worker_id_, 3u);
    ASSERT_EQ(decoded.written_objects_.size(), 2u);
    EXPECT_EQ(decoded.written_objects_[0].object_name_, "db1:obj_a");
    EXPECT_EQ(decoded.written_objects_[0].size_bytes_, 4096);
    EXPECT_EQ(decoded.written_objects_[1].object_name_, "db1:obj_b");
    EXPECT_EQ(decoded.written_objects_[1].size_bytes_, 1048576);
}

TEST(MessageProtocolTest, DataLocationMessage) {
    DataLocationMessage msg;
    msg.header_.type_ = MessageType::DATA_LOCATION;
    msg.file_path_ = "/data/worker7/output.bin";
    msg.object_name_ = "task_result";
    DataLocation dl;
    dl.object_name = "task_result";
    dl.worker_id = 7;
    dl.host = "10.0.0.7";
    dl.port = 9001;
    msg.locations_.push_back(dl);
    msg.success_ = true;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    DataLocationMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.file_path_, "/data/worker7/output.bin");
    ASSERT_EQ(decoded.locations_.size(), 1u);
    EXPECT_EQ(decoded.locations_[0].worker_id, 7u);
    EXPECT_EQ(decoded.locations_[0].host, "10.0.0.7");
    EXPECT_EQ(decoded.locations_[0].port, 9001);
    EXPECT_TRUE(decoded.success_);
}

TEST(MessageProtocolTest, DataQueryMessage) {
    DataQueryMessage msg;
    msg.header_.type_ = MessageType::DATA_QUERY;
    msg.header_.message_id_ = 55;
    msg.object_name_ = "db_xyz:intermediate";
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    DataQueryMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.object_name_, "db_xyz:intermediate");
}

TEST(MessageProtocolTest, ShutdownMessage) {
    ShutdownMessage msg;
    msg.header_.type_ = MessageType::SHUTDOWN;
    msg.header_.message_id_ = 999;
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    ShutdownMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.header_.type_, MessageType::SHUTDOWN);
    EXPECT_EQ(decoded.header_.message_id_, 999u);
}

TEST(MessageProtocolTest, TaskSubmitMessage) {
    TaskSubmitMessage msg;
    msg.header_.type_ = MessageType::TASK_SUBMIT;
    msg.task_name_ = "my_task";
    msg.task_module_ = "my_module";
    msg.args_ = {"a1", "a2"};
    msg.inputs_ = {"input1", "input2"};
    msg.priority_ = 20;  // 非 default（默认 10），验证 priority 跨进程序列化往返

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    TaskSubmitMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_name_, "my_task");
    EXPECT_EQ(decoded.task_module_, "my_module");
    EXPECT_EQ(decoded.args_.size(), 2u);
    EXPECT_EQ(decoded.inputs_.size(), 2u);
    EXPECT_EQ(decoded.priority_, 20);  // worker→master 递归提交场景 priority 透传
}

TEST(MessageProtocolTest, DbPathRequestResponseMessages) {
    DbPathRequestMessage req;
    req.header_.type_ = MessageType::DB_PATH_REQUEST;
    req.db_path_ = "my_database";

    CMString encoded_req = MessageProtocol::encode(req);
    CMString buffer = encoded_req;

    DbPathRequestMessage decoded_req;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded_req));
    EXPECT_EQ(decoded_req.db_path_, "my_database");

    DbPathResponseMessage resp;
    resp.header_.type_ = MessageType::DB_PATH_RESPONSE;
    resp.db_path_ = "my_database";
    resp.data_path_ = "/data/base/data";
    resp.success_ = true;

    CMString encoded_resp = MessageProtocol::encode(resp);
    CMString buffer_resp = encoded_resp;

    DbPathResponseMessage decoded_resp;
    EXPECT_TRUE(MessageProtocol::decode(buffer_resp, decoded_resp));
    EXPECT_EQ(decoded_resp.db_path_, "my_database");
    EXPECT_EQ(decoded_resp.data_path_, "/data/base/data");
    EXPECT_TRUE(decoded_resp.success_);
}

TEST(MessageProtocolTest, DatabaseFreezeNotificationRoundTrip) {
    DatabaseFreezeNotification msg;
    msg.header_.type_ = MessageType::DATABASE_FREEZE;
    msg.db_path_ = "frozen_db_001";
    msg.task_id_ = 42;   // 非 stream 模式 master 登记 pending frozen 需要 task_id

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    DatabaseFreezeNotification decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_path_, "frozen_db_001");
    EXPECT_EQ(decoded.task_id_, 42u);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, DatabaseFreezeAckRoundTripSuccess) {
    DatabaseFreezeAckMessage ack;
    ack.header_.type_ = MessageType::DATABASE_FREEZE_ACK;
    ack.db_path_ = "frozen_db_001";
    ack.success_ = true;

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    DatabaseFreezeAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_path_, "frozen_db_001");
    EXPECT_TRUE(decoded.success_);
    EXPECT_EQ(decoded.error_type_, TaskErrorType::UNKNOWN);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, DatabaseFreezeAckRoundTripConflict) {
    // 冲突场景：db 已被其他 task freeze，master 拒绝 → fail-fast
    DatabaseFreezeAckMessage ack;
    ack.header_.type_ = MessageType::DATABASE_FREEZE_ACK;
    ack.db_path_ = "already_frozen_db";
    ack.success_ = false;
    ack.error_type_ = TaskErrorType::DB_ALREADY_FROZEN;

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    DatabaseFreezeAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_path_, "already_frozen_db");
    EXPECT_FALSE(decoded.success_);
    EXPECT_EQ(decoded.error_type_, TaskErrorType::DB_ALREADY_FROZEN);
}

TEST(MessageProtocolTest, WriteRegisterAndAckMessages) {
    WriteRegisterMessage wr;
    wr.header_.type_ = MessageType::WRITE_REGISTER;
    wr.worker_id_ = 10;
    wr.object_name_ = "obj_key";
    wr.db_path_ = "db1";
    
    CMString encoded_wr = MessageProtocol::encode(wr);
    CMString buffer_wr = encoded_wr;
    
    WriteRegisterMessage decoded_wr;
    EXPECT_TRUE(MessageProtocol::decode(buffer_wr, decoded_wr));
    EXPECT_EQ(decoded_wr.worker_id_, 10u);
    EXPECT_EQ(decoded_wr.object_name_, "obj_key");
    EXPECT_EQ(decoded_wr.db_path_, "db1");
    
    WriteRegisterAckMessage ack;
    ack.header_.type_ = MessageType::WRITE_REGISTER_ACK;
    ack.object_name_ = "obj_key";
    ack.db_path_ = "db1";
    ack.success_ = true;
    ack.error_message_ = "";
    
    CMString encoded_ack = MessageProtocol::encode(ack);
    CMString buffer_ack = encoded_ack;
    
    WriteRegisterAckMessage decoded_ack;
    EXPECT_TRUE(MessageProtocol::decode(buffer_ack, decoded_ack));
    EXPECT_TRUE(decoded_ack.success_);
    EXPECT_EQ(decoded_ack.object_name_, "obj_key");
    EXPECT_EQ(decoded_ack.db_path_, "db1");
}

TEST(MessageProtocolTest, WriteRegisterAckFailure) {
    WriteRegisterAckMessage ack;
    ack.header_.type_ = MessageType::WRITE_REGISTER_ACK;
    ack.object_name_ = "bad_obj";
    ack.db_path_ = "bad_db";
    ack.success_ = false;
    ack.error_message_ = "database is frozen";
    ack.error_type_ = TaskErrorType::WRITE_TO_FROZEN_DB;
    
    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;
    
    WriteRegisterAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_FALSE(decoded.success_);
    EXPECT_EQ(decoded.error_message_, "database is frozen");
}

// =============================================================================
// Var service messages — round-trip serialization tests.
// =============================================================================

TEST(MessageProtocolTest, VarSetMessageRoundTrip) {
    VarSetMessage msg;
    msg.header_.type_ = MessageType::VAR_SET;
    msg.var_name_ = "db00000001:counter";  // full name
    msg.value_ = "deadbeef";  // serialized bytes
    msg.type_name_ = "int";

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    VarSetMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.var_name_, "db00000001:counter");
    EXPECT_EQ(decoded.value_, "deadbeef");
    EXPECT_EQ(decoded.type_name_, "int");
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, VarGetMessageRoundTrip) {
    VarGetMessage msg;
    msg.header_.type_ = MessageType::VAR_GET;
    msg.var_name_ = "db00000002:config_value";  // full name

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    VarGetMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.var_name_, "db00000002:config_value");
}

TEST(MessageProtocolTest, VarAckMessageGetHit) {
    VarAckMessage ack;
    ack.header_.type_ = MessageType::VAR_ACK;
    ack.var_name_ = "db00000003:threshold";  // full name
    ack.success_ = true;
    ack.value_ = "0a0b0c";
    ack.type_name_ = "float";

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    VarAckMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.var_name_, "db00000003:threshold");
    EXPECT_TRUE(decoded.success_);
    EXPECT_EQ(decoded.value_, "0a0b0c");
    EXPECT_EQ(decoded.type_name_, "float");
    EXPECT_TRUE(decoded.error_message_.empty());
}

TEST(MessageProtocolTest, VarAckMessageSetReject) {
    VarAckMessage ack;
    ack.header_.type_ = MessageType::VAR_ACK;
    ack.var_name_ = "db00000004:frozen_key";  // full name
    ack.success_ = false;
    ack.error_message_ = "var already exists (immutable)";

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    VarAckMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_FALSE(decoded.success_);
    EXPECT_TRUE(decoded.value_.empty());
    EXPECT_EQ(decoded.error_message_, "var already exists (immutable)");
}

TEST(MessageProtocolTest, VarRemoveMessageRoundTrip) {
    VarRemoveMessage msg;
    msg.header_.type_ = MessageType::VAR_REMOVE;
    msg.var_name_ = "db00000005:stale_var";  // full name

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    VarRemoveMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.var_name_, "db00000005:stale_var");
}

TEST(MessageProtocolTest, VarRemoveMessageClearAll) {
    // empty var_name_ means clear all vars of the db.
    VarRemoveMessage msg;
    msg.header_.type_ = MessageType::VAR_REMOVE;
    msg.var_name_ = "db00000006:";  // empty short name = clear all

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    VarRemoveMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.var_name_, "db00000006:");
}

TEST(MessageProtocolTest, VarBroadcastMessageRoundTrip) {
    VarBroadcastMessage msg;
    msg.header_.type_ = MessageType::VAR_BROADCAST;
    msg.var_name_ = "db00000007:rejected_var";  // full name
    msg.is_modification_reject_ = true;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    VarBroadcastMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.var_name_, "db00000007:rejected_var");
    EXPECT_TRUE(decoded.is_modification_reject_);
}

TEST(MessageProtocolTest, VarBroadcastMessageRemoval) {
    VarBroadcastMessage msg;
    msg.header_.type_ = MessageType::VAR_BROADCAST;
    msg.var_name_ = "db00000008:removed_var";  // full name
    msg.is_modification_reject_ = false;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    VarBroadcastMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_FALSE(decoded.is_modification_reject_);
}

TEST(MessageProtocolTest, VarPayloadRoundTrip) {
    VarPayload vp;
    vp.var_name = "vp_key";
    vp.value = "vp_value_bytes";
    vp.type_name = "MyClass";

    CMString encoded;
    FLY_ENCODE(vp, encoded);

    VarPayload decoded;
    FLY_DECODE(encoded, VarPayload, decoded);
    EXPECT_EQ(decoded.var_name, "vp_key");
    EXPECT_EQ(decoded.value, "vp_value_bytes");
    EXPECT_EQ(decoded.type_name, "MyClass");
}

TEST(MessageProtocolTest, TaskAssignMessageWithVarPayloads) {
    TaskAssignMessage msg;
    msg.header_.type_ = MessageType::TASK_ASSIGN;
    msg.task_id_ = 42;
    msg.task_name_ = "task_with_vars";
    msg.task_module_ = "m";
    msg.args_ = {"arg1"};
    msg.var_payloads_.push_back({"var_a", "val_a", "int"});
    msg.var_payloads_.push_back({"var_b", "val_b", "str"});

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    TaskAssignMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id_, 42u);
    EXPECT_EQ(decoded.args_.size(), 1u);
    ASSERT_EQ(decoded.var_payloads_.size(), 2u);
    EXPECT_EQ(decoded.var_payloads_[0].var_name, "var_a");
    EXPECT_EQ(decoded.var_payloads_[0].value, "val_a");
    EXPECT_EQ(decoded.var_payloads_[0].type_name, "int");
    EXPECT_EQ(decoded.var_payloads_[1].var_name, "var_b");
    EXPECT_EQ(decoded.var_payloads_[1].value, "val_b");
}

TEST(MessageProtocolTest, TaskAssignMessageEmptyVarPayloads) {
    // Backward-compatible case: no vars declared.
    TaskAssignMessage msg;
    msg.header_.type_ = MessageType::TASK_ASSIGN;
    msg.task_id_ = 7;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    TaskAssignMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_id_, 7u);
    EXPECT_TRUE(decoded.var_payloads_.empty());
}

TEST(MessageProtocolTest, IsValidMessageTypeCoversVarTypes) {
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::VAR_SET)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::VAR_GET)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::VAR_ACK)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::VAR_REMOVE)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::VAR_BROADCAST)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::DATABASE_FREEZE_ACK)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::DELETE_DATA)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::DELETE_DATA_ACK)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::MERGE_CLEANUP)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::MERGE_CLEANUP_ACK)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::LOG_MESSAGE)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::MSG_COUNT_REQUEST)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::MSG_COUNT_REPORT)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::MSG_LIMIT_SYNC)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::PEER_RPC_REQUEST)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::WORKER_BACKUP_SUGGEST)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::STORAGE_SPAWN_REQUEST)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::STORAGE_SPAWN_ACK)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::WORKER_PROBE)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::WORKER_PROBE_ACK)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::TASK_SUBMIT_ACK)));
    EXPECT_TRUE(is_valid_message_type(static_cast<uint8_t>(MessageType::STOP_NOW)));
    EXPECT_FALSE(is_valid_message_type(59));  // upper bound is 58
    EXPECT_FALSE(is_valid_message_type(0));
}

// WorkerBackupSuggestMessage: worker → master 上报 TIER2 读增量（count/bytes/size）。
TEST(MessageProtocolTest, WorkerBackupSuggestRoundTrip) {
    WorkerBackupSuggestMessage msg;
    msg.header_.type_ = MessageType::WORKER_BACKUP_SUGGEST;
    msg.worker_id_ = 7;
    msg.object_name_ = "my_db:hot_obj";
    msg.delta_count_ = 150;
    msg.delta_bytes_ = 2147483648ull;  // 2GB
    msg.size_bytes_ = 536870912;       // 512MB

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    WorkerBackupSuggestMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 7u);
    EXPECT_EQ(decoded.object_name_, "my_db:hot_obj");
    EXPECT_EQ(decoded.delta_count_, 150u);
    EXPECT_EQ(decoded.delta_bytes_, 2147483648ull);
    EXPECT_EQ(decoded.size_bytes_, 536870912);
    EXPECT_TRUE(buffer.empty());
}

// Net-probe messages (network-aware read priority): request asks the peer to
// echo back a payload of a given size; response carries that payload so the
// caller can measure round-trip throughput.
TEST(MessageProtocolTest, NetProbeRequestEncodeDecode) {
    NetProbeRequestMessage req;
    req.header_.type_ = MessageType::NET_PROBE_REQUEST;
    req.payload_size_ = 256 * 1024;
    req.probe_seq_ = 77;

    CMString encoded = MessageProtocol::encode(req);
    CMString buffer = encoded;

    NetProbeRequestMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.payload_size_, 256u * 1024u);
    EXPECT_EQ(decoded.probe_seq_, 77u);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, NetProbeResponseCarriesPayload) {
    NetProbeResponseMessage resp;
    resp.header_.type_ = MessageType::NET_PROBE_RESPONSE;
    resp.probe_seq_ = 77;
    resp.payload_.resize(4096);
    for (size_t i = 0; i < resp.payload_.size(); ++i) {
        resp.payload_[i] = static_cast<uint8_t>(i & 0xFF);
    }

    CMString encoded = MessageProtocol::encode(resp);
    CMString buffer = encoded;

    NetProbeResponseMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.probe_seq_, 77u);
    ASSERT_EQ(decoded.payload_.size(), 4096u);
    EXPECT_EQ(decoded.payload_[0], 0u);
    EXPECT_EQ(decoded.payload_[4095], 255u);
}

TEST(MessageProtocolTest, DeleteDataMessageRoundTrip) {
    DeleteDataMessage msg;
    msg.header_.type_ = MessageType::DELETE_DATA;
    msg.db_path_ = "abc123def4";
    msg.data_path_ = "/ssd/source_data";
    msg.writer_ids_ = {"worker_0", "worker_1", "worker_2"};

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    DeleteDataMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_path_, "abc123def4");
    EXPECT_EQ(decoded.data_path_, "/ssd/source_data");
    ASSERT_EQ(decoded.writer_ids_.size(), 3u);
    EXPECT_EQ(decoded.writer_ids_[0], "worker_0");
    EXPECT_EQ(decoded.writer_ids_[1], "worker_1");
    EXPECT_EQ(decoded.writer_ids_[2], "worker_2");
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, DeleteDataAckRoundTrip) {
    DeleteDataAckMessage ack;
    ack.header_.type_ = MessageType::DELETE_DATA_ACK;
    ack.worker_id_ = 7;
    ack.db_path_ = "abc123def4";
    ack.success_ = true;
    ack.deleted_count_ = 3;
    ack.error_message_ = "";
    ack.deleted_writer_ids_ = {"worker_0", "worker_1", "worker_2"};

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    DeleteDataAckMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 7u);
    EXPECT_EQ(decoded.db_path_, "abc123def4");
    EXPECT_TRUE(decoded.success_);
    EXPECT_EQ(decoded.deleted_count_, 3);
    ASSERT_EQ(decoded.deleted_writer_ids_.size(), 3u);
    EXPECT_EQ(decoded.deleted_writer_ids_[0], "worker_0");
}

TEST(MessageProtocolTest, DeleteDataAckFailurePath) {
    DeleteDataAckMessage ack;
    ack.header_.type_ = MessageType::DELETE_DATA_ACK;
    ack.db_path_ = "xyz789";
    ack.success_ = false;
    ack.error_message_ = "data_path not accessible";
    ack.deleted_writer_ids_ = {};  // 部分成功时可能为空

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    DeleteDataAckMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_FALSE(decoded.success_);
    EXPECT_EQ(decoded.error_message_, "data_path not accessible");
    EXPECT_TRUE(decoded.deleted_writer_ids_.empty());
}

TEST(MessageProtocolTest, MergeCleanupMessageRoundTrip) {
    MergeCleanupMessage msg;
    msg.header_.type_ = MessageType::MERGE_CLEANUP;
    msg.db_path_ = "merge_db_002";
    msg.data_path_ = "/ssd/merged_data";
    msg.exempt_worker_ids_ = {5, 6};  // merge target workers 免清理

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    MergeCleanupMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_path_, "merge_db_002");
    EXPECT_EQ(decoded.data_path_, "/ssd/merged_data");
    ASSERT_EQ(decoded.exempt_worker_ids_.size(), 2u);
    EXPECT_EQ(decoded.exempt_worker_ids_[0], 5u);
    EXPECT_EQ(decoded.exempt_worker_ids_[1], 6u);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, MergeCleanupAckRoundTrip) {
    MergeCleanupAckMessage ack;
    ack.header_.type_ = MessageType::MERGE_CLEANUP_ACK;
    ack.worker_id_ = 7;
    ack.db_path_ = "merge_db_002";

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    MergeCleanupAckMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 7u);
    EXPECT_EQ(decoded.db_path_, "merge_db_002");
    EXPECT_TRUE(buffer.empty());
}

// =============================================================================
// Message 日志系统 — round-trip 序列化测试。
// =============================================================================

TEST(MessageProtocolTest, LogMessageRoundTrip) {
    LogMessage msg;
    msg.header_.type_ = MessageType::LOG_MESSAGE;
    msg.worker_id_ = 3;
    msg.level_ = LogLevel::WARN;
    msg.domain_id_ = "SOLVER::0047";
    msg.source_ = 12;
    msg.msg_ = "收敛于 0.001，迭代 47 轮";

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    LogMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 3u);
    EXPECT_EQ(decoded.level_, LogLevel::WARN);
    EXPECT_EQ(decoded.domain_id_, "SOLVER::0047");
    EXPECT_EQ(decoded.source_, 12);
    EXPECT_EQ(decoded.msg_, "收敛于 0.001，迭代 47 轮");
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, MessageCountRequestRoundTrip) {
    MessageCountRequestMessage req;
    req.header_.type_ = MessageType::MSG_COUNT_REQUEST;
    req.header_.message_id_ = 99;

    CMString encoded = MessageProtocol::encode(req);
    CMString buffer = encoded;

    MessageCountRequestMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.header_.message_id_, 99u);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, MessageCountReportRoundTrip) {
    MessageCountReportMessage report;
    report.header_.type_ = MessageType::MSG_COUNT_REPORT;
    report.worker_id_ = 5;
    report.id_keys_ = {"SOLVER::0047", "SYS::0001"};
    report.id_values_ = {30, 2};
    report.domain_keys_ = {"SOLVER", "SYS"};
    report.domain_values_ = {30, 2};

    CMString encoded = MessageProtocol::encode(report);
    CMString buffer = encoded;

    MessageCountReportMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 5u);
    ASSERT_EQ(decoded.id_keys_.size(), 2u);
    EXPECT_EQ(decoded.id_keys_[0], "SOLVER::0047");
    EXPECT_EQ(decoded.id_keys_[1], "SYS::0001");
    ASSERT_EQ(decoded.id_values_.size(), 2u);
    EXPECT_EQ(decoded.id_values_[0], 30u);
    EXPECT_EQ(decoded.id_values_[1], 2u);
    ASSERT_EQ(decoded.domain_keys_.size(), 2u);
    EXPECT_EQ(decoded.domain_keys_[0], "SOLVER");
    EXPECT_EQ(decoded.domain_keys_[1], "SYS");
    ASSERT_EQ(decoded.domain_values_.size(), 2u);
    EXPECT_EQ(decoded.domain_values_[0], 30u);
    EXPECT_EQ(decoded.domain_values_[1], 2u);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, MessageLimitSyncRoundTrip) {
    MessageLimitSyncMessage sync;
    sync.header_.type_ = MessageType::MSG_LIMIT_SYNC;
    sync.global_limit_ = 15;
    sync.domain_keys_ = {"SOLVER", "SYS"};
    sync.domain_values_ = {100, 50};
    sync.id_keys_ = {"SOLVER::0047", "SYS::0001"};
    sync.id_values_ = {5, 10};

    CMString encoded = MessageProtocol::encode(sync);
    CMString buffer = encoded;

    MessageLimitSyncMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.global_limit_, 15);
    ASSERT_EQ(decoded.domain_keys_.size(), 2u);
    EXPECT_EQ(decoded.domain_keys_[0], "SOLVER");
    EXPECT_EQ(decoded.domain_values_[0], 100);
    ASSERT_EQ(decoded.id_keys_.size(), 2u);
    EXPECT_EQ(decoded.id_keys_[0], "SOLVER::0047");
    EXPECT_EQ(decoded.id_values_[0], 5);
    EXPECT_TRUE(buffer.empty());
}

// ── DataResponseMessage status 枚举传递 ────────────────────────────
//
// Bug T2: DataResponseMessage 没有 status 字段，data_server 用 error_message_
// 魔法字符串（"DATA_NOT_READY"/"OBJECT_NOT_FOUND"）承载状态码，client 字面量比较。
// 拼写/大小写/协议演进都会静默破坏分派。
//
// 修复: DataResponseMessage 加 ResponseStatus 枚举字段，server 填枚举、client 按枚举判。

TEST(MessageProtocolTest, DataResponseStatusEnumRoundTrip) {
    // NOT_READY 状态应通过序列化保留
    DataResponseMessage resp;
    resp.header_.type_ = MessageType::DATA_RESPONSE;
    resp.object_name_ = "db:test_obj";
    resp.success_ = false;
    resp.status_ = ResponseStatus::NOT_READY;

    CMString encoded = MessageProtocol::encode(resp);
    CMString buffer = encoded;

    DataResponseMessage decoded;
    ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.status_, ResponseStatus::NOT_READY);
    EXPECT_EQ(decoded.object_name_, "db:test_obj");
}

TEST(MessageProtocolTest, DataResponseStatusAllValues) {
    // 所有状态值都应正确 round-trip
    for (auto s : {ResponseStatus::SUCCESS, ResponseStatus::NOT_READY,
                   ResponseStatus::NOT_FOUND, ResponseStatus::ERROR}) {
        DataResponseMessage resp;
        resp.header_.type_ = MessageType::DATA_RESPONSE;
        resp.status_ = s;

        CMString encoded = MessageProtocol::encode(resp);
        CMString buffer = encoded;

        DataResponseMessage decoded;
        ASSERT_TRUE(MessageProtocol::decode(buffer, decoded));
        EXPECT_EQ(decoded.status_, s)
            << "status " << static_cast<int>(s) << " did not round-trip";
    }
}

// ── get_type 错误路径不应返回合法消息类型 ──────────────────────────
//
// Bug T1: get_type 在所有错误路径（buffer 太短、total_len 非法、type 非法）
// 都 return MessageType::REGISTER（值=1，合法高频类型）。调用方无法区分
// "真注册消息"和"解析失败"。
//
// 修复: 错误路径返回 MessageType::INVALID（值=0，哨兵）。

TEST(MessageProtocolTest, GetTypeEmptyBufferReturnsInvalid) {
    CMString empty;
    // BUG: 当前返回 REGISTER（合法类型），应返回 INVALID。
    EXPECT_EQ(MessageProtocol::get_type(empty), MessageType::INVALID);
}

TEST(MessageProtocolTest, GetTypeTooShortBufferReturnsInvalid) {
    CMString short_buf(4, '\0');  // 只有 4 字节，不够 5 字节帧头
    EXPECT_EQ(MessageProtocol::get_type(short_buf), MessageType::INVALID);
}

TEST(MessageProtocolTest, GetTypeInvalidTotalLenReturnsInvalid) {
    // total_len=0 是非法的（合法帧至少 1 字节 type）
    CMString bad;
    bad.resize(5, '\0');  // [4B total_len=0][1B type]
    // total_len field 全 0
    EXPECT_EQ(MessageProtocol::get_type(bad), MessageType::INVALID);
}

TEST(MessageProtocolTest, GetTypeInvalidTypeByteReturnsInvalid) {
    // 构造一个 total_len 合法但 type 字节为 0 的帧（0 = INVALID，不在合法范围）
    CMString bad;
    bad.resize(6, '\0');
    char* p = &bad[0];
    write_be32(p, 1);     // total_len=1（合法：至少 1 字节 type）
    p[4] = 0;             // type=0 (INVALID)
    EXPECT_EQ(MessageProtocol::get_type(bad), MessageType::INVALID);
}

TEST(MessageProtocolTest, GetTypeValidRegisterStillWorks) {
    // 确保合法 REGISTER 消息仍正确返回 REGISTER（不是 INVALID）
    RegisterMessage reg_msg;
    reg_msg.worker_id_ = 1;
    CMString encoded = MessageProtocol::encode(reg_msg);
    EXPECT_EQ(MessageProtocol::get_type(encoded), MessageType::REGISTER);
}


}  // namespace fly
