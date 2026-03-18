import { defaultTheme } from '@vuepress/theme-default';
import { searchPlugin } from '@vuepress/plugin-search';
import { sitemapPlugin } from 'vuepress-plugin-sitemap2';

const base = process.env.VUEPRESS_BASE || '/chirp/';

export default {
  base,
  lang: 'en-US',
  title: 'Chirp',
  description: 'Real-time Communication Platform for Gaming and Interactive Applications',

  head: [
    ['link', { rel: 'icon', href: '/logo.png' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:site', content: '@chirp_project' }],
  ],

  extends: defaultTheme({
    logo: '/logo.png',

    navbar: [
      {
        text: 'Guide',
        children: [
          '/guide/introduction.md',
          '/guide/getting-started.md',
          '/guide/architecture.md',
          '/guide/deployment.md',
        ],
      },
      {
        text: 'API Reference',
        children: [
          '/api/overview.md',
          '/api/protocol.md',
          '/api/gateway.md',
          '/api/chat.md',
          '/api/social.md',
          '/api/voice.md',
          '/api/notification.md',
        ],
      },
      {
        text: 'SDKs',
        children: [
          '/sdk/cpp.md',
          '/sdk/unity.md',
          '/sdk/unreal.md',
          '/sdk/flutter.md',
        ],
      },
      {
        text: 'Services',
        children: [
          '/services/gateway.md',
          '/services/auth.md',
          '/services/chat.md',
          '/services/social.md',
          '/services/voice.md',
          '/services/notification.md',
          '/services/search.md',
        ],
      },
      {
        text: 'Operations',
        children: [
          '/ops/monitoring.md',
          '/ops/scalability.md',
          '/ops/deployment.md',
        ],
      },
      {
        text: 'GitHub',
        link: 'https://github.com/cuihairu/chirp',
      },
    ],

    sidebar: {
      '/guide/': [
        {
          text: 'Getting Started',
          collapsible: false,
          children: [
            '/guide/introduction.md',
            '/guide/getting-started.md',
            '/guide/installation.md',
            '/guide/quickstart.md',
          ],
        },
        {
          text: 'Core Concepts',
          collapsible: false,
          children: [
            '/guide/architecture.md',
            '/guide/protocol.md',
            '/guide/services.md',
          ],
        },
        {
          text: 'Advanced',
          collapsible: true,
          children: [
            '/guide/deployment.md',
            '/guide/scalability.md',
            '/guide/security.md',
          ],
        },
      ],
      '/api/': [
        '/api/overview.md',
        '/api/protocol.md',
        {
          text: 'Services',
          collapsible: true,
          children: [
            '/api/gateway.md',
            '/api/chat.md',
            '/api/social.md',
            '/api/voice.md',
            '/api/notification.md',
            '/api/search.md',
          ],
        },
        {
          text: 'Events',
          collapsible: true,
          children: [
            '/api/events-chat.md',
            '/api/events-social.md',
            '/api/events-voice.md',
          ],
        },
        {
          text: 'Models',
          collapsible: true,
          children: [
            '/api/models-channel.md',
            '/api/models-group.md',
            '/api/models-presence.md',
          ],
        },
      ],
      '/sdk/': [
        '/sdk/overview.md',
        {
          text: 'Core SDK (C++)',
          collapsible: true,
          children: [
            '/sdk/cpp-installation.md',
            '/sdk/cpp-configuration.md',
            '/sdk/cpp-chat.md',
            '/sdk/cpp-social.md',
            '/sdk/cpp-voice.md',
          ],
        },
        {
          text: 'Unity SDK',
          collapsible: true,
          children: [
            '/sdk/unity-installation.md',
            '/sdk/unity-setup.md',
            '/sdk/unity-chat.md',
            '/sdk/unity-voice.md',
          ],
        },
        {
          text: 'Unreal SDK',
          collapsible: true,
          children: [
            '/sdk/unreal-installation.md',
            '/sdk/unreal-setup.md',
            '/sdk/unreal-blueprints.md',
          ],
        },
        {
          text: 'Flutter SDK',
          collapsible: true,
          children: [
            '/sdk/flutter-installation.md',
            '/sdk/flutter-setup.md',
            '/sdk/flutter-chat.md',
            '/sdk/flutter-voice.md',
          ],
        },
      ],
    },

    editLink: true,
    lastUpdated: true,
    contributors: true,
  }),

  plugins: [
    searchPlugin({
      locales: {
        '/': {
          placeholder: 'Search',
        },
      },
      maxSuggestions: 15,
      hotKeys: ['/', 'ctrl+k'],
    }),
    sitemapPlugin({
      hostname: 'https://cuihairu.github.io/chirp',
    }),
  ],

  markdown: {
    code: {
      lineNumbers: 20,
    },
  },

  bundler: {
    vite: {
      vue: {
        template: {
          compilerOptions: {
            isCustomElement: (tag) => tag === 'car-components',
          },
        },
      },
    },
  },
};
