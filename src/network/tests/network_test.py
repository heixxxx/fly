"""Layer 2 network test: Transport, MessageProtocol, IOThreadPool Python bindings"""

import sys
import os
import unittest
import tempfile
import time

def _find_module():
    script_dir = os.path.dirname(__file__)
    paths_to_try = [
        os.path.join(script_dir, '..', '..', '..', 'bazel-bin', 'src', 'network', 'export'),
        os.path.join(script_dir, '..', 'export'),
        os.path.join(os.getcwd(), 'bazel-bin', 'src', 'network', 'export'),
    ]
    for p in paths_to_try:
        if os.path.exists(os.path.join(p, '_fly_network.so')):
            return p
    return None

module_path = _find_module()
if module_path:
    sys.path.insert(0, module_path)


class TestImportModule(unittest.TestCase):
    def test_import_module(self):
        """Test that _fly_network can be imported"""
        from _fly_network import EXNetTransportEventType
        self.assertIsNotNone(EXNetTransportEventType)


class TestTransportEventTypeEnum(unittest.TestCase):
    def test_transport_event_type_enum(self):
        """Test TransportEventType enum values"""
        from _fly_network import EXNetTransportEventType
        self.assertEqual(EXNetTransportEventType.CONNECT.value, 0)
        self.assertEqual(EXNetTransportEventType.DATA.value, 1)
        self.assertEqual(EXNetTransportEventType.DISCONNECT.value, 2)
        self.assertEqual(EXNetTransportEventType.ERROR.value, 3)


class TestMessageTypeEnum(unittest.TestCase):
    def test_message_type_enum(self):
        """Test MessageType enum values"""
        from _fly_network import EXNetMessageType
        self.assertEqual(EXNetMessageType.REGISTER.value, 1)
        self.assertEqual(EXNetMessageType.HEARTBEAT.value, 3)
        self.assertEqual(EXNetMessageType.DATA_REQUEST.value, 11)
        self.assertEqual(EXNetMessageType.DATA_RESPONSE.value, 12)


class TestMessageHeaderCreation(unittest.TestCase):
    def test_message_header_creation(self):
        """Test MessageHeader struct"""
        from _fly_network import EXNetMessageHeader, EXNetMessageType
        header = EXNetMessageHeader()
        header.type = EXNetMessageType.HEARTBEAT
        header.message_id = 1
        header.timestamp = int(time.time() * 1000)
        
        self.assertEqual(header.type, EXNetMessageType.HEARTBEAT)
        self.assertEqual(header.message_id, 1)


class TestHeartbeatMessageCreation(unittest.TestCase):
    def test_heartbeat_message_creation(self):
        """Test HeartbeatMessage struct"""
        from _fly_network import EXNetHeartbeatMessage, EXNetMessageType
        msg = EXNetHeartbeatMessage()
        msg.worker_id = 123
        msg.running_tasks = [1, 2, 3]
        msg.attributes = ["gpu", "ssd"]
        
        self.assertEqual(msg.worker_id, 123)
        self.assertEqual(len(msg.running_tasks), 3)
        self.assertEqual(len(msg.attributes), 2)


class TestRegisterMessageCreation(unittest.TestCase):
    def test_register_message_creation(self):
        """Test RegisterMessage struct"""
        from _fly_network import EXNetRegisterMessage
        msg = EXNetRegisterMessage()
        msg.worker_id = 42
        msg.hostname = "gpu-node-1"
        msg.ip_address = "10.0.1.5"
        msg.attributes = ["has_gpu", "large_memory"]
        
        self.assertEqual(msg.worker_id, 42)
        self.assertEqual(msg.hostname, "gpu-node-1")
        self.assertEqual(msg.ip_address, "10.0.1.5")


class TestDataRequestResponseMessages(unittest.TestCase):
    def test_data_request_response_messages(self):
        from _fly_network import EXNetDataRequestMessage, EXNetDataResponseMessage
        
        req = EXNetDataRequestMessage()
        req.object_name = "test/object"
        req.requesting_worker_id = 10
        self.assertEqual(req.object_name, "test/object")
        
        resp = EXNetDataResponseMessage()
        resp.object_name = "test/object"
        resp.success = True
        self.assertEqual(resp.object_name, "test/object")


class TestIOThreadPoolCreation(unittest.TestCase):
    def test_io_thread_pool_creation(self):
        """Test IOThreadPool creation and basic operations"""
        from _fly_network import EXNetIOThreadPool
        
        pool = EXNetIOThreadPool(2)
        pool.start()
        
        self.assertTrue(pool.is_idle())
        self.assertEqual(pool.queue_size(), 0)
        
        pool.stop()
        self.assertTrue(pool.is_idle())


class TestExNetEncodeDecodeHeartbeat(unittest.TestCase):
    def test_ex_net_encode_decode_heartbeat(self):
        """Test ex_net_encode/ex_net_decode heartbeat message roundtrip"""
        from _fly_network import EXNetHeartbeatMessage, EXNetMessageType, ex_net_encode_message, ex_net_decode_heartbeat
        
        msg = EXNetHeartbeatMessage()
        msg.worker_id = 999
        msg.running_tasks = [10, 20]
        msg.attributes = ["fast", "reliable"]
        
        encoded = ex_net_encode_message(msg)
        self.assertGreater(len(encoded), 4)
        
        decoded = ex_net_decode_heartbeat(encoded)
        self.assertEqual(decoded.worker_id, 999)
        self.assertEqual(len(decoded.running_tasks), 2)


class TestIsCppMarker(unittest.TestCase):
    def test_is_cpp_marker(self):
        """Test is_cpp marker is present on exported classes"""
        from _fly_network import EXNetHeartbeatMessage, EXNetRegisterMessage, EXNetMessageHeader
        
        for cls in [EXNetHeartbeatMessage, EXNetRegisterMessage, EXNetMessageHeader]:
            instance = cls()
            self.assertTrue(hasattr(instance, 'is_cpp'))
            self.assertEqual(instance.is_cpp, True)


class TestPickleRoundtrip(unittest.TestCase):
    def test_pickle_roundtrip(self):
        """Test pickle serialization roundtrip"""
        from _fly_network import EXNetHeartbeatMessage
        import pickle
        
        msg = EXNetHeartbeatMessage()
        msg.worker_id = 777
        msg.running_tasks = [1, 2]
        
        pickled = pickle.dumps(msg)
        unpickled = pickle.loads(pickled)
        
        self.assertEqual(unpickled.worker_id, 777)
        self.assertEqual(len(unpickled.running_tasks), 2)


if __name__ == "__main__":
    unittest.main()