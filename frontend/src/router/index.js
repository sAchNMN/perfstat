import { createRouter, createWebHistory } from 'vue-router'

import ProcessesView from '@/views/ProcessesView.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    { path: '/', redirect: '/processes' },
    { path: '/processes', name: 'processes', component: ProcessesView },
  ],
})

export default router
