// ECharts 通用封装：统一暗色主题、时间轴 tooltip、实例生命周期。
const AXIS_COLOR = '#37424f';
const LABEL_COLOR = '#7a8a9c';

export function makeChart(el, option) {
  const chart = echarts.init(el, null, { renderer: 'canvas' });
  chart.setOption({
    backgroundColor: 'transparent',
    textStyle: { color: LABEL_COLOR, fontFamily: 'inherit' },
    grid: { left: 60, right: 60, top: 30, bottom: 44 },
    tooltip: {
      trigger: 'axis',
      backgroundColor: '#1c2530',
      borderColor: AXIS_COLOR,
      textStyle: { color: '#d7e0ea', fontSize: 12 },
      axisPointer: { lineStyle: { color: AXIS_COLOR } },
    },
    legend: { textStyle: { color: LABEL_COLOR }, top: 0 },
    xAxis: {
      type: 'time',
      axisLine: { lineStyle: { color: AXIS_COLOR } },
      axisLabel: { color: LABEL_COLOR, formatter: '{HH}:{mm}:{ss}' },
      splitLine: { show: false },
    },
    yAxis: [
      { type: 'value', axisLine: { show: false }, axisLabel: { color: LABEL_COLOR },
        splitLine: { lineStyle: { color: '#212b37' } } },
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
