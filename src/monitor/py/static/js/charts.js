// ECharts 通用封装：统一主题色、时间轴 tooltip、实例生命周期。
// 主题色经 theme.cssVar() 从 CSS 变量实时读取（canvas 不消费 CSS 变量；
// 主题切换后页面重建、图表实例随 mount 新建，自然取到新值）。
import { cssVar } from './theme.js';

// 图表主题色快照（axis/label/tooltip/数据系列色）。fallback 为深色默认
// 值——CSS 变量读不到时（样式表未就绪等）保证图表仍可读。
export function chartColors() {
  return {
    axis: cssVar('--chart-axis', '#4a5d72'),
    label: cssVar('--chart-label', '#99aabb'),
    split: cssVar('--chart-split', '#28323f'),
    panel2: cssVar('--panel2', '#1c2530'),
    text: cssVar('--text', '#d7e0ea'),
    accent: cssVar('--accent', '#4aa8ff'),
    blue: cssVar('--ser-blue', '#4aa8ff'),
    yellow: cssVar('--ser-yellow', '#e8b339'),
    cyan: cssVar('--ser-cyan', '#6fd3e8'),
    orange: cssVar('--ser-orange', '#ff9d5c'),
    green: cssVar('--ser-green', '#3fb972'),
    purple: cssVar('--ser-purple', '#c79bf2'),
    hostTotal: cssVar('--ser-muted', '#93a7bc'),
  };
}

export function makeChart(el, option) {
  const c = chartColors();
  const chart = echarts.init(el, null, { renderer: 'canvas' });
  chart.setOption({
    backgroundColor: 'transparent',
    textStyle: { color: c.label, fontFamily: 'inherit' },
    grid: { left: 56, right: 22, top: 34, bottom: 38 },
    tooltip: {
      trigger: 'axis',
      backgroundColor: c.panel2,
      borderColor: c.axis,
      textStyle: { color: c.text, fontSize: 12 },
      axisPointer: { lineStyle: { color: c.axis } },
    },
    legend: { textStyle: { color: c.label }, top: 2, left: 4, itemGap: 16 },
    xAxis: {
      type: 'time',
      axisLine: { lineStyle: { color: c.axis } },
      axisLabel: { color: c.label, formatter: '{HH}:{mm}:{ss}' },
      splitLine: { show: false },
    },
    yAxis: [
      { type: 'value', axisLine: { show: false }, axisLabel: { color: c.label },
        splitLine: { lineStyle: { color: c.split } } },
    ],
    ...option,
  });
  return chart;
}

// 网络速率序列：累计字节差分（计数器回绕/重启 → 速率 0）。
export function rateSeries(times, counters) {
  const out = [];
  for (let i = 1; i < times.length; i++) {
    const dt = (times[i] - times[i - 1]) / 1000;
    const d = counters[i] - counters[i - 1];
    const v = (d >= 0 && dt > 0) ? d / dt : 0;
    out.push([times[i], +v.toFixed(1)]);
  }
  return out;
}

// series 快捷构造。
export function line(name, data, color, yAxisIndex = 0, area = false) {
  return {
    name, type: 'line', data, yAxisIndex, showSymbol: false,
    lineStyle: { width: 1.5, color }, itemStyle: { color },
    areaStyle: area ? { color, opacity: 0.08 } : undefined,
  };
}

// ---- tooltip 数值单位格式（详细数值不可裸显原始字节）----
// MB 口径与 fmtGB 一致（1024²）——轴标签仍用 fmtGB 自适应，tooltip 按
// 用户裁定统一 MB，避免大数 GB 折算损失精度。
export function fmtMb(v) {
  return (v == null || isNaN(v)) ? '-' : (v / 1048576).toFixed(1) + ' MB';
}
export function fmtMbPerS(v) {
  return (v == null || isNaN(v)) ? '-' : (v / 1048576).toFixed(1) + ' MB/s';
}
export function fmtPctVal(v) {
  return (v == null || isNaN(v)) ? '-' : (+v).toFixed(1) + '%';
}
