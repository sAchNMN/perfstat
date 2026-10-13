<script setup>
import { computed, onMounted, ref } from 'vue'

import { getStartup } from '@/api/client'

// 启动项清单变化很慢，后端每约 30s 重读注册表；本页手动刷新即可
const items = ref([])
const loading = ref(false)
const error = ref('')

const keyword = ref('')
const onlyEnabled = ref(false)
const sortKey = ref('cpu')
const sortDesc = ref(true)

async function load() {
  loading.value = true
  error.value = ''
  try {
    items.value = await getStartup()
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
}

onMounted(load)

const rows = computed(() => {
  let list = items.value
  if (onlyEnabled.value) {
    list = list.filter((x) => x.enabled)
  }
  const kw = keyword.value.trim().toLowerCase()
  if (kw) {
    list = list.filter(
      (x) => x.name.toLowerCase().includes(kw) || x.command.toLowerCase().includes(kw),
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

const stats = computed(() => {
  const total = items.value.length
  const enabled = items.value.filter((x) => x.enabled).length
  const running = items.value.filter((x) => x.running).length
  return { total, enabled, disabled: total - enabled, running }
})

function formatKb(kb) {
  if (kb >= 1024 * 1024) return (kb / 1024 / 1024).toFixed(2) + ' GB'
  if (kb >= 1024) return (kb / 1024).toFixed(1) + ' MB'
  return kb + ' KB'
}
</script>

<template>
  <div class="wrap">
    <div class="toolbar">
      <span class="dot" :class="error ? 'off' : 'on'"></span>
      <span :class="{ err: !!error }">{{
        error || `共 ${stats.total} 项 | 启用 ${stats.enabled} / 禁用 ${stats.disabled}`
      }}</span>
      <span class="sep">|</span>
      <span>当前在跑 {{ stats.running }}</span>
      <label class="chk">
        <input v-model="onlyEnabled" type="checkbox" />
        仅看启用
      </label>
      <input v-model="keyword" class="search" placeholder="过滤名称或命令" />
      <button type="button" class="act" :disabled="loading" @click="load">
        {{ loading ? '读取中…' : '刷新' }}
      </button>
      <span class="spacer"></span>
      <span class="hint">来源：注册表 Run + 启动文件夹 | 占用三态：数值 / 无权限（提权进程）/ —</span>
    </div>

    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th class="clickable" @click="setSort('name')">名称{{ arrow('name') }}</th>
            <th>命令 / 目标</th>
            <th class="clickable" @click="setSort('source')">来源{{ arrow('source') }}</th>
            <th>范围</th>
            <th>状态</th>
            <th class="num clickable" @click="setSort('cpu')">CPU %{{ arrow('cpu') }}</th>
            <th class="num clickable" @click="setSort('mem_kb')">内存{{ arrow('mem_kb') }}</th>
            <th class="num">PID</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(it, i) in rows"
            :key="it.source + '-' + it.name + '-' + i"
            :class="{ off: !it.enabled, idle: it.enabled && !it.running }"
          >
            <td :title="it.name">{{ it.name }}</td>
            <td class="cmd" :title="it.command">{{ it.command || '（未取到命令）' }}</td>
            <td class="dim">{{ it.source }}</td>
            <td class="dim">{{ it.scope === 'user' ? '当前用户' : '所有用户' }}</td>
            <td>
              <span v-if="!it.enabled && it.running" class="tag warn">已禁用 · 却在跑</span>
              <span v-else-if="!it.enabled" class="tag dis">已禁用</span>
              <span v-else-if="it.running" class="tag run">已启用 · 在跑</span>
              <span v-else class="tag on">已启用</span>
            </td>
            <td class="num" :class="{ hot: it.occ === 'ok' && it.cpu >= 5 }">
              <span v-if="it.occ === 'ok'">{{ it.cpu.toFixed(2) }}</span>
              <span v-else-if="it.occ === 'denied'" class="denied" title="进程在跑，但句柄被拒，需提权才能读到占用">无权限</span>
              <span v-else>—</span>
            </td>
            <td class="num">{{ it.occ === 'ok' ? formatKb(it.mem_kb) : '—' }}</td>
            <td class="num dim">{{ it.running ? it.pid : '—' }}</td>
          </tr>
          <tr v-if="rows.length === 0">
            <td colspan="8" class="empty">
              {{ loading ? '读取中…' : '没有匹配的启动项' }}
            </td>
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

.spacer {
  flex: 1;
}

.hint {
  color: #5c6572;
}

.chk {
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
  width: 200px;
}

.search:focus {
  outline: none;
  border-color: #4ea1ff;
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
  max-width: 320px;
}

tbody tr:hover {
  background: #171b22;
}

/* 已禁用：整体压暗；已启用但没在跑：名称稍淡，突出「在跑」的行 */
tbody tr.off td {
  color: #5c6572;
}

tbody tr.idle td:first-child {
  color: #8b93a1;
}

.cmd {
  color: #6b7482;
  font-family: Consolas, 'Courier New', monospace;
  font-size: 12px;
  max-width: 460px;
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

.tag.run {
  background: #16271d;
  color: #3ddc84;
}

.tag.on {
  background: #1d2a3a;
  color: #6cb6ff;
}

.tag.dis {
  background: #232833;
  color: #6b7482;
}

/* 已禁用却检测到进程在跑：值得注意，用琥珀色标出 */
.tag.warn {
  background: #2b2113;
  color: #ffb454;
}

.denied {
  color: #8b93a1;
  font-size: 11px;
}

.empty {
  text-align: center;
  color: #5c6572;
  padding: 30px;
}
</style>
