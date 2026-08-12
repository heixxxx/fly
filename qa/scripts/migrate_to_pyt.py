#!/usr/bin/env python3
"""迁移脚本：为 qa/ 下所有 test_*.py（无同名 .pyt）生成单 sub case .pyt 骨架。

生成的 .pyt 调 run_subcase("test_x.py", timeout=60)，原 test_x.py 退为 sub case 脚本
（被 .pyt 接管，runqa 不再独立发现）。

跳过：
  - 已有同名 .pyt（手动转的复合 case）
  - 含 subprocess.run/Popen 的 test_*.py（复合 wrapper，需手动审查拆分/合并为多 sub case）

用法：
  python3 qa/scripts/migrate_to_pyt.py --dry-run    # 预览将生成哪些 .pyt
  python3 qa/scripts/migrate_to_pyt.py              # 实际生成
"""
import os
import sys

QA_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _is_log_artifact(name):
    if name.startswith("."):
        return True
    if name == "logs":
        return True
    if name.endswith(".latest"):
        return True
    if "." in name:
        stem, _, suffix = name.rpartition(".")
        if stem and suffix.isdigit():
            return True
    return False


def main():
    dry_run = "--dry-run" in sys.argv
    generated = []
    skipped_compound = []
    for root, dirs, files in os.walk(QA_DIR, followlinks=False):
        dirs[:] = [d for d in dirs if not _is_log_artifact(d)]
        for f in files:
            if not (f.startswith("test_") and f.endswith(".py")):
                continue
            stem = f[:-3]
            pyt_path = os.path.join(root, stem + ".pyt")
            if os.path.exists(pyt_path):
                continue  # 已有 .pyt（手动转的）
            full = os.path.join(root, f)
            try:
                with open(full) as tf:
                    src = tf.read()
            except Exception:
                continue
            # 复合 wrapper（含 subprocess 编排）跳过，需手动转
            if "subprocess.run" in src or "subprocess.Popen" in src:
                skipped_compound.append(os.path.relpath(full, QA_DIR))
                continue
            generated.append(pyt_path)
            if not dry_run:
                content = (
                    f'# 自动生成：单进程 case 包装（原 {f} 退为 sub case）。\n'
                    f'# 若此 case 实际是多阶段复合场景，改为手动编排多 run_subcase。\n'
                    f'run_subcase("{f}", timeout=60)\n'
                )
                with open(pyt_path, "w") as pf:
                    pf.write(content)

    print(f"=== 将生成 {len(generated)} 个 .pyt（单进程 case）===")
    for p in generated:
        print(f"  + {os.path.relpath(p, QA_DIR)}")
    print(f"\n=== 跳过 {len(skipped_compound)} 个复合 wrapper（需手动转）===")
    for p in skipped_compound:
        print(f"  ~ {p}")


if __name__ == "__main__":
    main()
