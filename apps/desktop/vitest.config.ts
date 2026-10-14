import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import { resolve } from 'node:path';

export default defineConfig({
  plugins: [react()],
  resolve: {
    // Mirrors vite.config.ts / tsconfig paths: the generated protos live
    // outside the app directory, the app-side protocol core in the sibling
    // workspace package.
    alias: {
      '@chirp/proto': resolve(import.meta.dirname, '../../proto/ts/proto'),
      '@chirp/app-protocol': resolve(import.meta.dirname, '../shared/protocol/src'),
    },
  },
  test: {
    environment: 'jsdom',
    // globals:true lets @testing-library/react register its auto-cleanup,
    // otherwise DOM from one test leaks into the next.
    globals: true,
    setupFiles: ['./src/test-setup.ts'],
    include: ['src/**/*.test.ts', 'src/**/*.test.tsx'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'lcov'],
      include: ['src/**'],
      // Smoke-level bar: the app shell's logic is covered by the web
      // companion's suite, this harness guards the desktop-only layers.
      thresholds: {
        global: { lines: 70, branches: 70, functions: 60, statements: 70 },
      },
    },
  },
});