import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/agent/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/log/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/storage/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/core/export'))

from _fly_agent import EXAgentWorker
from _fly_storage import ex_stg_get_data_service


def test_set_single_property():
    worker = EXAgentWorker(1, "127.0.0.1", 19900, [])
    worker.set_data_service(ex_stg_get_data_service())
    worker.set_worker_property(["gpu"])
    props = worker.get_worker_properties()
    assert "gpu" in props, f"Expected 'gpu' in {props}"
    print("PASS: test_set_single_property")


def test_set_batch_properties():
    worker = EXAgentWorker(2, "127.0.0.1", 19901, ["python"])
    worker.set_data_service(ex_stg_get_data_service())
    worker.set_worker_property(["gpu", "cuda"])
    props = worker.get_worker_properties()
    assert "python" in props, f"Expected 'python' in {props}"
    assert "gpu" in props, f"Expected 'gpu' in {props}"
    assert "cuda" in props, f"Expected 'cuda' in {props}"
    print("PASS: test_set_batch_properties")


def test_set_duplicate_property():
    worker = EXAgentWorker(3, "127.0.0.1", 19902, ["python"])
    worker.set_data_service(ex_stg_get_data_service())
    worker.set_worker_property(["python"])
    props = worker.get_worker_properties()
    assert len(props) == 1, f"Expected 1 property, got {len(props)}: {props}"
    print("PASS: test_set_duplicate_property")


def test_remove_property():
    worker = EXAgentWorker(4, "127.0.0.1", 19903, ["python", "gpu"])
    worker.set_data_service(ex_stg_get_data_service())
    worker.remove_worker_property(["gpu"])
    props = worker.get_worker_properties()
    assert "gpu" not in props, f"'gpu' should be removed, got {props}"
    assert "python" in props, f"Expected 'python' to remain, got {props}"
    print("PASS: test_remove_property")


def test_remove_nonexistent_property():
    worker = EXAgentWorker(5, "127.0.0.1", 19904, ["python"])
    worker.set_data_service(ex_stg_get_data_service())
    worker.remove_worker_property(["nonexistent"])
    props = worker.get_worker_properties()
    assert len(props) == 1, f"Expected 1 property, got {len(props)}"
    print("PASS: test_remove_nonexistent_property")


def test_get_properties_returns_copy():
    worker = EXAgentWorker(6, "127.0.0.1", 19905, ["python"])
    worker.set_data_service(ex_stg_get_data_service())
    props1 = worker.get_worker_properties()
    worker.set_worker_property(["gpu"])
    props2 = worker.get_worker_properties()
    assert len(props1) == 1, f"Original snapshot should be unchanged"
    assert len(props2) == 2, f"New snapshot should have 2 properties"
    print("PASS: test_get_properties_returns_copy")


if __name__ == "__main__":
    test_set_single_property()
    test_set_batch_properties()
    test_set_duplicate_property()
    test_remove_property()
    test_remove_nonexistent_property()
    test_get_properties_returns_copy()
    print("\nAll worker property tests passed!")
