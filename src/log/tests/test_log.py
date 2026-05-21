import sys
import os
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 
                                '../../../bazel-bin/src/log/export'))

import _fly_log as log

def test_master_logger():
    if os.path.exists("test_logs"):
        shutil.rmtree("test_logs")
    
    log.init_log("test_logs/", 0)
    
    log.INFO("Master started")
    log.flush_log()
    
    with open("test_logs/master.log", "r") as f:
        line = f.readline()
        assert "[INFO]" in line
        assert "Master started" in line
    
    log.shutdown_log()
    shutil.rmtree("test_logs")
    print("PASS: test_master_logger")

def test_worker_logger():
    if os.path.exists("test_logs"):
        shutil.rmtree("test_logs")
    
    log.init_log("test_logs/", 1)
    
    log.DBG("Worker initializing")
    log.flush_log()
    
    with open("test_logs/worker1.log", "r") as f:
        line = f.readline()
        assert "[DEBUG]" in line
        assert "Worker initializing" in line
    
    log.shutdown_log()
    shutil.rmtree("test_logs")
    print("PASS: test_worker_logger")

def test_log_level():
    if os.path.exists("test_logs"):
        shutil.rmtree("test_logs")
    
    log.init_log("test_logs/", 0)
    log.set_log_level(log.EXLogLevel.INFO)
    
    log.DBG("Should not appear")
    log.INFO("Should appear")
    log.flush_log()
    
    with open("test_logs/master.log", "r") as f:
        lines = f.readlines()
        assert len(lines) == 1
        assert "[DEBUG]" not in lines[0]
    
    log.shutdown_log()
    shutil.rmtree("test_logs")
    print("PASS: test_log_level")

def test_all_levels():
    if os.path.exists("test_logs"):
        shutil.rmtree("test_logs")
    
    log.init_log("test_logs/", 0)
    
    log.DBG("Debug message")
    log.INFO("Info message")
    log.WARN("Warn message")
    log.ERR("Error message")
    log.flush_log()
    
    with open("test_logs/master.log", "r") as f:
        lines = f.readlines()
        assert len(lines) == 4
    
    log.shutdown_log()
    shutil.rmtree("test_logs")
    print("PASS: test_all_levels")

if __name__ == "__main__":
    test_master_logger()
    test_worker_logger()
    test_log_level()
    test_all_levels()
    
    print("\nAll Python log tests passed!")