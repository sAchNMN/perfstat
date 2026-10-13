<script setup>
import { computed, ref } from 'vue'

import { setUsage } from '@/api/client'

// 用途单元格：展示类别与用途，点击后打开编辑弹层写人工覆盖（source = manual）
// 保存成功后不需要手动刷新 —— 进程走 SSE、服务走 2s 轮询，下一帧即带新值
const props = defineProps({
  kind: { type: String, required: true }, // process | service
  name: { type: String, required: true },
  path: { type: String, default: '' },
  usage: { type: Object, default: () => ({}) },
})

const open = ref(false)
const saving = ref(false)
const error = ref('')
const form = ref({ category: '', purpose: '', suggestion: '' })

const sourceLabel = computed(
  () =>
    ({
      builtin: '内置规则',
      manual: '人工覆盖',
      none: '未识别',
    })[props.usage.source] || '未识别',
)

function beginEdit() {
  form.value = {
    category: props.usage.category || '',
    purpose: props.usage.purpose || '',
    suggestion: props.usage.suggestion || '',
  }
  error.value = ''
  open.value = true
}

async function save() {
  saving.value = true
  error.value = ''
  try {
    await setUsage({
      kind: props.kind,
      name: props.name,
      path: props.path,
      category: form.value.category,
      purpose: form.value.purpose,
      suggestion: form.value.suggestion,
    })
    open.value = false
  } catch (e) {
    error.value = String(e)
  } finally {
    saving.value = false
  }
}
</script>

<template>
  <div class="usage-cell">
    <button
      type="button"
      class="cell"
      :title="usage.suggestion || usage.purpose || '点击填写用途'"
      @click="beginEdit"
    >
      <span class="cat" :class="'src-' + (usage.source || 'none')">{{
        usage.category || '其他'
      }}</span>
      <span class="purpose" :class="{ unknown: !usage.known }">{{
        usage.purpose || '未知'
      }}</span>
    </button>

    <div v-if="open" class="mask" @click.self="open = false">
      <div class="dialog">
        <div class="head">
          <span class="title">编辑用途</span>
          <span class="target">{{ name }}</span>
        </div>
        <div class="path">{{ path || '（服务无路径，按服务名匹配）' }}</div>

        <label class="field">
          <span>类别</span>
          <input v-model="form.category" placeholder="如 系统服务 / 云同步 / 开发工具" />
        </label>
        <label class="field">
          <span>用途</span>
          <input v-model="form.purpose" placeholder="一句话说明它做什么" />
        </label>
        <label class="field">
          <span>清理建议</span>
          <textarea v-model="form.suggestion" rows="3" placeholder="能否关闭 / 怎么关" ></textarea>
        </label>

        <div class="foot">
          <span class="src">当前来源：{{ sourceLabel }}</span>
          <span class="spacer"></span>
          <span v-if="error" class="err">{{ error }}</span>
          <button type="button" class="btn" @click="open = false">取消</button>
          <button type="button" class="btn primary" :disabled="saving" @click="save">
            {{ saving ? '保存中…' : '保存为人工覆盖' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.usage-cell {
  display: inline-block;
  max-width: 100%;
}

.cell {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  max-width: 100%;
  background: none;
  border: none;
  padding: 0;
  font: inherit;
  color: inherit;
  cursor: pointer;
  text-align: left;
}

.cell:hover .purpose {
  color: #4ea1ff;
}

.cat {
  flex: none;
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 3px;
  background: #1d2a3a;
  color: #6cb6ff;
}

.cat.src-manual {
  background: #16271d;
  color: #3ddc84;
}

.cat.src-none {
  background: #232833;
  color: #6b7482;
}

.purpose {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #9aa3b2;
}

.purpose.unknown {
  color: #5c6572;
}

.mask {
  position: fixed;
  inset: 0;
  background: rgba(8, 10, 14, 0.72);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 50;
}

.dialog {
  width: 460px;
  background: #171b22;
  border: 1px solid #2d3542;
  border-radius: 6px;
  padding: 16px 18px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.head {
  display: flex;
  align-items: baseline;
  gap: 10px;
}

.title {
  color: #e6e9ef;
  font-weight: 600;
}

.target {
  color: #6cb6ff;
  font-size: 12px;
}

.path {
  color: #5c6572;
  font-size: 11px;
  word-break: break-all;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
  color: #8b93a1;
  font-size: 12px;
}

.field input,
.field textarea {
  background: #0f1115;
  border: 1px solid #2d3542;
  border-radius: 4px;
  color: #d7dae0;
  padding: 5px 8px;
  font-family: inherit;
  font-size: 12px;
  resize: vertical;
}

.field input:focus,
.field textarea:focus {
  outline: none;
  border-color: #4ea1ff;
}

.foot {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 2px;
}

.src {
  color: #5c6572;
  font-size: 11px;
}

.spacer {
  flex: 1;
}

.err {
  color: #e5534b;
  font-size: 11px;
}

.btn {
  background: #232833;
  border: 1px solid #2d3542;
  border-radius: 4px;
  color: #d7dae0;
  padding: 4px 12px;
  font-family: inherit;
  font-size: 12px;
  cursor: pointer;
}

.btn:hover {
  border-color: #4ea1ff;
}

.btn.primary {
  background: #1d2a3a;
  border-color: #2f4a6b;
  color: #9ecbff;
}

.btn:disabled {
  opacity: 0.6;
  cursor: default;
}
</style>
