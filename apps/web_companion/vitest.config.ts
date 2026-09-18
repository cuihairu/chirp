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
    // globals:true lets @testing-library/react register its auto-cleanup,
    // otherwise DOM from one test leaks into the next.
    globals: true,
    include: ['src/**/*.test.ts', 'src/**/*.test.tsx'],
    // The real-backend suite lives in its own config (vitest.integration.config.ts)
    // so skipped-by-default cases never dilute coverage here.
    exclude: ['**/node_modules/**', 'src/integration/**'],
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
