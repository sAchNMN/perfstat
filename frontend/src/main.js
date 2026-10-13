import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'
import { initToken } from './api/client'

// 先把地址栏的 ?t=<token> 收进 sessionStorage，再挂载应用
initToken()

const app = createApp(App)

app.use(createPinia())
app.use(router)

app.mount('#app')
