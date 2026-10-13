<script setup>
import { RouterLink, RouterView } from 'vue-router'
</script>

<template>
  <div class="app">
    <header class="topbar">
      <span class="brand">PerfStat</span>
      <nav>
        <RouterLink to="/processes">进程</RouterLink>
        <RouterLink to="/services">服务</RouterLink>
      </nav>
    </header>
    <main class="content">
      <RouterView />
    </main>
  </div>
</template>

<style>
:root {
  color-scheme: dark;
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  background: #0f1115;
  color: #d7dae0;
  font-family: ui-monospace, Consolas, 'Cascadia Mono', monospace;
  font-size: 13px;
}

.app {
  display: flex;
  flex-direction: column;
  height: 100vh;
}

.topbar {
  display: flex;
  align-items: center;
  gap: 22px;
  padding: 10px 16px;
  background: #161a21;
  border-bottom: 1px solid #232833;
}

.brand {
  color: #4ea1ff;
  font-weight: 600;
  letter-spacing: 1px;
}

nav a {
  margin-right: 14px;
  color: #8b93a1;
  text-decoration: none;
}

nav a.router-link-active {
  color: #e6e9ef;
}

.content {
  flex: 1;
  min-height: 0;
  overflow: hidden;
}
</style>
