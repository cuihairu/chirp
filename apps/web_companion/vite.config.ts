import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Dev proxy keeps the page same-origin: chirp's WebSocket edges do not check
// the upgrade path, so the proxy can forward /ws/chat and /ws/social to the
// chat (7001) and social (8001) edges. In production, terminate TLS on a
// reverse proxy and point VITE_CHAT_WS_URL / VITE_SOCIAL_WS_URL at wss://.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 3001,
    proxy: {
      '/ws/chat': { target: 'ws://127.0.0.1:7001', ws: true },
      '/ws/social': { target: 'ws://127.0.0.1:8001', ws: true },
    },
  },
});
