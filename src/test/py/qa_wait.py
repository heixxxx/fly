"""QA 等待 helper：异步结果的轮询等待（零容忍裸 sleep;assert 的替代）。

用法（case 脚本内）：
    from test import wait_until
    assert wait_until(lambda: master.failed_tasks, timeout=30), \
        "group death must surface within 30s"

    from test import wait_tasks
    assert wait_tasks(master, 3, "counter/no_failed_tasks"), \
        "tasks did not complete"
"""
import time

# 失败任务诊断清单的规模上限：最多带多少个失败任务、单条错误消息截断到多少字符。
_FAILURE_DETAIL_MAX_TASKS = 10
_FAILURE_DETAIL_MAX_CHARS = 200


def wait_until(cond, timeout: float = 30.0, interval: float = 0.05) -> bool:
    """轮询等待 cond() 为真。deadline 内为真返回 True，超时返回 False。

    cond 无参可调用；interval 为采样间距。替代「裸 sleep 后断言」——
    断言的事件到达时间不确定，固定睡眠在慢机/高负载下必然 flaky。
    """
    deadline = time.monotonic() + timeout
    while True:
        if cond():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval)


def wait_tasks(master, expected, key=None, timeout: float = 30.0,
               interval: float = 0.05) -> bool:
    """轮询等待任务完成：完成数达到 expected，或出现失败任务，或超时。

    替代「assert wait_for(完成数 >= N) + assert 没有失败任务」这一对。关键
    差别：失败任务不计入已完成，干等只会熬满超时——这里任务一失败就立即
    停止等待，并把「没有失败任务」记为顶层布尔判定，detail 带失败任务及其
    错误消息，差异报告因此能直接指出是哪个任务、为什么。

    key 非空时记录一条判定（写进子阶段的缺省结果文件）；错误消息截断到
    200 字符，最多带 10 个失败任务；取错误消息失败时写取不到的原因。
    返回「没有失败任务，且已完成数达到 expected」。
    """
    deadline = time.monotonic() + timeout
    while True:
        if master.failed_tasks:
            break
        if len(master.completed_tasks) >= expected:
            break
        if time.monotonic() >= deadline:
            break
        time.sleep(interval)
    no_failures = len(master.failed_tasks) == 0
    if key is not None:
        _record_verdict(key, no_failures, detail=_failure_details(master))
    return no_failures and len(master.completed_tasks) >= expected


def _failure_details(master):
    """失败任务清单（诊断信息）：任务编号 + 异常行（最后一个非空行，截断）。

    任务失败时 get_task_error 返回的是整段回溯，有用的根因是末尾的
    「异常类型: 消息」行；差异报告展示 detail 时只显示前 80 个字符
    （qa_record 的 _MAX_VALUE_TEXT），存整段回溯会把根因截掉——所以只存
    最后一个非空行，这一行仍超过 200 字符时保留前 200 字符加省略号。
    完整回溯不进 detail，master.log 与 worker 日志里有。
    """
    entries = []
    for task_id in list(master.failed_tasks)[:_FAILURE_DETAIL_MAX_TASKS]:
        try:
            error = master.get_task_error(task_id)
        except Exception as exc:
            error = f"<取错误消息失败: {type(exc).__name__}: {exc}>"
        if not isinstance(error, str):
            error = repr(error)
        error = _last_nonempty_line(error)
        if len(error) > _FAILURE_DETAIL_MAX_CHARS:
            error = error[:_FAILURE_DETAIL_MAX_CHARS] + "…"
        entries.append({"task": task_id, "error": error})
    return entries


def _last_nonempty_line(text):
    """取文本最后一个非空行；全是空白行时返回去尾部空白的原文。"""
    lines = [line for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else text.rstrip()


def _record_verdict(key, value, *, detail=None):
    """记录一条顶层布尔判定（qa_record.record 的间接层）。

    经由模块属性转发：单元测试替换本属性即可在不依赖 fly 的环境下断言
    记录调用；fly 进程内 import 时才解析真实记录接口（qa_wait 保持
    顶层纯标准库，与 qa_record 同约定）。
    """
    from .qa_record import record
    record(key, value, detail=detail)
