import { createRouter, createWebHistory } from 'vue-router'

import ProcessesView from '@/views/ProcessesView.vue'
import ServicesView from '@/views/ServicesView.vue'
import StartupView from '@/views/StartupView.vue'
import HistoryView from '@/views/HistoryView.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    { path: '/', redirect: '/processes' },
    { path: '/processes', name: 'processes', component: ProcessesView },
    { path: '/services', name: 'services', component: ServicesView },
    { path: '/startup', name: 'startup', component: StartupView },
    { path: '/history', name: 'history', component: HistoryView },
  ],
})

export default router
