#include <storage/cpp/storage_manager.h>
#include <storage/cpp/data_service.h>
#include <filesystem>

namespace fs = std::filesystem;

CMSharedPtr<StorageManager> StorageManager::instance() {
    static CMSharedPtr<StorageManager> inst = CMMakeShared<StorageManager>();
    return inst;
}

StorageManager::StorageManager() = default;

CMSharedPtr<Database> StorageManager::get_or_create_database(
    const CMString& db_path,
    const CMString& data_path) {

    // 快照模式（§13.3 锁内禁 IO）：Database 构造是重 IO（建目录/读 idx/开
    // 数据文件），禁止在容器锁内（原 get_or_insert factory 在锁内执行）。
    // 锁内只 find；miss 时锁外构造，插入前 double-checked——并发输者丢弃
    // 自建实例（析构在锁外），返回既有实例，语义与原 get-or-create 一致。
    if (auto existing = databases_.find(db_path)) {
        return *existing;
    }
    fs::create_directories(db_path);
    if (!data_path.empty()) {
        fs::create_directories(data_path);
    }
    auto created = CMMakeShared<Database>(db_path, data_path);
    CMSharedPtr<Database> winner;
    databases_.with_lock([&](DBMap& m) {
        auto [it, ok] = m.emplace(db_path, created);
        winner = it->second;
    });
    // created 若输给并发插入者，此处释放引用 → ~Database（含 WBQ drain）
    // 在容器锁外执行
    return winner;
}

void StorageManager::close_all() {
    // 快照模式（§13.3 锁内禁 IO）：freeze 是重活（WBQ drain + 落盘 marker +
    // unlink + flush_vars），锁内只拷快照并清容器，freeze 全部在锁外执行。
    // 快照+清空原子完成后，close_all 期间并发 get_or_create 的实例要么已在
    // 快照内被 freeze，要么在清空后存活（原 iterate→clear 两段加锁存在
    // "夹缝创建的实例被无 freeze 清掉"的窗口）。
    DBMap doomed;
    databases_.with_lock([&](DBMap& m) {
        doomed = std::move(m);
        m.clear();
    });
    for (auto& [path, db] : doomed) {
        (void)path;
        if (!db->is_frozen()) {
            db->freeze();
        }
    }
}

void StorageManager::reset() {
    // unregister_database 取 DataService 自身锁——嵌套锁拆掉：先锁内收路径，
    // 锁外逐个注销。
    DBMap doomed;
    {
        databases_.with_lock([&](DBMap& m) {
            doomed = std::move(m);
            m.clear();
        });
    }
    for (auto& [path, db] : doomed) {
        (void)path;
        fly::DataService::instance()->unregister_database(db->get_db_path());
    }
}
