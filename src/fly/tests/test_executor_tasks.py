"""Test task module for executor tests."""
import _fly_log as log


def simple_task():
    log.INFO("simple_task executed")
    return "done"


def add_numbers(nums):
    a, b = nums
    return a + b


def raising_task():
    raise RuntimeError("intentional test error")


def freeze_task(db):
    db.write_object("freeze_test", "data")
    db.freeze()
    return "frozen"