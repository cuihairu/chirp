import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import { resolve } from 'node:path';

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      // Generated protobuf code lives outside the app directory; the commit
      // keeps CI free of any protobuf toolchain.
      '@chirp/proto': resolve(import.meta.dirname, '../../proto/ts/proto'),
    },
  },
  test: {
    environment: 'jsdom',
    include: ['src/**/*.test.ts', 'src/**/*.test.tsx'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'lcov'],
      include: ['src/**'],
      thresholds: {
        // The protocol layer is the contract with the C++ backend and the
        // blueprint for the future Dart port; hold it to a stricter bar than
        // the UI, which only gets smoke-level tests.
        'src/protocol/**': { lines: 90, branches: 90, functions: 90, statements: 90 },
        global: { lines: 70, branches: 70, functions: 60, statements: 70 },
      },
    },
  },
});
