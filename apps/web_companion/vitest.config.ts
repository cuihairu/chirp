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
      // The framework-agnostic protocol core lives in its own workspace
      // package; alias keeps vitest resolution identical to tsc's paths.
      '@chirp/protocol': resolve(import.meta.dirname, '../../sdks/ts/src'),
    },
  },
  test: {
    environment: 'jsdom',
    // globals:true lets @testing-library/react register its auto-cleanup,
    // otherwise DOM from one test leaks into the next.
    globals: true,
    setupFiles: ['./src/test-setup.ts'],
    include: ['src/**/*.test.ts', 'src/**/*.test.tsx'],
    // The real-backend suite lives in its own config (vitest.integration.config.ts)
    // so skipped-by-default cases never dilute coverage here.
    exclude: ['**/node_modules/**', 'src/integration/**'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'lcov'],
      include: ['src/**'],
      thresholds: {
        // The 90% contract-layer bar moved with the protocol core into
        // sdks/ts (its own vitest config enforces it there); the UI keeps
        // smoke-level coverage thresholds.
        global: { lines: 70, branches: 70, functions: 60, statements: 70 },
      },
    },
  },
});
