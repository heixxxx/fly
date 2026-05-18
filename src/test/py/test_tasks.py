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
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/test/export'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from fly import as_task
from _fly_test import EXTestObject, ex_test_parallel_read


@as_task()
def write_data_task(db, idx):
    obj = EXTestObject(idx, "test_obj_" + str(idx))
    db.write_object("test." + str(idx), obj)


@as_task(inputs=lambda db, names: list(names))
def concurrent_read_task(db, names):
    total, local_count, remote_count = ex_test_parallel_read(db, names)
    db.write_object("summary", total)


@as_task()
def entry_task(db):
    names = []
    for i in range(10):
        names.append(db.get_obj_name("test." + str(i)))
        write_data_task(db, i)
    concurrent_read_task(db, names)
