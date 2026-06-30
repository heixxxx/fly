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
    msg.db_id_ = "db_abc";
    msg.write_context_hash_ = "hash123";
    msg.writer_id_ = "w_abc";
    msg.size_bytes_ = 1234567;

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    WriteRegisterMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.worker_id_, 5u);
    EXPECT_EQ(decoded.object_name_, "db_abc:result/output");
    EXPECT_EQ(decoded.db_id_, "db_abc");
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
    
    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;
    
    TaskSubmitMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.task_name_, "my_task");
    EXPECT_EQ(decoded.task_module_, "my_module");
    EXPECT_EQ(decoded.args_.size(), 2u);
    EXPECT_EQ(decoded.inputs_.size(), 2u);
}

TEST(MessageProtocolTest, DbPathRequestResponseMessages) {
    DbPathRequestMessage req;
    req.header_.type_ = MessageType::DB_PATH_REQUEST;
    req.db_id_ = "my_database";

    CMString encoded_req = MessageProtocol::encode(req);
    CMString buffer = encoded_req;

    DbPathRequestMessage decoded_req;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded_req));
    EXPECT_EQ(decoded_req.db_id_, "my_database");

    DbPathResponseMessage resp;
    resp.header_.type_ = MessageType::DB_PATH_RESPONSE;
    resp.db_id_ = "my_database";
    resp.base_path_ = "/data/base";
    resp.data_path_ = "/data/base/data";
    resp.success_ = true;

    CMString encoded_resp = MessageProtocol::encode(resp);
    CMString buffer_resp = encoded_resp;

    DbPathResponseMessage decoded_resp;
    EXPECT_TRUE(MessageProtocol::decode(buffer_resp, decoded_resp));
    EXPECT_EQ(decoded_resp.db_id_, "my_database");
    EXPECT_EQ(decoded_resp.base_path_, "/data/base");
    EXPECT_EQ(decoded_resp.data_path_, "/data/base/data");
    EXPECT_TRUE(decoded_resp.success_);
}

TEST(MessageProtocolTest, DatabaseFreezeNotificationRoundTrip) {
    DatabaseFreezeNotification msg;
    msg.header_.type_ = MessageType::DATABASE_FREEZE;
    msg.db_id_ = "frozen_db_001";
    msg.task_id_ = 42;   // 非 stream 模式 master 登记 pending frozen 需要 task_id

    CMString encoded = MessageProtocol::encode(msg);
    CMString buffer = encoded;

    DatabaseFreezeNotification decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_id_, "frozen_db_001");
    EXPECT_EQ(decoded.task_id_, 42u);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, DatabaseFreezeAckRoundTripSuccess) {
    DatabaseFreezeAckMessage ack;
    ack.header_.type_ = MessageType::DATABASE_FREEZE_ACK;
    ack.db_id_ = "frozen_db_001";
    ack.success_ = true;

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    DatabaseFreezeAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_id_, "frozen_db_001");
    EXPECT_TRUE(decoded.success_);
    EXPECT_EQ(decoded.error_type_, TaskErrorType::UNKNOWN);
    EXPECT_TRUE(buffer.empty());
}

TEST(MessageProtocolTest, DatabaseFreezeAckRoundTripConflict) {
    // 冲突场景：db 已被其他 task freeze，master 拒绝 → fail-fast
    DatabaseFreezeAckMessage ack;
    ack.header_.type_ = MessageType::DATABASE_FREEZE_ACK;
    ack.db_id_ = "already_frozen_db";
    ack.success_ = false;
    ack.error_type_ = TaskErrorType::DB_ALREADY_FROZEN;

    CMString encoded = MessageProtocol::encode(ack);
    CMString buffer = encoded;

    DatabaseFreezeAckMessage decoded;
    EXPECT_TRUE(MessageProtocol::decode(buffer, decoded));
    EXPECT_EQ(decoded.db_id_, "already_frozen_db");
    EXPECT_FALSE(decoded.success_);
    EXPECT_EQ(decoded.error_type_, TaskErrorType::DB_ALREADY_FROZEN);
}

TEST(MessageProtocolTest, WriteRegisterAndAckMessages) {
    WriteRegisterMessage wr;
    wr.header_.type_ = MessageType::WRITE_REGISTER;
    wr.worker_id_ = 10;
    wr.object_name_ = "obj_key";
    wr.db_id_ = "db1";
    
    CMString encoded_wr = MessageProtocol::encode(wr);
    CMString buffer_wr = encoded_wr;
    
    WriteRegisterMessage decoded_wr;
    EXPECT_TRUE(MessageProtocol::decode(buffer_wr, decoded_wr));
    EXPECT_EQ(decoded_wr.worker_id_, 10u);
    EXPECT_EQ(decoded_wr.object_name_, "obj_key");
    EXPECT_EQ(decoded_wr.db_id_, "db1");
    
    WriteRegisterAckMessage ack;
    ack.header_.type_ = MessageType::WRITE_REGISTER_ACK;
    ack.object_name_ = "obj_key";
    ack.db_id_ = "db1";
    ack.success_ = true;
    ack.error_message_ = "";
    
    CMString encoded_ack = MessageProtocol::encode(ack);
    CMString buffer_ack = encoded_ack;
    
    WriteRegisterAckMessage decoded_ack;
    EXPECT_TRUE(MessageProtocol::decode(buffer_ack, decoded_ack));
    EXPECT_TRUE(decoded_ack.success_);
    EXPECT_EQ(decoded_ack.object_name_, "obj_key");
    EXPECT_EQ(decoded_ack.db_id_, "db1");
}

TEST(MessageProtocolTest, WriteRegisterAckFailure) {
    WriteRegisterAckMessage ack;
    ack.header_.type_ = MessageType::WRITE_REGISTER_ACK;
    ack.object_name_ = "bad_obj";
    ack.db_id_ = "bad_db";
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
    EXPECT_FALSE(is_valid_message_type(42));  // upper bound is 41
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

}  // namespace fly