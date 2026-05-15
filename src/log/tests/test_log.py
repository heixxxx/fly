import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 
                                '../../../bazel-bin/src/log/export'))

import _fly_log as log

def test_master_logger():
    log.init_master("test_logs/")
    
    master = log.get_master_log()
    
    master.info("MasterAgent", "Master started")
    master.flush()
    
    with open("test_logs/master.log", "r") as f:
        line = f.readline()
        assert "[INFO]" in line
        assert "[MasterAgent]" in line
        assert "Master started" in line
    
    log.shutdown()
    print("PASS: test_master_logger")

def test_worker_logger():
    log.init_worker(1, "test_logs/")
    
    worker = log.get_worker_log(1)
    
    worker.debug("WorkerAgent", "Worker initializing")
    worker.flush()
    
    with open("test_logs/worker1.log", "r") as f:
        line = f.readline()
        assert "[DEBUG]" in line
        assert "[WorkerAgent]" in line
    
    log.shutdown()
    print("PASS: test_worker_logger")

def test_log_level():
    log.init_master("test_logs/")
    
    master = log.get_master_log()
    master.set_level(log.EXLogLevel.INFO)
    
    assert master.get_level() == log.EXLogLevel.INFO
    
    master.debug("Test", "Should not appear")
    master.info("Test", "Should appear")
    master.flush()
    
    with open("test_logs/master.log", "r") as f:
        lines = f.readlines()
        assert len(lines) == 1
        assert "[DEBUG]" not in lines[0]
    
    log.shutdown()
    print("PASS: test_log_level")

def test_all_levels():
    log.init_master("test_logs/")
    
    master = log.get_master_log()
    
    master.debug("Test", "Debug")
    master.info("Test", "Info")
    master.warn("Test", "Warn")
    master.error("Test", "Error")
    master.flush()
    
    with open("test_logs/master.log", "r") as f:
        lines = f.readlines()
        assert len(lines) == 4
    
    log.shutdown()
    print("PASS: test_all_levels")

if __name__ == "__main__":
    import shutil
    if os.path.exists("test_logs"):
        shutil.rmtree("test_logs")
    
    test_master_logger()
    shutil.rmtree("test_logs")
    
    test_worker_logger()
    shutil.rmtree("test_logs")
    
    test_log_level()
    shutil.rmtree("test_logs")
    
    test_all_levels()
    shutil.rmtree("test_logs")
    
    print("\nAll Python log tests passed!")