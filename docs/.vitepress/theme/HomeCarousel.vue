<!--
  首页界面轮播（用户令：dashboard 真实截图 + 手机原型图放文档首页 hero 之下，
  对齐 croupier HomeCarousel 标准）。当前素材：5 张 admin_dashboard 真实运行
  截图（3200×1800，16:9 统一；vite dev 真跑 + Playwright 收敛连拍，图表为
  应用内置演示序列，指标卡 0 属实——/api/stats 无后端时静默保持 0）。
  手机原型图仓库无图片资产（docs/design-notes/app_chat_prototype.md 只有
  ASCII 线框，严禁生成代餐）——素材到位后在 slides 数组追加即可（竖屏图
  在 16/9 框内 object-fit: contain 居中留白）。
  挂载点：theme/index.ts 经 home-features-before 插槽注入（hero 之下、
  特性之上；该插槽只在 layout: home 页生效，仓库仅 docs/index.md 一页 home）。
-->
<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { useData } from 'vitepress';

import shot01 from '../../screenshots/ui/01-dashboard.png';
import shot02 from '../../screenshots/ui/02-users.png';
import shot03 from '../../screenshots/ui/03-channels.png';
import shot04 from '../../screenshots/ui/04-messages.png';
import shot05 from '../../screenshots/ui/05-settings.png';

interface Slide {
  src: string;
  caption: string;
}

const slides: Slide[] = [
  { src: shot01, caption: '管理台 · 总览' },
  { src: shot02, caption: '管理台 · 用户管理' },
  { src: shot03, caption: '管理台 · 频道管理' },
  { src: shot04, caption: '管理台 · 消息治理' },
  { src: shot05, caption: '管理台 · 运行设置' },
];

const AUTOPLAY_MS = 4500;

const index = ref(0);
const total = slides.length;
const paused = ref(false);
const current = computed(() => slides[index.value]);

let timer: ReturnType<typeof setInterval> | null = null;

function stopTimer(): void {
  if (timer !== null) {
    clearInterval(timer);
    timer = null;
  }
}

function go(next: number): void {
  index.value = ((next % total) + total) % total;
}

onMounted(() => {
  timer = setInterval(() => {
    if (!paused.value) go(index.value + 1);
  }, AUTOPLAY_MS);
});

onBeforeUnmount(stopTimer);

const { site } = useData();
const detailLink = `${site.value.base}design-notes/app_chat_prototype`;
</script>

<template>
  <section
    class="home-carousel"
    aria-roledescription="轮播"
    aria-label="界面预览"
    @mouseenter="paused = true"
    @mouseleave="paused = false"
    @focusin="paused = true"
    @focusout="paused = false"
    @keydown.left="go(index - 1)"
    @keydown.right="go(index + 1)"
  >
    <header class="hc-head">
      <h2 class="hc-title">界面预览</h2>
      <a class="hc-more" :href="detailLink">应用原型文档 →</a>
    </header>

    <div class="hc-frame">
      <Transition name="hc-fade" mode="out-in">
        <img
          :key="current.src"
          class="hc-img"
          :src="current.src"
          :alt="`chirp 管理台：${current.caption}`"
          loading="lazy"
          decoding="async"
        />
      </Transition>

      <button
        type="button"
        class="hc-nav hc-prev"
        :aria-label="`上一张：${slides[(index - 1 + total) % total].caption}`"
        @click="go(index - 1)"
      >
        <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M14.5 5.5 8 12l6.5 6.5" />
        </svg>
      </button>
      <button
        type="button"
        class="hc-nav hc-next"
        :aria-label="`下一张：${slides[(index + 1) % total].caption}`"
        @click="go(index + 1)"
      >
        <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M9.5 5.5 16 12l-6.5 6.5" />
        </svg>
      </button>

      <span class="hc-caption">{{ current.caption }}</span>
    </div>

    <div class="hc-dots" role="tablist" :aria-label="`共 ${total} 张`">
      <button
        v-for="(slide, i) in slides"
        :key="slide.src"
        type="button"
        role="tab"
        :aria-selected="i === index"
        :aria-label="`第 ${i + 1} 张：${slide.caption}`"
        :class="['hc-dot', { active: i === index }]"
        @click="go(i)"
      />
    </div>
  </section>
