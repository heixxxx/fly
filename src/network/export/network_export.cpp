#include <export/cpp/export_macros.h>
#include <serialization/cpp/serialization_macros.h>
#include <network/cpp/connection_manager.h>
#include <network/cpp/tcp_connection_manager.h>
#include <network/cpp/message_types.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/io_thread_pool.h>
#include <storage/cpp/database.h>
#include <memory>

FLY_EXPORT_MODULE(_fly_network) {

FLY_EXPORT_ENUM(fly::TransportEventType, "EXNetTransportEventType")
    FLY_EXPORT_ENUM_VALUE("CONNECT", fly::TransportEventType::CONNECT)
    FLY_EXPORT_ENUM_VALUE("DATA", fly::TransportEventType::DATA)
    FLY_EXPORT_ENUM_VALUE("DISCONNECT", fly::TransportEventType::DISCONNECT)
    FLY_EXPORT_ENUM_VALUE("ERROR", fly::TransportEventType::ERROR);

FLY_EXPORT_ENUM(fly::MessageType, "EXNetMessageType")
    FLY_EXPORT_ENUM_VALUE("REGISTER", fly::MessageType::REGISTER)
    FLY_EXPORT_ENUM_VALUE("REGISTER_ACK", fly::MessageType::REGISTER_ACK)
    FLY_EXPORT_ENUM_VALUE("HEARTBEAT", fly::MessageType::HEARTBEAT)
    FLY_EXPORT_ENUM_VALUE("TASK_SUBMIT", fly::MessageType::TASK_SUBMIT)
    FLY_EXPORT_ENUM_VALUE("TASK_ASSIGN", fly::MessageType::TASK_ASSIGN)
    FLY_EXPORT_ENUM_VALUE("TASK_COMPLETE", fly::MessageType::TASK_COMPLETE)
    FLY_EXPORT_ENUM_VALUE("TASK_FAILED", fly::MessageType::TASK_FAILED)
    FLY_EXPORT_ENUM_VALUE("DATA_READY", fly::MessageType::DATA_READY)
    FLY_EXPORT_ENUM_VALUE("DATA_QUERY", fly::MessageType::DATA_QUERY)
    FLY_EXPORT_ENUM_VALUE("DATA_LOCATION", fly::MessageType::DATA_LOCATION)
    FLY_EXPORT_ENUM_VALUE("DATA_REQUEST", fly::MessageType::DATA_REQUEST)
    FLY_EXPORT_ENUM_VALUE("DATA_RESPONSE", fly::MessageType::DATA_RESPONSE)
    FLY_EXPORT_ENUM_VALUE("SHUTDOWN", fly::MessageType::SHUTDOWN);

FLY_EXPORT_CLASS(fly::TransportEvent, "EXNetTransportEvent")
    FLY_EXPORT_READONLY_ATTR("type", &fly::TransportEvent::type_)
    FLY_EXPORT_READONLY_ATTR("conn_id", &fly::TransportEvent::conn_id_)
    FLY_EXPORT_READONLY_ATTR("data", &fly::TransportEvent::data_)
    FLY_EXPORT_READONLY_ATTR("error_code", &fly::TransportEvent::error_code_)
    FLY_EXPORT_SERIALIZE(fly::TransportEvent);

FLY_EXPORT_CLASS(fly::MessageHeader, "EXNetMessageHeader")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("type", &fly::MessageHeader::type_)
    FLY_EXPORT_ATTR("message_id", &fly::MessageHeader::message_id_)
    FLY_EXPORT_ATTR("timestamp", &fly::MessageHeader::timestamp_)
    FLY_EXPORT_SERIALIZE(fly::MessageHeader);

FLY_EXPORT_CLASS(fly::HeartbeatMessage, "EXNetHeartbeatMessage")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("header", &fly::HeartbeatMessage::header_)
    FLY_EXPORT_ATTR("worker_id", &fly::HeartbeatMessage::worker_id_)
    FLY_EXPORT_ATTR("running_tasks", &fly::HeartbeatMessage::running_tasks_)
    FLY_EXPORT_ATTR("attributes", &fly::HeartbeatMessage::attributes_)
    FLY_EXPORT_SERIALIZE(fly::HeartbeatMessage);

FLY_EXPORT_CLASS(fly::RegisterMessage, "EXNetRegisterMessage")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("header", &fly::RegisterMessage::header_)
    FLY_EXPORT_ATTR("worker_id", &fly::RegisterMessage::worker_id_)
    FLY_EXPORT_ATTR("hostname", &fly::RegisterMessage::hostname_)
    FLY_EXPORT_ATTR("ip_address", &fly::RegisterMessage::ip_address_)
    FLY_EXPORT_ATTR("attributes", &fly::RegisterMessage::attributes_)
    FLY_EXPORT_ATTR("data_server_host", &fly::RegisterMessage::data_server_host_)
    FLY_EXPORT_ATTR("data_server_port", &fly::RegisterMessage::data_server_port_)
    FLY_EXPORT_SERIALIZE(fly::RegisterMessage);

FLY_EXPORT_CLASS(fly::DataRequestMessage, "EXNetDataRequestMessage")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("header", &fly::DataRequestMessage::header_)
    FLY_EXPORT_ATTR("object_name", &fly::DataRequestMessage::object_name_)
    FLY_EXPORT_ATTR("requesting_worker_id", &fly::DataRequestMessage::requesting_worker_id_)
    FLY_EXPORT_SERIALIZE(fly::DataRequestMessage);

FLY_EXPORT_CLASS(fly::DataResponseMessage, "EXNetDataResponseMessage")
    FLY_EXPORT_INIT()
    FLY_EXPORT_ATTR("header", &fly::DataResponseMessage::header_)
    FLY_EXPORT_ATTR("object_name", &fly::DataResponseMessage::object_name_)
    FLY_EXPORT_ATTR("success", &fly::DataResponseMessage::success_)
    FLY_EXPORT_ATTR("error_message", &fly::DataResponseMessage::error_message_)
    FLY_EXPORT_ATTR("compressed_data", &fly::DataResponseMessage::compressed_data_)
    FLY_EXPORT_ATTR("py_name", &fly::DataResponseMessage::py_name_)
    FLY_EXPORT_SERIALIZE(fly::DataResponseMessage);

FLY_EXPORT_CLASS(fly::IOThreadPool, "EXNetIOThreadPool")
    FLY_EXPORT_INIT(int)
    FLY_EXPORT_METHOD("start", &fly::IOThreadPool::start)
    FLY_EXPORT_METHOD("stop", &fly::IOThreadPool::stop)
    FLY_EXPORT_METHOD("queue_size", &fly::IOThreadPool::queue_size)
    FLY_EXPORT_METHOD("is_idle", &fly::IOThreadPool::is_idle)
    FLY_EXPORT_METHOD("process_completions", &fly::IOThreadPool::process_completions);

FLY_EXPORT_FUNCTION("ex_net_create_connection_manager", [](const fly::CMString& type) -> CMUniquePtr<fly::ConnectionManager> {
    return fly::create_connection_manager(type);
});

FLY_EXPORT_FUNCTION("ex_net_encode_message", [](const fly::HeartbeatMessage& msg) -> fly_export::bytes {
    fly::CMString encoded = fly::MessageProtocol::encode(msg);
    return fly_export::bytes(encoded.data(), encoded.size());
});

FLY_EXPORT_FUNCTION("ex_net_decode_heartbeat", [](fly_export::bytes data) -> fly::HeartbeatMessage {
    fly::CMString buffer(data.c_str(), data.size());
    fly::HeartbeatMessage msg;
    fly::MessageProtocol::decode(buffer, msg);
    return msg;
});

}