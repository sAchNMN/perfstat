<script setup>
import { computed } from 'vue'

// 手写 SVG 折线图：不引图表库，保持前端零额外依赖（与项目「零包管理」一致）
// 点数超过可用像素宽度时等距抽稀（DESIGN 第 14 节未决项 2 的落实）
const props = defineProps({
  points: { type: Array, default: () => [] }, // [{ ts, <valueKey>: number }]
  valueKey: { type: String, default: 'cpu' },
  width: { type: Number, default: 900 },
  height: { type: Number, default: 110 },
  color: { type: String, default: '#6cb6ff' },
  label: { type: String, default: '' },
  format: { type: Function, default: (v) => v.toFixed(2) },
})

const PAD = { left: 8, right: 8, top: 12, bottom: 16 }

const sampled = computed(() => {
  const pts = props.points
  const limit = Math.max(2, Math.floor(props.width - PAD.left - PAD.right))
  if (pts.length <= limit) {
    return pts
  }
  const step = pts.length / limit
  const out = []
  for (let i = 0; i < limit; i += 1) {
    out.push(pts[Math.floor(i * step)])
  }
  out.push(pts[pts.length - 1])
  return out
})

const peak = computed(() => {
  let max = 0
  for (const p of sampled.value) {
    max = Math.max(max, Number(p[props.valueKey]) || 0)
  }
  return max > 0 ? max : 1
})

const linePath = computed(() => {
  const pts = sampled.value
  if (pts.length < 2) return ''
  const innerW = props.width - PAD.left - PAD.right
  const innerH = props.height - PAD.top - PAD.bottom
  const span = peak.value
  return pts
    .map((p, i) => {
      const x = PAD.left + (i / (pts.length - 1)) * innerW
      const v = Math.max(0, Number(p[props.valueKey]) || 0)
      const y = PAD.top + innerH - (v / span) * innerH
      return `${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`
    })
    .join(' ')
})

const areaPath = computed(() => {
  if (!linePath.value) return ''
  const innerW = props.width - PAD.left - PAD.right
  const bottom = props.height - PAD.bottom
  return `${linePath.value} L${(PAD.left + innerW).toFixed(1)},${bottom} L${PAD.left},${bottom} Z`
})

const ticks = computed(() => {
  const pts = sampled.value
  if (pts.length < 2) return []
  const last = pts.length - 1
  const innerW = props.width - PAD.left - PAD.right
  return [0, Math.floor(last / 2), last].map((i) => ({
    key: `${i}-${pts[i].ts}`,
    x: PAD.left + (i / last) * innerW,
    text: formatTime(pts[i].ts),
  }))
})

function formatTime(ts) {
  const d = new Date(ts * 1000)
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
}
</script>

<template>
  <div class="chart">
    <div class="head">
      <span class="name">{{ label }}</span>
      <span class="meta">
        <span class="dim">{{ points.length }} 点</span>
        <span class="peak">峰值 {{ format(peak) }}</span>
      </span>
    </div>

    <svg
      v-if="points.length > 1"
      class="plot"
      :viewBox="`0 0 ${width} ${height}`"
      preserveAspectRatio="none"
    >
      <line
        :x1="PAD.left"
        :y1="height - PAD.bottom"
        :x2="width - PAD.right"
        :y2="height - PAD.bottom"
        class="axis"
      />
      <path :d="areaPath" :fill="color" opacity="0.13" />
      <path :d="linePath" :stroke="color" fill="none" stroke-width="1.5" />
    </svg>
    <div v-else class="empty">该区间无数据</div>

    <div v-if="ticks.length" class="ticks">
      <span v-for="t in ticks" :key="t.key">{{ t.text }}</span>
    </div>
  </div>
</template>

<style scoped>
.chart {
  background: #12151b;
  border: 1px solid #232833;
  border-radius: 5px;
  padding: 8px 10px 4px;
}

.head {
  display: flex;
  align-items: baseline;
  gap: 10px;
  margin-bottom: 4px;
}

.name {
  color: #b7bec9;
}

.meta {
  margin-left: auto;
  display: flex;
  gap: 12px;
  font-size: 12px;
}

.dim {
  color: #5c6572;
}

.peak {
  color: #8b93a1;
}

.plot {
  display: block;
  width: 100%;
  height: 110px;
}

.axis {
  stroke: #232833;
  stroke-width: 1;
}

.ticks {
  display: flex;
  justify-content: space-between;
  color: #5c6572;
  font-size: 11px;
  padding: 0 2px 2px;
}

.empty {
  color: #5c6572;
  text-align: center;
  padding: 34px 0;
  font-size: 12px;
}
</style>
