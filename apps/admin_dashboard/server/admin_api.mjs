#!/usr/bin/env node
// Dev API server for the Chirp Admin Dashboard.
//
// The dashboard's pages call /api/stats, /api/users, /api/channels and
// /api/messages (recent + search). The production backend does not exist
// yet — the C++ services only expose /metrics (Prometheus text) and there
// is no service that owns an admin HTTP contract. Until one lands, `pnpm
// dev` proxied to a dead port; this server answers the same contract the
// pages render (and the vitest suites pin) so the dashboard runs end to
// end in the browser.
//
// This is a DEV fixture: the data is a fixed seed, not live service
// state. Production wiring (real users/messages/channels backed by the
// chat store) is a separate task tracked in todo.md.
//
// Usage: node server/admin_api.mjs          (PORT env overrides 5000)
import http from 'node:http';

const PORT = Number(process.env.PORT ?? 5000);

// Seed data mirrors the shapes pinned by the frontend suites (App.test.tsx,
// Users/Channels/Messages test files) so dev and tests agree on the
// contract.
const seed = {
  stats: { totalUsers: 1234567, onlineUsers: 8912, totalMessages: 55, activeChannels: 7 },
  users: [
    { id: 'u1', username: 'Ada', email: 'ada@example.com', status: 'online', role: 'admin', joinedAt: '2024-01-01', lastSeen: '2024-03-01', messagesSent: 10 },
    { id: 'u2', username: 'Bob', email: 'bob@example.com', status: 'offline', role: 'user', joinedAt: '2024-01-02', lastSeen: '2024-03-02', messagesSent: 20 },
    { id: 'u3', username: 'Carol', email: 'carol@example.com', status: 'online', role: 'user', joinedAt: '2024-01-03', lastSeen: '2024-03-03', messagesSent: 7 },
  ],
  channels: [
    { id: 'ch_1', name: 'general', type: 'text', category: 'General', memberCount: 1523, messageCount: 45678, createdAt: '2024-01-01' },
    { id: 'ch_2', name: 'voice-general', type: 'voice', category: 'Voice', memberCount: 45, messageCount: 0, createdAt: '2024-01-01' },
    { id: 'ch_3', name: 'announcements', type: 'announcement', category: 'Info', memberCount: 2341, messageCount: 234, createdAt: '2024-01-01' },
  ],
  messages: [
    { id: 'm1', content: 'Hello everyone!', author: 'User1', channel: 'general', timestamp: '2024-03-18 10:30', reactions: 5 },
    { id: 'm2', content: 'Welcome to the server!', author: 'Admin', channel: 'general', timestamp: '2024-03-18 10:31', reactions: 0 },
    { id: 'm3', content: 'Deploy window tonight at 22:00 UTC.', author: 'Ops', channel: 'announcements', timestamp: '2024-03-18 10:32', reactions: 2 },
  ],
};

function json(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify(body));
}

const server = http.createServer((req, res) => {
  if (req.method !== 'GET') {
    json(res, 405, { error: 'method not allowed' });
    return;
  }
  // Parse against a fixed base: the client-supplied Host header must never
  // reach the URL parser (a malformed Host like "a b" used to throw out of
  // new URL and take the whole dev server down). Only the path/query of the
  // request target is consumed, so the base origin is irrelevant.
  const url = new URL(req.url, 'http://localhost');
  const path = url.pathname;

  if (path === '/api/stats') {
    json(res, 200, seed.stats);
    return;
  }
  if (path === '/api/users') {
    json(res, 200, seed.users);
    return;
  }
  if (path === '/api/channels') {
    json(res, 200, seed.channels);
    return;
  }
  if (path === '/api/messages') {
    // limit is an optional positive-integer cap; anything else (absent,
    // non-numeric, fractional, zero, negative) falls back to the default.
    const raw = url.searchParams.get('limit');
    const parsed = raw === null ? 50 : Number(raw);
    const limit = Number.isInteger(parsed) && parsed > 0 ? parsed : 50;
    json(res, 200, seed.messages.slice(0, limit));
    return;
  }
  if (path === '/api/messages/search') {
    const q = (url.searchParams.get('q') ?? '').trim().toLowerCase();
    const results = q
      ? seed.messages.filter((m) => m.content.toLowerCase().includes(q) || m.author.toLowerCase().includes(q))
      : [];
    json(res, 200, { results });
    return;
  }
  json(res, 404, { error: `no route for ${path}` });
});

server.listen(PORT, () => {
  // Report the actually bound port: PORT=0 asks the OS for an ephemeral
  // port (the contract test uses that to avoid fixed-port collisions).
  const bound = server.address().port;
  console.log(`admin dashboard dev API listening on port ${bound}`);
});
