// localStorage 安全封装：隐私模式/禁用站点数据下访问 localStorage 直接
// 抛 SecurityError——吞掉退回内存默认，绝不阻断模块加载（index.html 的
// FOUC 内联脚本是同口径 try/catch；此前的裸访问会让整页空白）。
export function lsGet(key) {
  try {
    return localStorage.getItem(key);
  } catch (e) {
    return null;
  }
}

export function lsSet(key, value) {
  try {
    localStorage.setItem(key, value);
  } catch (e) {
    // 持久化失败不影响本会话（下次打开回默认值）。
  }
}
