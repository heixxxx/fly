"""Layer 2 network test: Transport, MessageProtocol, IOThreadPool Python bindings"""

import sys
import os
import pytest
import tempfile
import time

_bazel_bin = os.path.join(os.path.dirname(__file__), '..', 'bazel-bin', 'src', 'network', 'export')
if os.path.exists(_bazel_bin):
    sys.path.insert(0, _bazel_bin)


def test_import_module():
    """Test that _fly_network can be imported"""
    from _fly_network import EXNetTransportEventType
    assert EXNetTransportEventType is not None


def test_transport_event_type_enum():
    """Test TransportEventType enum values"""
    from _fly_network import EXNetTransportEventType
    assert EXNetTransportEventType.CONNECT == 0
    assert EXNetTransportEventType.DATA == 1
    assert EXNetTransportEventType.DISCONNECT == 2
    assert EXNetTransportEventType.ERROR == 3


def test_message_type_enum():
    """Test MessageType enum values"""
    from _fly_network import EXNetMessageType
    assert EXNetMessageType.REGISTER == 1
    assert EXNetMessageType.HEARTBEAT == 3
    assert EXNetMessageType.DATA_REQUEST == 11
    assert EXNetMessageType.DATA_RESPONSE == 12


def test_transport_event_creation():
    """Test TransportEvent struct"""
    from _fly_network import EXNetTransportEvent, EXNetTransportEventType
    event = EXNetTransportEvent()
    assert event.conn_id == 0
    assert event.error_code == 0


def test_message_header_creation():
    """Test MessageHeader struct"""
    from _fly_network import EXNetMessageHeader, EXNetMessageType
    header = EXNetMessageHeader()
    header.type = EXNetMessageType.HEARTBEAT
    header.message_id = 1
    header.timestamp = int(time.time() * 1000)
    
    assert header.type == EXNetMessageType.HEARTBEAT
    assert header.message_id == 1


def test_heartbeat_message_creation():
    """Test HeartbeatMessage struct"""
    from _fly_network import EXNetHeartbeatMessage, EXNetMessageType
    msg = EXNetHeartbeatMessage()
    msg.worker_id = 123
    msg.running_tasks = [1, 2, 3]
    msg.attributes = ["gpu", "ssd"]
    
    assert msg.worker_id == 123
    assert len(msg.running_tasks) == 3
    assert len(msg.attributes) == 2


def test_register_message_creation():
    """Test RegisterMessage struct"""
    from _fly_network import EXNetRegisterMessage
    msg = EXNetRegisterMessage()
    msg.worker_id = 42
    msg.role = "hybrid"
    msg.attributes = ["has_gpu", "large_memory"]
    
    assert msg.worker_id == 42
    assert msg.role == "hybrid"


def test_data_request_response_messages():
    """Test DataRequest and DataResponse messages"""
    from _fly_network import EXNetDataRequestMessage, EXNetDataResponseMessage
    
    req = EXNetDataRequestMessage()
    req.object_name = "test/object"
    req.requesting_worker_id = 10
    assert req.object_name == "test/object"
    
    resp = EXNetDataResponseMessage()
    resp.object_name = "test/object"
    resp.data = b"binary_payload"
    assert resp.data == b"binary_payload"


def test_io_thread_pool_creation():
    """Test IOThreadPool creation and basic operations"""
    from _fly_network import EXNetIOThreadPool
    
    pool = EXNetIOThreadPool(2)
    pool.start()
    
    assert pool.is_idle() == True
    assert pool.queue_size() == 0
    
    pool.stop()
    assert pool.is_idle() == True


def test_io_thread_pool_submit():
    """Test IOThreadPool submit with completion"""
    from _fly_network import EXNetIOThreadPool
    
    pool = EXNetIOThreadPool(1)
    pool.start()
    
    import threading
    counter = threading.Semaphore(0)
    
    def task():
        pass
    
    def completion():
        counter.release()
    
    pool.submit(task, completion)
    
    time.sleep(0.2)
    pool.process_completions()
    
    assert counter.acquire(timeout=1)
    
    pool.stop()


def test_ex_net_create_transport():
    """Test ex_net_create_transport factory function"""
    from _fly_network import ex_net_create_transport
    
    transport = ex_net_create_transport("tcp")
    assert transport is not None


def test_ex_net_create_invalid_transport():
    """Test ex_net_create_transport with invalid type raises error"""
    from _fly_network import ex_net_create_transport
    
    with pytest.raises(RuntimeError):
        ex_net_create_transport("invalid_type")


def test_ex_net_encode_decode_heartbeat():
    """Test ex_net_encode/ex_net_decode heartbeat message roundtrip"""
    from _fly_network import EXNetHeartbeatMessage, EXNetMessageType, ex_net_encode_message, ex_net_decode_heartbeat
    
    msg = EXNetHeartbeatMessage()
    msg.worker_id = 999
    msg.running_tasks = [10, 20]
    msg.attributes = ["fast", "reliable"]
    
    encoded = ex_net_encode_message(msg)
    assert len(encoded) > 4
    
    decoded = ex_net_decode_heartbeat(encoded)
    assert decoded.worker_id == 999
    assert len(decoded.running_tasks) == 2


def test_is_cpp_marker():
    """Test is_cpp marker is present on exported classes"""
    from _fly_network import EXNetHeartbeatMessage, EXNetRegisterMessage, EXNetMessageHeader
    
    for cls in [EXNetHeartbeatMessage, EXNetRegisterMessage, EXNetMessageHeader]:
        assert hasattr(cls, 'is_cpp')
        assert cls.is_cpp == True


def test_pickle_roundtrip():
    """Test pickle serialization roundtrip"""
    from _fly_network import EXNetHeartbeatMessage
    import pickle
    
    msg = EXNetHeartbeatMessage()
    msg.worker_id = 777
    msg.running_tasks = [1, 2]
    
    pickled = pickle.dumps(msg)
    unpickled = pickle.loads(pickled)
    
    assert unpickled.worker_id == 777
    assert len(unpickled.running_tasks) == 2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])