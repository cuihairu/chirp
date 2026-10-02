// chirp docs theme — 默认主题 + 品牌配色（custom.css 见同目录）
// VitePress 会自动发现本文件，无需在 config.mts 里引用。
import { h } from 'vue';
import DefaultTheme from 'vitepress/theme';
import HomeCarousel from './HomeCarousel.vue';
import './custom.css';

// 首页界面轮播（用户令）：hero 之下、特性之上。home-features-before 插槽
// 只在 layout: home 页生效（仓库仅 docs/index.md 一页 home），其余页面零影响。
export default {
  ...DefaultTheme,
  Layout: () =>
    h(DefaultTheme.Layout, null, {
      'home-features-before': () => h(HomeCarousel),
    }),
};
