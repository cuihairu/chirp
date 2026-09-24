import { resolve } from 'node:path';
import { defineConfig } from 'vitest/config';

// Real-backend integration suite, driven by scripts/web_smoke.sh. Never
// picked up by the unit `npm test` run; cases self-skip unless
// CHIRP_WS_URL points at a live chat websocket.
export default defineConfig({
  resolve: {
    alias: {
      '@chirp/proto': resolve(import.meta.dirname, '../../proto/ts/proto'),
      '@chirp/protocol': resolve(import.meta.dirname, '../../sdks/ts/src'),
    },
  },
  test: {
    environment: 'node',
    include: ['src/integration/**/*.test.ts'],
    testTimeout: 15_000,
    hookTimeout: 15_000,
  },
});
