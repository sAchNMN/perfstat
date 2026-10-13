<script setup>
import { computed, onMounted, ref, watch } from 'vue'

import { getStats } from '@/api/client'
import LineChart from '@/components/LineChart.vue'

// 时间范围预设：区间越长，时间桶越粗，避免返回上万个点
const RANGES = [
  { label: '1 小时', seconds: 3600, bucket: 60 },
  { label: '6 小时', seconds: 6 * 3600, bucket: 300 },
  { label: '24 小时', seconds: 24 * 3600, bucket: 900 },
  { label: '7 天', seconds: 7 * 24 * 3600, bucket: 3600 },
]

const rangeIndex = ref(0)
const loading = ref(false)
const error = ref('')
const data = ref({ entities: [], series: [], from: 0, to: 0, bucket: 60 })
const selected = ref(null)
const entitySeries = ref([])

function windowOf() {
  const r = RANGES[rangeIndex.value]
  const to = Math.floor(Date.now() / 1000)
  return { from: to - r.seconds, to, bucket: r.bucket }
}

async function load() {
  loading.value = true
  error.value = ''
  try {
    const w = windowOf()
    data.value = await getStats({ ...w, top: 50 })
    selected.value = null
    entitySeries.value = []
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
}

async function pick(row) {
  if (selected.value && selected.value.entity_id === row.entity_id) {
    selected.value = null
    entitySeries.value = []
    return
  }
  selected.value = row
  try {
    const w = windowOf()
    const res = await getStats({ ...w, entityId: row.entity_id })
    entitySeries.value = res.series
  } catch (e) {
    error.value = String(e)
  }
}

onMounted(load)
watch(rangeIndex, load)

const statusText = computed(() => {
  const n = data.value.entities.length
  const pts = data.value.series.length
  if (!n && !pts) return '该区间暂无落库数据（需先跑 record 或 serve 落库）'
  return `汇总 ${n} 个实体 | 曲线 ${pts} 点`
})

function formatKb(kb) {
  if (kb >= 1024 * 1024) return (kb / 1024 / 1024).toFixed(2) + ' GB'
  if (kb >= 1024) return (kb / 1024).toFixed(1) + ' MB'
  return kb + ' KB'
}

function formatCpu(v) {
  return v.toFixed(2) + ' %'
}

function kindLabel(kind) {
  return kind === 'service' ? '服务' : '进程'
}
</script>

<template>
  <div class="wrap">
    <div class="toolbar">
      <span class="dot" :class="error ? 'off' : 'on'"></span>
      <span :class="{ err: !!error }">{{ error || statusText }}</span>
      <span class="sep">|</span>
      <label v-for="(r, i) in RANGES" :key="r.label" class="range">
        <input v-model="rangeIndex" type="radio" :value="i" />
        {{ r.label }}
      </label>
      <button type="button" class="act" :disabled="loading" @click="load">
        {{ loading ? '查询中…' : '刷新' }}
      </button>
      <span class="spacer"></span>
      <span class="hint">桶粒度 {{ data.bucket }}s | 最多 50 行</span>
    </div>

    <div class="charts">
      <LineChart
        :points="data.series"
        value-key="cpu"
        color="#ff7b72"
        label="CPU 合计（占整机 %）"
        :format="formatCpu"
      />
      <LineChart
        :points="data.series"
        value-key="mem_kb"
        color="#6cb6ff"
        label="内存合计（工作集）"
        :format="formatKb"
      />
      <LineChart
        v-if="selected"
        :points="entitySeries"
        value-key="cpu"
        color="#d2a8ff"
        :label="`${selected.name} · CPU`"
        :format="formatCpu"
      />
    </div>

    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>类型</th>
            <th>名称</th>
            <th class="usage-col">用途</th>
            <th class="num">CPU 均值</th>
            <th class="num">CPU 峰值</th>
            <th class="num">内存均值</th>
            <th class="num">内存峰值</th>
            <th class="num">读</th>
            <th class="num">写</th>
            <th class="num">分钟</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="row in data.entities"
            :key="row.entity_id"
            :class="{ active: selected && selected.entity_id === row.entity_id }"
            @click="pick(row)"
          >
            <td class="dim">{{ kindLabel(row.kind) }}</td>
            <td :title="row.path">{{ row.name }}</td>
            <td class="usage-col">
              <span class="cat" :class="'src-' + (row.usage?.source || 'none')">{{
                row.usage?.category || '其他'
              }}</span>
              <span class="dim purpose">{{ row.usage?.purpose || '未知' }}</span>
            </td>
            <td class="num">{{ row.cpu_avg.toFixed(2) }}</td>
            <td class="num hot">{{ row.cpu_max.toFixed(2) }}</td>
            <td class="num">{{ formatKb(row.mem_avg_kb) }}</td>
            <td class="num">{{ formatKb(row.mem_max_kb) }}</td>
            <td class="num dim">{{ formatKb(row.io_r_kb) }}</td>
            <td class="num dim">{{ formatKb(row.io_w_kb) }}</td>
            <td class="num dim">{{ row.buckets }}</td>
          </tr>
          <tr v-if="data.entities.length === 0">
            <td colspan="10" class="empty">无数据。点某行可展开该实体的 CPU 曲线。</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.wrap {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 14px;
  background: #12151b;
  border-bottom: 1px solid #232833;
  color: #8b93a1;
  flex-wrap: wrap;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}

.dot.on {
  background: #3ddc84;
  box-shadow: 0 0 6px #3ddc84;
}

.dot.off {
  background: #e5534b;
}

.err {
  color: #e5534b;
}

.sep {
  color: #2d3542;
}

.range {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  color: #b7bec9;
  cursor: pointer;
  font-size: 12px;
}

.spacer {
  flex: 1;
}

.hint {
  color: #5c6572;
}

.act {
  background: #232833;
  border: 1px solid #2d3542;
  border-radius: 4px;
  color: #b7bec9;
  padding: 3px 10px;
  font-family: inherit;
  font-size: 12px;
  cursor: pointer;
}

.act:hover {
  border-color: #4ea1ff;
  color: #d7dae0;
}

.act:disabled {
  opacity: 0.6;
  cursor: default;
}

.charts {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 10px 14px;
  overflow: auto;
  flex: none;
}

.table-wrap {
  flex: 1;
  min-height: 0;
  overflow: auto;
}

table {
  width: 100%;
  border-collapse: collapse;
}

thead th {
  position: sticky;
  top: 0;
  background: #171b22;
  color: #9aa3b2;
  font-weight: 500;
  text-align: left;
  padding: 7px 12px;
  border-bottom: 1px solid #232833;
  white-space: nowrap;
}

td {
  padding: 5px 12px;
  border-bottom: 1px solid #191d24;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 360px;
}

tbody tr {
  cursor: pointer;
}

tbody tr:hover {
  background: #171b22;
}

tbody tr.active {
  background: #1b2432;
}

.num {
  text-align: right;
  font-variant-numeric: tabular-nums;
}

.dim {
  color: #6b7482;
}

.hot {
  color: #ffb454;
}

.usage-col {
  min-width: 220px;
}

.purpose {
  margin-left: 6px;
}

.cat {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 3px;
  background: #1d2a3a;
  color: #6cb6ff;
}

.cat.src-manual {
  background: #16271d;
  color: #3ddc84;
}

.cat.src-none {
  background: #232833;
  color: #6b7482;
}

.empty {
  text-align: center;
  color: #5c6572;
  padding: 30px;
  cursor: default;
}
</style>
