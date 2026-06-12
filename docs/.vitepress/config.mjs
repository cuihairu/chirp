import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'

const config = defineConfig({
  lang: 'zh-CN',
  title: 'Chirp',
  description: '面向游戏开发的轻量聊天后端骨架',
  ignoreDeadLinks: true,
  head: [
    ['meta', { name: 'viewport', content: 'width=device-width,initial-scale=1' }],
    ['meta', { name: 'keywords', content: 'chirp,游戏聊天,实时通信,gRPC,TCP,WebSocket' }],
    ['meta', { name: 'theme-color', content: '#3eaf7c' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:site', content: '@chirp_project' }],
  ],
  base: '/chirp/',

  themeConfig: {
    logo: '/logo.png',

    nav: [
      { text: '核心说明', link: '/CORE' },
      { text: '快速开始', link: '/guide/getting-started' },
      { text: '架构', link: '/architecture' },
      { text: 'API', link: '/api/overview' },
      {
        text: 'GitHub',
        link: 'https://github.com/cuihairu/chirp'
      },
    ],

    sidebar: {
      '/api/': [
        { text: 'API 参考', collapsed: false, items: [
          { text: '概述', link: '/api/overview' },
        ]},
      ],
      '/': [
        { text: '核心文档', collapsed: false, items: [
          { text: '首页', link: '/' },
          { text: '核心说明', link: '/CORE' },
          { text: '能力矩阵', link: '/CAPABILITY_MATRIX' },
          { text: '快速开始', link: '/guide/getting-started' },
          { text: '整体架构', link: '/architecture' },
          { text: 'API 参考', link: '/api/overview' },
        ]},
        { text: '专题/历史文档', collapsed: true, items: [
          { text: '部署说明', link: '/DEPLOYMENT' },
          { text: '分布式部署', link: '/DISTRIBUTED_DEPLOYMENT' },
          { text: '扩展性设计', link: '/SCALABILITY' },
          { text: '游戏聊天设计', link: '/game_chat_architecture' },
          { text: 'NPC 对话系统', link: '/npc_dialog_system' },
        ]},
      ],
    },

    editLink: {
      pattern: 'https://github.com/cuihairu/chirp/edit/main/docs/:path',
      text: '在 GitHub 上编辑此页'
    },

    lastUpdated: { text: '最后更新' },

    social: [
      { icon: 'github', link: 'https://github.com/cuihairu/chirp' }
    ],

    search: { provider: 'local' },

    docFooter: {
      prev: '上一页',
      next: '下一页'
    }
  },

  vite: {
    build: {
      chunkSizeWarningLimit: 1200,
    },
  },
})

export default withMermaid(config)
