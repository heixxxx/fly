// 智能顶部浮窗（Timeline 工具栏 / Tasks 筛选栏共用的统一实现）：
// 原位置控件条滚出视口顶部后整体搬入固定浮窗，回滚可见即归位——数百行
// 表格/泳道的页面远超一屏，控件条滚出去后（排序/筛选/条纹开关）不可达。
//
//   · 判定用 main 滚动位置驱动（单一来源；IntersectionObserver 回调时序
//     在部分环境不可靠，仅 scroll 事件一路驱动）。
//   · moveSelf=false：逐个搬子元素，bar 壳留原位（Timeline 工具栏——
//     工具栏内部多段布局随浮窗样式独立排布）。
//   · moveSelf=true：整体搬 bar 自身并以 minHeight 占位防原位塌陷
//     （Tasks 筛选栏——.controls 祖先类承载控件样式，拆散会褪回原生白底）。
//
// 返回 { sync, destroy }：sync 供视图显隐切换后手动重估；destroy 解绑
// scroll 监听并移除浮窗壳（换页时 bar 所在子树整体销毁，无需先归位）。
export function createTopFloat({ bar, floatId, threshold = 62,
                                 moveSelf = false, canFloat = null }) {
  const float = document.createElement('div');
  float.id = floatId;
  float.style.display = 'none';
  document.body.appendChild(float);
  const mainEl = document.getElementById('main');
  const rect = bar.getBoundingClientRect ? bar.getBoundingClientRect() : null;
  const absTop = ((rect && rect.top) || 110) + mainEl.scrollTop;
  if (moveSelf && bar.style) {
    bar.style.minHeight = ((rect && rect.height) || 0) + 'px';
  }
  const home = { parent: bar.parentElement, next: bar.nextElementSibling };
  let floated = false;

  const sync = () => {
    const wantFloat = (!canFloat || canFloat()) &&
                      mainEl.scrollTop > absTop - threshold;
    if (floated === wantFloat) return;
    floated = wantFloat;
    if (floated) {
      if (moveSelf) float.appendChild(bar);
      else while (bar.firstChild) float.appendChild(bar.firstChild);
      float.style.display = 'block';
    } else if (moveSelf) {
      const { parent, next } = home;
      if (next && next.parentElement === parent) parent.insertBefore(bar, next);
      else if (parent) parent.appendChild(bar);
      float.style.display = 'none';
    } else {
      while (float.firstChild) bar.appendChild(float.firstChild);
      float.style.display = 'none';
    }
  };
  const onScroll = () => sync();
  mainEl.addEventListener('scroll', onScroll, { passive: true });
  return {
    sync,
    destroy() {
      mainEl.removeEventListener('scroll', onScroll);
      float.remove();
    },
  };
}
