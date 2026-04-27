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
      { text: '指南', link: '/guide/' },
      { text: '架构', link: '/game_chat_architecture' },
      { text: 'API 参考', link: '/api/' },
      { text: 'NPC 系统', link: '/npc_dialog_system' },
      {
        text: 'GitHub',
        link: 'https://github.com/cuihairu/chirp'
      },
    ],

    sidebar: {
      '/guide/': [
        { text: '入门指南', collapsed: false, items: [
          { text: '简介', link: '/guide/introduction' },
          { text: '快速开始', link: '/guide/getting-started' },
          { text: '安装', link: '/guide/installation' },
          { text: '部署', link: '/guide/deployment' },
        ]},
      ],
      '/api/': [
        { text: 'API 参考', collapsed: false, items: [
          { text: '概述', link: '/api/overview' },
        ]},
      ],
      '/': [
        { text: '概览', collapsed: false, items: [
          { text: '首页', link: '/' },
          { text: '快速开始', link: '/QUICKSTART' },
          { text: '指南', link: '/guide/' },
          { text: '架构文档', link: '/game_chat_architecture' },
          { text: '功能特性', link: '/game_chat_features' },
          { text: 'NPC 对话系统', link: '/npc_dialog_system' },
          { text: 'API 参考', link: '/api/' },
          { text: '部署', link: '/DEPLOYMENT' },
          { text: '分布式部署', link: '/DISTRIBUTED_DEPLOYMENT' },
          { text: '扩展性', link: '/SCALABILITY' },
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
