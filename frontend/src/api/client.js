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
