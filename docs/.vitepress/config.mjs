import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'
import { fileURLToPath } from 'node:url'

// mermaid 11.x 的 dist 按 stylis 4.4 的导出面打包(import LAYER),
// vitepress 1.6.4 运行时按 stylis 4.1 的导出面加载(import SCOPE),
// 任何单一 stylis 版本都无法同时满足。stylis 经 pnpm-workspace.yaml
// 钉在 4.1.4(供 vitepress 运行时),打包期用下面的插件把裸 'stylis'
// 指到补齐 LAYER 常量的 shim(值取自 stylis 4.4.0 的 Enum.js)。
const stylisShim = fileURLToPath(new URL('./stylis-shim.mjs', import.meta.url))
const stylisShimPlugin = {
  name: 'stylis-shim',
  enforce: 'pre',
  resolveId(id, importer) {
    if (id !== 'stylis') return null
    if (importer && importer.replace(/\\/g, '/').endsWith('/.vitepress/stylis-shim.mjs')) return null
    return stylisShim
  },
}

// 双语结构:英文留在 srcDir 根(locale `root`,URL `/`),中文整体收在
// `docs/zh/`(locale `zh`,URL `/zh/`)。目录同名同构,VitePress 内置语言
// 切换器按同名相对路径互跳;不写浏览器自动跳转。
const config = defineConfig({
  title: 'Chirp',
  description: 'A lightweight real-time communication backend skeleton for game development',
  ignoreDeadLinks: false,
  head: [
    ['meta', { name: 'viewport', content: 'width=device-width,initial-scale=1' }],
    ['meta', { name: 'keywords', content: 'chirp,game chat,real-time communication,game backend,game server,C++,C++23,TCP,WebSocket,Protobuf,ASIO' }],
    ['meta', { name: 'theme-color', content: '#3eaf7c' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:site', content: '@chirp_project' }],
  ],
  base: '/chirp/',

  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      title: 'Chirp',
      description: 'A lightweight real-time communication backend skeleton for game development',
      themeConfig: {
        logo: '/logo.svg',
        nav: [
          { text: 'Core', link: '/CORE' },
          { text: 'Getting Started', link: '/guide/getting-started' },
          { text: 'Architecture', link: '/architecture' },
          { text: 'API', link: '/api/overview' },
          {
            text: 'SDKs',
            items: [
              { text: 'SDK Overview', link: '/sdk/' },
              { text: 'Unity3D', link: '/sdk/unity3d' },
              { text: 'Unreal', link: '/sdk/unreal' },
              { text: 'Cocos Creator', link: '/sdk/cocos-creator' },
              { text: 'LayaAir', link: '/sdk/layaair' },
              { text: 'WeChat Mini Game', link: '/sdk/wechat-minigame' },
              { text: 'Godot', link: '/sdk/godot' },
              { text: 'Server SDK', link: '/sdk/server' },
            ],
          },
        ],
        sidebar: {
          '/sdk/': [
            { text: 'SDKs', collapsed: false, items: [
              { text: 'SDK Overview', link: '/sdk/' },
              { text: 'Unity3D', link: '/sdk/unity3d' },
              { text: 'Unreal', link: '/sdk/unreal' },
              { text: 'Cocos Creator', link: '/sdk/cocos-creator' },
              { text: 'LayaAir', link: '/sdk/layaair' },
              { text: 'WeChat Mini Game', link: '/sdk/wechat-minigame' },
              { text: 'Godot', link: '/sdk/godot' },
              { text: 'Server SDK', link: '/sdk/server' },
            ]},
          ],
          '/guide/': [
            { text: 'Guide', collapsed: false, items: [
              { text: 'Introduction', link: '/guide/introduction' },
              { text: 'Installation', link: '/guide/installation' },
              { text: 'Getting Started', link: '/guide/getting-started' },
              { text: 'Deployment', link: '/guide/deployment' },
              { text: 'Integration Pitfalls', link: '/guide/integration-pitfalls' },
            ]},
          ],
          '/api/': [
            { text: 'API Reference', collapsed: false, items: [
              { text: 'Overview', link: '/api/overview' },
              { text: 'Chat Peer Registration Protocol', link: '/api/peer_protocol' },
            ]},
          ],
          '/': [
            { text: 'Core Docs', collapsed: false, items: [
              { text: 'Home', link: '/' },
              { text: 'Docs Guide', link: '/README' },
              { text: 'Core', link: '/CORE' },
              { text: 'Capability Matrix', link: '/CAPABILITY_MATRIX' },
              { text: 'Introduction', link: '/guide/introduction' },
              { text: 'Installation', link: '/guide/installation' },
              { text: 'Getting Started', link: '/guide/getting-started' },
              { text: 'Integration Pitfalls', link: '/guide/integration-pitfalls' },
              { text: 'Overall Architecture', link: '/architecture' },
              { text: 'Server Plane', link: '/server_plane' },
              { text: 'API Reference', link: '/api/overview' },
            ]},
            { text: 'Topic & Historical Docs', collapsed: true, items: [
              { text: 'Deployment', link: '/design-notes/DEPLOYMENT' },
              { text: 'Distributed Deployment', link: '/design-notes/DISTRIBUTED_DEPLOYMENT' },
              { text: 'Scalability Design', link: '/design-notes/SCALABILITY' },
              { text: 'Scalability Summary', link: '/design-notes/SCALABILITY_SUMMARY' },
              { text: 'Scalability Chat Implementation', link: '/design-notes/SCALABILITY_CHAT_IMPLEMENTATION' },
              { text: 'Game Chat Design', link: '/design-notes/game_chat_architecture' },
              { text: 'Game Chat Features', link: '/design-notes/game_chat_features' },
              { text: 'App Chat Prototype', link: '/design-notes/app_chat_prototype' },
              { text: 'Word Filter Specification', link: '/design-notes/word_filter' },
              { text: 'SDK Hooks', link: '/design-notes/sdk_hooks' },
              { text: 'SDK Engine Compatibility', link: '/design-notes/sdk_compatibility' },
              { text: 'Combat Best Practices', link: '/design-notes/game_combat_best_practices' },
              { text: 'NPC Dialog System', link: '/design-notes/npc_dialog_system' },
              { text: 'Communication Core Model', link: '/design-notes/communication_core' },
              { text: 'Integration Test Fixes', link: '/design-notes/INTEGRATION_TEST_FIXES' },
              { text: 'Luna Review (2026-10-18)', link: '/review/luna-review-2026-10-18' },
              { text: 'Legacy API', link: '/API' },
              { text: 'Legacy Quick Start', link: '/QUICKSTART' },
            ]},
          ],
        },
        editLink: {
          pattern: 'https://github.com/cuihairu/chirp/edit/main/docs/:path',
          text: 'Edit this page on GitHub',
        },
        lastUpdated: { text: 'Last updated' },
        socialLinks: [{ icon: 'github', link: 'https://github.com/cuihairu/chirp' }],
        search: { provider: 'local' },
        docFooter: { prev: 'Previous page', next: 'Next page' },
        outline: { label: 'On this page', level: [2, 3] },
        darkModeSwitchLabel: 'Appearance',
        lightModeSwitchTitle: 'Switch to light theme',
        darkModeSwitchTitle: 'Switch to dark theme',
        sidebarMenuLabel: 'Menu',
        returnToTopLabel: 'Return to top',
        langMenuLabel: 'Change language',
      },
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      title: 'Chirp',
      description: '面向游戏开发的轻量实时通信后端骨架',
      themeConfig: {
        logo: '/logo.svg',
        nav: [
          { text: '核心说明', link: '/zh/CORE' },
          { text: '快速开始', link: '/zh/guide/getting-started' },
          { text: '架构', link: '/zh/architecture' },
          { text: 'API', link: '/zh/api/overview' },
          {
            text: 'SDK 接入',
            items: [
              { text: 'SDK 总览', link: '/zh/sdk/' },
              { text: 'Unity3D', link: '/zh/sdk/unity3d' },
              { text: 'Unreal', link: '/zh/sdk/unreal' },
              { text: 'Cocos Creator', link: '/zh/sdk/cocos-creator' },
              { text: 'LayaAir', link: '/zh/sdk/layaair' },
              { text: '微信小游戏', link: '/zh/sdk/wechat-minigame' },
              { text: 'Godot', link: '/zh/sdk/godot' },
              { text: '服务端 SDK', link: '/zh/sdk/server' },
            ],
          },
        ],
        sidebar: {
          '/zh/sdk/': [
            { text: 'SDK 接入', collapsed: false, items: [
              { text: 'SDK 总览', link: '/zh/sdk/' },
              { text: 'Unity3D', link: '/zh/sdk/unity3d' },
              { text: 'Unreal', link: '/zh/sdk/unreal' },
              { text: 'Cocos Creator', link: '/zh/sdk/cocos-creator' },
              { text: 'LayaAir', link: '/zh/sdk/layaair' },
              { text: '微信小游戏', link: '/zh/sdk/wechat-minigame' },
              { text: 'Godot', link: '/zh/sdk/godot' },
              { text: '服务端 SDK', link: '/zh/sdk/server' },
            ]},
          ],
          '/zh/guide/': [
            { text: '入门指南', collapsed: false, items: [
              { text: '导言', link: '/zh/guide/introduction' },
              { text: '安装', link: '/zh/guide/installation' },
              { text: '快速开始', link: '/zh/guide/getting-started' },
              { text: '部署指南', link: '/zh/guide/deployment' },
              { text: '接入避坑指南', link: '/zh/guide/integration-pitfalls' },
            ]},
          ],
          '/zh/api/': [
            { text: 'API 参考', collapsed: false, items: [
              { text: '概述', link: '/zh/api/overview' },
              { text: 'Chat Peer 注册协议', link: '/zh/api/peer_protocol' },
            ]},
          ],
          '/zh/': [
            { text: '核心文档', collapsed: false, items: [
              { text: '首页', link: '/zh/' },
              { text: '文档说明', link: '/zh/README' },
              { text: '核心说明', link: '/zh/CORE' },
              { text: '能力矩阵', link: '/zh/CAPABILITY_MATRIX' },
              { text: '导言', link: '/zh/guide/introduction' },
              { text: '安装', link: '/zh/guide/installation' },
              { text: '快速开始', link: '/zh/guide/getting-started' },
              { text: '接入避坑指南', link: '/zh/guide/integration-pitfalls' },
              { text: '整体架构', link: '/zh/architecture' },
              { text: '服务器平面', link: '/zh/server_plane' },
              { text: 'API 参考', link: '/zh/api/overview' },
            ]},
            { text: '专题/历史文档', collapsed: true, items: [
              { text: '部署说明', link: '/zh/design-notes/DEPLOYMENT' },
              { text: '分布式部署', link: '/zh/design-notes/DISTRIBUTED_DEPLOYMENT' },
              { text: '扩展性设计', link: '/zh/design-notes/SCALABILITY' },
              { text: '扩展性总结', link: '/zh/design-notes/SCALABILITY_SUMMARY' },
              { text: 'Chat 改造实现', link: '/zh/design-notes/SCALABILITY_CHAT_IMPLEMENTATION' },
              { text: '游戏聊天设计', link: '/zh/design-notes/game_chat_architecture' },
              { text: '游戏聊天功能', link: '/zh/design-notes/game_chat_features' },
              { text: '应用聊天原型', link: '/zh/design-notes/app_chat_prototype' },
              { text: '敏感词过滤规约', link: '/zh/design-notes/word_filter' },
              { text: 'SDK 钩子接口', link: '/zh/design-notes/sdk_hooks' },
              { text: 'SDK 引擎兼容性', link: '/zh/design-notes/sdk_compatibility' },
              { text: '战斗最佳实践', link: '/zh/design-notes/game_combat_best_practices' },
              { text: 'NPC 对话系统', link: '/zh/design-notes/npc_dialog_system' },
              { text: 'Communication Core 模型', link: '/zh/design-notes/communication_core' },
              { text: '集成测试修复', link: '/zh/design-notes/INTEGRATION_TEST_FIXES' },
              { text: '旧版 API 说明', link: '/zh/API' },
              { text: '旧版快速开始', link: '/zh/QUICKSTART' },
            ]},
          ],
        },
        editLink: {
          pattern: 'https://github.com/cuihairu/chirp/edit/main/docs/:path',
          text: '在 GitHub 上编辑此页',
        },
        lastUpdated: { text: '最后更新' },
        socialLinks: [{ icon: 'github', link: 'https://github.com/cuihairu/chirp' }],
        search: { provider: 'local' },
        docFooter: { prev: '上一页', next: '下一页' },
      },
    },
  },

  vite: {
    plugins: [stylisShimPlugin],
    build: {
      chunkSizeWarningLimit: 1200,
    },
  },
})

export default withMermaid(config)
