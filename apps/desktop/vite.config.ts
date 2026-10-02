import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// 与 web_companion 同一套别名约定：@chirp/proto 指 proto/ts 的 ts-proto 产物，
// @chirp/app-protocol 指 apps/shared/protocol 的 app 侧协议层
// （tsconfig paths 与此一一对应；app 不依赖 sdks/——那是游戏接入面）。
export default defineConfig({
  plugins: [react()],
  clearScreen: false,
  server: {
    port: 5180,
    strictPort: true,
  },
  build: {
    outDir: 'dist',
  },
  resolve: {
    alias: [
      { find: '@chirp/proto', replacement: new URL('../../proto/ts/proto', import.meta.url).pathname },
      { find: '@chirp/app-protocol', replacement: new URL('../shared/protocol/src', import.meta.url).pathname },
    ],
  },
});
