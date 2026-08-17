# __setstate__ 对损坏 bytes 的错误翻译契约：FLY_DECODE 的 runtime_error
# 必须在 binding 层转成 ValueError（损坏 pickle 的 Python 惯例异常类型），
# 不允许裸 RuntimeError 穿透（issue 002 暴露点收口）。
import sys
import os


def _find_module():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    paths_to_try = [
        os.path.join(script_dir, '..', '..', '..', 'bazel-bin', 'src', 'test', 'export'),
        os.path.join(script_dir, '..', '..', '..', 'src', 'test', 'export'),
        os.path.join(os.getcwd(), 'bazel-bin', 'src', 'test', 'export'),
    ]
    for p in paths_to_try:
        if os.path.exists(os.path.join(p, '_fly_test.so')):
            return p
    return None


module_path = _find_module()
if module_path:
    sys.path.insert(0, module_path)

from _fly_test import EXTestObject


def test_setstate_corrupted_bytes_raises_value_error():
    # 正常 round-trip 先确认管道可用。
    obj = EXTestObject(7, "corrupt-me")
    state = obj.__getstate__()
    restored = EXTestObject.__new__(EXTestObject)
    EXTestObject.__setstate__(restored, state)
    assert restored.value == 7

    # 截断 bytes（size < 4 跳过 magic 分支 → 裸 FLY_DECODE 数据不足 → 抛）：
    # 期望 ValueError 而非 RuntimeError。
    broken = EXTestObject.__new__(EXTestObject)
    try:
        EXTestObject.__setstate__(broken, b"\x01")
    except ValueError:
        pass
    else:
        raise AssertionError("corrupted state must raise ValueError, got no exception")

    # 破坏 magic（前 4 字节）→ 同样走裸 FLY_DECODE，garbage 解码失败 → ValueError。
    bad_magic = bytearray(state)
    bad_magic[0] ^= 0xFF
    broken2 = EXTestObject.__new__(EXTestObject)
    try:
        EXTestObject.__setstate__(broken2, bytes(bad_magic))
    except ValueError:
        pass
    else:
        raise AssertionError("bad-magic state must raise ValueError, got no exception")


if __name__ == "__main__":
    test_setstate_corrupted_bytes_raises_value_error()
    print("setstate_error_test: OK")
