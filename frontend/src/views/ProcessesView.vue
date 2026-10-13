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

// 表格 / 进程树切换
const view = ref('table')
const collapsed = ref(new Set())

function toggleCollapse(pid) {
  const next = new Set(collapsed.value)
  if (next.has(pid)) {
    next.delete(pid)
  } else {
    next.add(pid)
  }
  collapsed.value = next
}

function toggleAll(hasAnyOpen) {
  if (hasAnyOpen) {
    const all = new Set()
    for (const p of processes.value) {
      if (p.ok) all.add(p.pid)
    }
    collapsed.value = all
  } else {
    collapsed.value = new Set()
  }
}

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

/*
 * 进程树：按 pid / ppid 构树后展平成可见行
 * 说明：
 *   - 父进程不在当前（已过滤）集合内的，视为根
 *   - 折叠的子树不产出行，但仍标记为已访问，避免被兜底逻辑当成根重复挂出
 *   - 环（父子互相指向）由 visited + 深度上限兜住，不递归爆栈
 */
const treeRows = computed(() => {
  const list = rows.value
  const byId = new Map()
  for (const p of list) byId.set(p.pid, p)

  const children = new Map()
  const roots = []
  for (const p of list) {
    const hasParent = p.ppid && p.ppid !== p.pid && byId.has(p.ppid)
    if (hasParent) {
      const bucket = children.get(p.ppid)
      if (bucket) {
        bucket.push(p)
      } else {
        children.set(p.ppid, [p])
      }
    } else {
      roots.push(p)
    }
  }

  const out = []
  const visited = new Set()
  const walk = (p, depth, visible, guard) => {
    if (visited.has(p.pid) || guard > 64) return
    visited.add(p.pid)
    const kids = children.get(p.pid) || []
    if (visible) {
      out.push({
        p,
        depth,
        hasChildren: kids.length > 0,
        open: !collapsed.value.has(p.pid),
      })
    }
    const kidsVisible = visible && !collapsed.value.has(p.pid)
    for (const k of kids) walk(k, depth + 1, kidsVisible, guard + 1)
  }

  for (const r of roots) walk(r, 0, true, 0)
  // 兜底：成环导致没有根可达的节点，也各挂成一根
  for (const p of list) walk(p, 0, true, 0)
  return out
})

// 表格模式：展平成深度 0 的行；树模式：带缩进与折叠态的行
const shown = computed(() => {
  if (view.value === 'tree') return treeRows.value
  return rows.value.map((p) => ({ p, depth: 0, hasChildren: false, open: true }))
})

// 树模式下是否还有展开着的节点（决定「全部展开/折叠」按钮文案）
const anyOpen = computed(() => treeRows.value.some((r) => r.hasChildren && r.open))


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

// 树模式下的层级缩进：每层 16px，超过 12 层不再加深，避免窄屏被挤出
function indentOf(depth) {
  return { paddingLeft: 12 + Math.min(depth, 12) * 16 + 'px' }
}

// 本地批量识别：内置规则 + 人工覆盖两级
const analyzing = ref(false)
const analyzeText = ref('')

async function runAnalyze() {
  analyzing.value = true
  try {
    const r = await analyzeUsage()
    analyzeText.value = `命中 内置 ${r.builtin} / 人工 ${r.manual}，未识别 ${r.unknown}（共 ${r.total}）`
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
      <span class="seg">
        <button
          type="button"
          :class="{ on: view === 'table' }"
          @click="view = 'table'"
        >
          表格
        </button>
        <button
          type="button"
          :class="{ on: view === 'tree' }"
          @click="view = 'tree'"
        >
          进程树
        </button>
      </span>
      <label class="chk">
        <input v-model="onlyBackground" type="checkbox" />
        仅后台
      </label>
      <input v-model="keyword" class="search" placeholder="过滤进程名" />
      <button
        v-if="view === 'tree'"
        type="button"
        class="act"
        @click="toggleAll(anyOpen)"
      >
        {{ anyOpen ? '全部折叠' : '全部展开' }}
      </button>
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
          <tr v-for="row in shown" :key="row.p.pid + '-' + row.p.name">
            <td class="dim">{{ row.p.pid }}</td>
            <td :title="row.p.path" :style="indentOf(row.depth)">
              <template v-if="view === 'tree'">
                <span
                  v-if="row.hasChildren"
                  class="caret"
                  :class="{ open: row.open }"
                  @click.stop="toggleCollapse(row.p.pid)"
                ></span>
                <span v-else class="caret-spacer"></span>
              </template>
              <span class="pname">{{ row.p.name }}</span>
            </td>
            <td class="usage-col">
              <UsageCell
                kind="process"
                :name="row.p.name"
                :path="row.p.path"
                :usage="row.p.usage"
              />
            </td>
            <td class="num" :class="{ hot: row.p.cpu >= 5 }">
              {{ row.p.cpu.toFixed(2) }}
            </td>
            <td class="num">{{ formatKb(row.p.mem_kb) }}</td>
            <td class="num dim">{{ row.p.io_r_kb }}</td>
            <td class="num dim">{{ row.p.io_w_kb }}</td>
            <td>
              <span v-if="row.p.sys" class="tag sys">系统</span>
              <span v-else-if="row.p.bg" class="tag bg">后台</span>
            </td>
          </tr>
          <tr v-if="shown.length === 0">
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

/* 表格 / 进程树分段切换 */
.seg {
  display: inline-flex;
  border: 1px solid #2d3542;
  border-radius: 4px;
  overflow: hidden;
}

.seg button {
  background: #171b22;
  border: none;
  color: #8b93a1;
  font-family: inherit;
  font-size: 12px;
  padding: 3px 10px;
  cursor: pointer;
}

.seg button.on {
  background: #1d2a3a;
  color: #6cb6ff;
}

/* 折叠三角：用 CSS 画，避免字形回退成豆腐块 */
.caret {
  display: inline-block;
  width: 14px;
  color: #6b7482;
  cursor: pointer;
  user-select: none;
}

.caret::before {
  content: '';
  display: inline-block;
  width: 0;
  height: 0;
  border-left: 5px solid currentColor;
  border-top: 4px solid transparent;
  border-bottom: 4px solid transparent;
  vertical-align: middle;
  transition: transform 0.12s ease;
}

.caret.open::before {
  transform: rotate(90deg);
}

.caret:hover {
  color: #6cb6ff;
}

.caret-spacer {
  display: inline-block;
  width: 14px;
}

.pname {
  color: #d7dae0;
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
