import { defineConfig } from 'vitest/config';
import { resolve } from 'node:path';

export default defineConfig({
  resolve: {
    alias: {
      // Generated protobuf code lives outside this package; the commit
      // keeps CI free of any protobuf toolchain.
      '@chirp/proto': resolve(import.meta.dirname, '../../../proto/ts/proto'),
    },
  },
  test: {
    // Browser-shaped runtime: chirp_client and its fakes construct
    // WebSocket-flavoured DOM events (CloseEvent/MessageEvent).
    environment: 'jsdom',
    include: ['src/**/*.test.ts'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'lcov'],
      include: ['src/**'],
      thresholds: {
        // App-side protocol layer: same 90% bar as the game SDK this was
        // forked from — the two stay in lockstep on semantics and vectors.
        global: { lines: 90, branches: 90, functions: 90, statements: 90 },
      },
    },
  },
});
