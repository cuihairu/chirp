import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// 与 web_companion 同一套别名约定：@chirp/proto 指 proto/ts 的 ts-proto 产物，
// @chirp/protocol 指 sdks/ts 协议核（tsconfig paths 与此一一对应）。
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
      { find: '@chirp/protocol', replacement: new URL('../../sdks/ts/src', import.meta.url).pathname },
    ],
  },
});