</template>

<style scoped>
.home-carousel {
  max-width: 1152px;
  margin: 0 auto;
  padding: 0 24px 8px;
}

.hc-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  margin-bottom: 12px;
}

.hc-title {
  margin: 0;
  padding: 0;
  border: none;
  font-size: 20px;
  font-weight: 600;
  line-height: 1.4;
  color: var(--vp-c-text-1);
  letter-spacing: -0.02em;
}

.hc-more {
  font-size: 14px;
  font-weight: 500;
  color: var(--vp-c-brand-1);
  text-decoration: none;
}

.hc-more:hover {
  text-decoration: underline;
}

/* 亮暗自适应：框线/底色/文字全走 VitePress CSS 变量，随 .dark 切换。 */
.hc-frame {
  position: relative;
  aspect-ratio: 16 / 9;
  overflow: hidden;
  border-radius: 12px;
  border: 1px solid var(--vp-c-divider);
  background-color: var(--vp-c-bg-soft);
  box-shadow: var(--vp-shadow-1);
  outline: none;
}

.hc-img {
  display: block;
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.hc-nav {
  position: absolute;
  top: 50%;
  transform: translateY(-50%);
  display: flex;
  align-items: center;
  justify-content: center;
  width: 40px;
  height: 40px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 50%;
  background-color: var(--vp-button-alt-bg);
  color: var(--vp-c-text-1);
  cursor: pointer;
  opacity: 0;
  transition:
    opacity 0.2s ease,
    border-color 0.2s ease,
    color 0.2s ease;
}

.hc-prev {
  left: 12px;
}

.hc-next {
  right: 12px;
}

.hc-frame:hover .hc-nav,
.hc-nav:focus-visible {
  opacity: 1;
}

/* 触屏无 hover：箭头常显（coarse 指针媒体查询） */
@media (pointer: coarse) {
  .hc-nav {
    opacity: 0.85;
  }
}

.hc-nav:hover {
  color: var(--vp-c-brand-1);
  border-color: var(--vp-c-brand-1);
}

.hc-nav:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}

.hc-caption {
  position: absolute;
  left: 50%;
  bottom: 10px;
  transform: translateX(-50%);
  max-width: 80%;
  padding: 2px 12px;
  border-radius: 999px;
  font-size: 12.5px;
  line-height: 1.7;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  color: #fff;
  background-color: rgba(0, 0, 0, 0.55);
  backdrop-filter: blur(4px);
}

.hc-dots {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  margin-top: 10px;
}

.hc-dot {
  width: 8px;
  height: 8px;
  padding: 0;
  border: none;
  border-radius: 999px;
  background-color: var(--vp-c-text-3);
  opacity: 0.55;
  cursor: pointer;
  transition:
    width 0.2s ease,
    background-color 0.2s ease,
    opacity 0.2s ease;
}

.hc-dot:hover {
  opacity: 0.9;
}

.hc-dot.active {
  width: 20px;
  background-color: var(--vp-c-brand-1);
  opacity: 1;
}

.hc-dot:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}

/* crossfade；reduced-motion 下免动画直接切换 */
.hc-fade-enter-active,
.hc-fade-leave-active {
  transition: opacity 0.25s ease;
}

.hc-fade-enter-from,
.hc-fade-leave-to {
  opacity: 0;
}

@media (prefers-reduced-motion: reduce) {
  .hc-fade-enter-active,
  .hc-fade-leave-active {
    transition: none;
  }
}

@media (max-width: 768px) {
  .home-carousel {
    padding: 0 16px 4px;
  }

  .hc-nav {
    width: 32px;
    height: 32px;
  }
}
</style>
