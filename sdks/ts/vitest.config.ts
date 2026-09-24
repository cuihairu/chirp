import { defineConfig } from 'vitest/config';
import { resolve } from 'node:path';

export default defineConfig({
  resolve: {
    alias: {
      // Generated protobuf code lives outside this package; the commit
      // keeps CI free of any protobuf toolchain.
      '@chirp/proto': resolve(import.meta.dirname, '../../proto/ts/proto'),
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
        // This package is the contract with the C++ backend and the
        // blueprint the Unity/Dart ports were written against; hold it to
        // the stricter bar the web companion used to enforce for it.
        global: { lines: 90, branches: 90, functions: 90, statements: 90 },
      },
    },
  },
});
