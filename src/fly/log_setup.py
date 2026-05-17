import os


def setup_log_dir(base_dir="fly_log"):
    if not os.path.exists(base_dir):
        os.makedirs(base_dir, exist_ok=True)
        _update_latest_symlink(base_dir)
        return base_dir

    num = 1
    while os.path.exists(f"{base_dir}.{num}"):
        num += 1
    os.rename(base_dir, f"{base_dir}.{num}")
    os.makedirs(base_dir, exist_ok=True)
    _update_latest_symlink(base_dir)
    return base_dir


def _update_latest_symlink(base_dir):
    latest = base_dir + ".latest"
    if os.path.islink(latest):
        os.unlink(latest)
    elif os.path.exists(latest):
        os.remove(latest)
    os.symlink(os.path.basename(base_dir), latest)


__all__ = ['setup_log_dir']
