import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

// Mirrors apps/web_companion's vitest config (same vite/jsdom/RTL stack);
// no workspace aliases needed — the dashboard has no @chirp/* imports.
export default defineConfig({
  plugins: [react()],
  test: {
    environment: 'jsdom',
    // globals:true lets @testing-library/react register its auto-cleanup,
    // otherwise DOM from one test leaks into the next.
    globals: true,
    setupFiles: ['./src/test-setup.ts'],
    include: ['src/**/*.test.ts', 'src/**/*.test.tsx'],
    // This dev box runs builds and other agents concurrently; parallel jsdom
    // workers starve the 5s budgets (a 5s poll test timed out at 15s under
    // six-worker load). Serial files + a 15s budget keep the suite stable.
    maxWorkers: 1,
    testTimeout: 15000,
  },
});
