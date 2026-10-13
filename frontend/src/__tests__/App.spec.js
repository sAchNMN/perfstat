import { describe, it, expect } from 'vitest'

import { mount } from '@vue/test-utils'
import { createRouter, createWebHistory } from 'vue-router'

import App from '../App.vue'

describe('App', () => {
  it('渲染顶部品牌与导航', async () => {
    const router = createRouter({ history: createWebHistory(), routes: [] })
    const wrapper = mount(App, { global: { plugins: [router] } })
    await router.isReady()

    expect(wrapper.find('.brand').text()).toBe('PerfStat')
    expect(wrapper.text()).toContain('进程')
  })
})
