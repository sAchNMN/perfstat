// 后端接口客户端：token 管理 + 快照请求 + SSE 订阅
// 说明：token 由后端启动 URL 的 ?t= 带入，落到 sessionStorage，之后所有请求复用

const TOKEN_KEY = 'perfstat_token'

// 从地址栏取 token 并清理 URL；返回当前生效的 token
export function initToken() {
  const url = new URL(window.location.href)
  const fromUrl = url.searchParams.get('t')
  if (fromUrl) {
    sessionStorage.setItem(TOKEN_KEY, fromUrl)
    url.searchParams.delete('t')
    const query = url.searchParams.toString()
    window.history.replaceState({}, '', url.pathname + (query ? '?' + query : '') + url.hash)
  }
  return getToken()
}

export function getToken() {
  return sessionStorage.getItem(TOKEN_KEY) || ''
}

// GET /api/*，带 X-Token
export async function apiGet(path) {
  const res = await fetch(path, { headers: { 'X-Token': getToken() } })
  if (!res.ok) {
    throw new Error(`${res.status} ${res.statusText}`)
  }
  return res.json()
}

// POST /api/*，带 X-Token 与 JSON body
export async function apiPost(path, payload) {
  const res = await fetch(path, {
    method: 'POST',
    headers: { 'X-Token': getToken(), 'Content-Type': 'application/json' },
    body: JSON.stringify(payload ?? {}),
  })
  if (!res.ok) {
    throw new Error(`${res.status} ${res.statusText}`)
  }
  return res.json()
}

// 订阅 SSE；EventSource 无法自定义 header，token 走 query
export function openRealtime({ onFrame, onOpen, onError }) {
  const token = getToken()
  const source = new EventSource(`/api/realtime?t=${encodeURIComponent(token)}`)

  source.onopen = () => onOpen && onOpen()
  source.onerror = () => onError && onError()
  source.onmessage = (event) => {
    try {
      onFrame(JSON.parse(event.data))
    } catch {
      // 单帧解析失败不影响后续
    }
  }

  return source
}

// GET /api/services：服务列表（含 D18 三态占用）
export async function getServices() {
  return apiGet('/api/services')
}

// GET /api/startup：开机自启项（注册表 Run + 启动文件夹），占用为当前进程快照现值
export async function getStartup() {
  return apiGet('/api/startup')
}

// POST /api/usage：写入人工覆盖（source = manual），下一帧快照即生效
// entry: { kind, name, path, category, purpose, suggestion }
export async function setUsage(entry) {
  return apiPost('/api/usage', entry)
}

// GET /api/usage：列出全部人工覆盖条目
export async function getUsageOverrides() {
  return apiGet('/api/usage')
}

// POST /api/analyze：对当前快照做一次本地识别，返回命中统计与未命中清单
export async function analyzeUsage() {
  return apiPost('/api/analyze', {})
}

// GET /api/stats：时间段统计
// 不给 entityId 时返回汇总榜 + 全系统曲线；给出时返回该实体的时间序列
export async function getStats({ from, to, bucket = 60, top = 50, entityId = 0 } = {}) {
  const query = new URLSearchParams()
  if (from) query.set('from', from)
  if (to) query.set('to', to)
  query.set('bucket', bucket)
  query.set('top', top)
  if (entityId) query.set('entity_id', entityId)
  return apiGet('/api/stats?' + query.toString())
}
