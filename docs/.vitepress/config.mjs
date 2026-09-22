import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'

const config = defineConfig({
  lang: 'zh-CN',
  title: 'Chirp',
  description: '面向游戏开发的轻量实时通信后端骨架',
  ignoreDeadLinks: true,
  head: [
    ['meta', { name: 'viewport', content: 'width=device-width,initial-scale=1' }],
    ['meta', { name: 'keywords', content: 'chirp,游戏聊天,实时通信,游戏后端,游戏服务器,C++,C++23,TCP,WebSocket,Protobuf,ASIO' }],
    ['meta', { name: 'theme-color', content: '#3eaf7c' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:site', content: '@chirp_project' }],
  ],
  base: '/chirp/',

  themeConfig: {
    logo: '/logo.svg',

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
      '/guide/': [
        { text: '入门指南', collapsed: false, items: [
          { text: '导言', link: '/guide/introduction' },
          { text: '安装', link: '/guide/installation' },
          { text: '快速开始', link: '/guide/getting-started' },
          { text: '部署指南', link: '/guide/deployment' },
        ]},
      ],
      '/api/': [
        { text: 'API 参考', collapsed: false, items: [
          { text: '概述', link: '/api/overview' },
          { text: 'Chat Peer 注册协议', link: '/api/peer_protocol' },
        ]},
      ],
      '/': [
        { text: '核心文档', collapsed: false, items: [
          { text: '首页', link: '/' },
          { text: '文档说明', link: '/README' },
          { text: '核心说明', link: '/CORE' },
          { text: '能力矩阵', link: '/CAPABILITY_MATRIX' },
          { text: '导言', link: '/guide/introduction' },
          { text: '安装', link: '/guide/installation' },
          { text: '快速开始', link: '/guide/getting-started' },
          { text: '整体架构', link: '/architecture' },
          { text: '服务器平面', link: '/server_plane' },
          { text: 'API 参考', link: '/api/overview' },
        ]},
        { text: '专题/历史文档', collapsed: true, items: [
          { text: '部署说明', link: '/design-notes/DEPLOYMENT' },
          { text: '分布式部署', link: '/design-notes/DISTRIBUTED_DEPLOYMENT' },
          { text: '扩展性设计', link: '/design-notes/SCALABILITY' },
          { text: '扩展性总结', link: '/design-notes/SCALABILITY_SUMMARY' },
          { text: 'Chat 改造实现', link: '/design-notes/SCALABILITY_CHAT_IMPLEMENTATION' },
          { text: '游戏聊天设计', link: '/design-notes/game_chat_architecture' },
          { text: '游戏聊天功能', link: '/design-notes/game_chat_features' },
          { text: 'SDK 钩子接口', link: '/design-notes/sdk_hooks' },
          { text: 'SDK 引擎兼容性', link: '/design-notes/sdk_compatibility' },
          { text: '战斗最佳实践', link: '/design-notes/game_combat_best_practices' },
          { text: 'NPC 对话系统', link: '/design-notes/npc_dialog_system' },
          { text: '集成测试修复', link: '/design-notes/INTEGRATION_TEST_FIXES' },
          { text: '旧版 API 说明', link: '/API' },
          { text: '旧版快速开始', link: '/QUICKSTART' },
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
