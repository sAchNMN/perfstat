<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'

import { openRealtime, analyzeUsage } from '@/api/client'
import UsageCell from '@/components/UsageCell.vue'

// 后端每轮推一帧全量快照，前端整表替换
const processes = ref([])
const connected = ref(false)
const round = ref(0)

// 视图状态
const onlyBackground = ref(true)
const keyword = ref('')
const sortKey = ref('cpu')
const sortDesc = ref(true)

let source = null

onMounted(() => {
  source = openRealtime({
    onFrame: (list) => {
      processes.value = list
      round.value += 1
    },
    onOpen: () => (connected.value = true),
    onError: () => (connected.value = false),
  })
})

onBeforeUnmount(() => {
  if (source) source.close()
})

// 过滤 + 排序后的行
const rows = computed(() => {
  let list = processes.value.filter((p) => p.ok)
  if (onlyBackground.value) {
    list = list.filter((p) => p.bg)
  }
  const kw = keyword.value.trim().toLowerCase()
  if (kw) {
    list = list.filter((p) => p.name.toLowerCase().includes(kw))
  }

  const key = sortKey.value
  return [...list].sort((a, b) => {
    const x = a[key]
    const y = b[key]
    const result = typeof x === 'string' ? x.localeCompare(y) : x - y
    return sortDesc.value ? -result : result
  })
})

function setSort(key) {
  if (sortKey.value === key) {
    sortDesc.value = !sortDesc.value
  } else {
    sortKey.value = key
    sortDesc.value = true
  }
}

function arrow(key) {
  if (sortKey.value !== key) return ''
  return sortDesc.value ? ' ▾' : ' ▴'
}

function formatKb(kb) {
  if (kb >= 1024 * 1024) return (kb / 1024 / 1024).toFixed(2) + ' GB'
  if (kb >= 1024) return (kb / 1024).toFixed(1) + ' MB'
  return kb + ' KB'
}

// 本地批量识别：M5 只跑内置规则 + 人工覆盖，未命中的留给 M6 的 LLM 层
const analyzing = ref(false)
const analyzeText = ref('')

async function runAnalyze() {
  analyzing.value = true
  try {
    const r = await analyzeUsage()
    analyzeText.value = `命中 内置 ${r.builtin} / 人工 ${r.manual} / LLM ${r.llm}，未识别 ${r.unknown}（共 ${r.total}）`
  } catch (e) {
    analyzeText.value = '识别失败：' + String(e)
  } finally {
    analyzing.value = false
  }
}
</script>

<template>
  <div class="wrap">
    <div class="toolbar">
      <span class="dot" :class="connected ? 'on' : 'off'"></span>
      <span>{{ connected ? '已连接' : '未连接' }}</span>
      <span class="sep">|</span>
      <span>轮次 {{ round }}</span>
      <span class="sep">|</span>
      <span>显示 {{ rows.length }} 个</span>
      <label class="chk">
        <input v-model="onlyBackground" type="checkbox" />
        仅后台
      </label>
      <input v-model="keyword" class="search" placeholder="过滤进程名" />
      <button type="button" class="act" :disabled="analyzing" @click="runAnalyze">
        {{ analyzing ? '识别中…' : '本地识别' }}
      </button>
      <span v-if="analyzeText" class="hint">{{ analyzeText }}</span>
      <span class="spacer"></span>
      <span class="hint">每 2s 刷新</span>
    </div>

    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th class="clickable" @click="setSort('pid')">PID{{ arrow('pid') }}</th>
            <th class="clickable" @click="setSort('name')">名称{{ arrow('name') }}</th>
            <th class="usage-col">用途</th>
            <th class="num clickable" @click="setSort('cpu')">CPU %{{ arrow('cpu') }}</th>
            <th class="num clickable" @click="setSort('mem_kb')">内存{{ arrow('mem_kb') }}</th>
            <th class="num clickable" @click="setSort('io_r_kb')">读 KB/s{{ arrow('io_r_kb') }}</th>
            <th class="num clickable" @click="setSort('io_w_kb')">写 KB/s{{ arrow('io_w_kb') }}</th>
            <th>标记</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="p in rows" :key="p.pid + '-' + p.name">
            <td class="dim">{{ p.pid }}</td>
            <td :title="p.path">{{ p.name }}</td>
            <td class="usage-col">
              <UsageCell kind="process" :name="p.name" :path="p.path" :usage="p.usage" />
            </td>
            <td class="num" :class="{ hot: p.cpu >= 5 }">{{ p.cpu.toFixed(2) }}</td>
            <td class="num">{{ formatKb(p.mem_kb) }}</td>
            <td class="num dim">{{ p.io_r_kb }}</td>
            <td class="num dim">{{ p.io_w_kb }}</td>
            <td>
              <span v-if="p.sys" class="tag sys">系统</span>
              <span v-else-if="p.bg" class="tag bg">后台</span>
            </td>
          </tr>
          <tr v-if="rows.length === 0">
            <td colspan="8" class="empty">暂无数据，等待后端推送…</td>
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
  gap: 8px;
  padding: 8px 14px;
  background: #12151b;
  border-bottom: 1px solid #232833;
  color: #8b93a1;
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

.sep {
  color: #2d3542;
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

.usage-col {
  min-width: 240px;
  max-width: 380px;
}

.chk {
  margin-left: 10px;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  color: #b7bec9;
  cursor: pointer;
}

.search {
  background: #0f1115;
  border: 1px solid #2d3542;
  border-radius: 4px;
  color: #d7dae0;
  padding: 3px 8px;
  font-family: inherit;
  font-size: 12px;
  width: 160px;
}

.search:focus {
  outline: none;
  border-color: #4ea1ff;
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

th.clickable {
  cursor: pointer;
  user-select: none;
}

th.clickable:hover {
  color: #d7dae0;
}

td {
  padding: 5px 12px;
  border-bottom: 1px solid #191d24;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 380px;
}

tbody tr:hover {
  background: #171b22;
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
  font-weight: 600;
}

.tag {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 3px;
}

.tag.bg {
  background: #1d2a3a;
  color: #6cb6ff;
}

.tag.sys {
  background: #2b2130;
  color: #c792ea;
}

.empty {
  text-align: center;
  color: #5c6572;
  padding: 30px;
}
</style>
