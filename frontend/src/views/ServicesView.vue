<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'

import { getServices } from '@/api/client'

// 服务列表由后端 /api/services 提供，前端每 2s 轮询一次（服务变化慢，无需 SSE）
const services = ref([])
const round = ref(0)
const connected = ref(false)
const lastError = ref('')

// 视图状态
const keyword = ref('')
const sortKey = ref('name')
const sortDesc = ref(true)
const hideStopped = ref(false)

let timer = null

async function refresh() {
  try {
    const list = await getServices()
    services.value = list
    round.value += 1
    connected.value = true
    lastError.value = ''
  } catch (e) {
    connected.value = false
    lastError.value = String(e)
  }
}

onMounted(() => {
  refresh()
  timer = setInterval(refresh, 2000)
})

onBeforeUnmount(() => {
  if (timer) clearInterval(timer)
})

const rows = computed(() => {
  let list = services.value
  if (hideStopped.value) {
    list = list.filter((s) => s.state === 'running')
  }
  const kw = keyword.value.trim().toLowerCase()
  if (kw) {
    list = list.filter(
      (s) => s.name.toLowerCase().includes(kw) || s.display.toLowerCase().includes(kw),
    )
  }
  const key = sortKey.value
  return [...list].sort((a, b) => {
    const x = a[key]
    const y = b[key]
    const result = typeof x === 'string' ? x.localeCompare(y) : x - y
    return sortDesc.value ? -result : result
  })
})

const occSummary = computed(() => {
  let ok = 0
  let denied = 0
  let na = 0
  for (const s of services.value) {
    if (s.occ === 'ok') ok++
    else if (s.occ === 'denied') denied++
    else na++
  }
  return { ok, denied, na, total: services.value.length }
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

function stateLabel(s) {
  const map = {
    running: '运行中',
    stopped: '已停止',
    start_pending: '启动中',
    stop_pending: '停止中',
    pause_pending: '暂停中',
    paused: '已暂停',
    unknown: '未知',
  }
  return map[s] || s
}

function startLabel(s) {
  const map = {
    auto: '自动',
    manual: '手动',
    disabled: '禁用',
    delayed: '自动(延迟)',
    unknown: '未知',
  }
  return map[s] || s
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
      <span>共 {{ occSummary.total }} 个</span>
      <span class="sep">|</span>
      <span class="occ ok">有占用 {{ occSummary.ok }}</span>
      <span class="occ denied">无权限 {{ occSummary.denied }}</span>
      <span class="occ na">不适用 {{ occSummary.na }}</span>
      <label class="chk">
        <input v-model="hideStopped" type="checkbox" />
        仅运行中
      </label>
      <input v-model="keyword" class="search" placeholder="过滤名称/显示名" />
      <span class="spacer"></span>
      <span class="hint" v-if="lastError">{{ lastError }}</span>
      <span class="hint" v-else>每 2s 刷新</span>
    </div>

    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th class="clickable" @click="setSort('name')">服务名{{ arrow('name') }}</th>
            <th>显示名</th>
            <th class="clickable" @click="setSort('state')">状态{{ arrow('state') }}</th>
            <th class="clickable" @click="setSort('start_type')">启动类型{{ arrow('start_type') }}</th>
            <th class="num clickable" @click="setSort('cpu')">CPU %{{ arrow('cpu') }}</th>
            <th class="num clickable" @click="setSort('mem_kb')">内存{{ arrow('mem_kb') }}</th>
            <th>共享</th>
            <th class="num clickable" @click="setSort('host_pid')">宿主 PID{{ arrow('host_pid') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="s in rows" :key="s.name">
            <td :title="s.name">{{ s.name }}</td>
            <td class="dim" :title="s.display">{{ s.display }}</td>
            <td>
              <span class="tag" :class="s.state === 'running' ? 'run' : 'stop'">
                {{ stateLabel(s.state) }}
              </span>
            </td>
            <td>{{ startLabel(s.start_type) }}</td>
            <td class="num" :class="{ hot: s.occ === 'ok' && s.cpu >= 5 }">
              <span v-if="s.occ === 'ok'">{{ s.cpu.toFixed(2) }}</span>
              <span v-else-if="s.occ === 'denied'" class="denied" title="需管理员权限才能读取该宿主进程占用">—</span>
              <span v-else class="dim">—</span>
            </td>
            <td class="num">
              <span v-if="s.occ === 'ok'">{{ formatKb(s.mem_kb) }}</span>
              <span v-else-if="s.occ === 'denied'" class="denied" title="需管理员权限才能读取该宿主进程占用">—</span>
              <span v-else class="dim">—</span>
            </td>
            <td>
              <span v-if="s.shared > 1" class="tag share">N={{ s.shared }} 共享</span>
              <span v-else class="dim">—</span>
            </td>
            <td class="num dim">{{ s.host_pid || '—' }}</td>
          </tr>
          <tr v-if="rows.length === 0">
            <td colspan="8" class="empty">暂无数据，等待后端响应…</td>
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

.sep {
  color: #2d3542;
}

.occ {
  font-size: 12px;
}

.occ.ok {
  color: #3ddc84;
}

.occ.denied {
  color: #e5a14b;
}

.occ.na {
  color: #6b7482;
}

.spacer {
  flex: 1;
}

.hint {
  color: #5c6572;
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
  width: 180px;
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
  max-width: 360px;
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

.denied {
  color: #e5a14b;
  cursor: help;
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

.tag.run {
  background: #16271d;
  color: #3ddc84;
}

.tag.stop {
  background: #2b2130;
  color: #c792ea;
}

.tag.share {
  background: #1d2a3a;
  color: #6cb6ff;
}

.empty {
  text-align: center;
  color: #5c6572;
  padding: 30px;
}
</style>
