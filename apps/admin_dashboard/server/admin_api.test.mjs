// Contract test for the admin dashboard dev API server.
//
// Boots the server on an ephemeral port and asserts every /api/* endpoint
// the pages call answers 200 with the exact shape the frontend suites pin
// (App/Users/Channels/Messages test files). Runs with the built-in node
// test runner: pnpm test:api.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';

let child;
let base;

before(async () => {
  const port = 19000 + Math.floor(Math.random() * 1000);
  child = spawn(process.execPath, ['server/admin_api.mjs'], {
    cwd: new URL('..', import.meta.url).pathname,
    env: { ...process.env, PORT: String(port) },
    stdio: 'ignore',
  });
  base = `http://127.0.0.1:${port}`;
  // Wait for the server to accept connections.
  for (let i = 0; i < 50; i++) {
    try {
      const res = await fetch(`${base}/api/stats`);
      if (res.ok) return;
    } catch {
      // not up yet
    }
    await new Promise((r) => setTimeout(r, 50));
  }
  throw new Error('admin API server did not come up');
});

after(() => {
  child?.kill();
});

test('GET /api/stats answers the dashboard metric shape', async () => {
  const res = await fetch(`${base}/api/stats`);
  assert.equal(res.status, 200);
  const body = await res.json();
  assert.deepEqual(Object.keys(body).sort(), ['activeChannels', 'onlineUsers', 'totalMessages', 'totalUsers']);
  for (const v of Object.values(body)) assert.equal(typeof v, 'number');
});

test('GET /api/users answers the users table shape', async () => {
  const res = await fetch(`${base}/api/users`);
  assert.equal(res.status, 200);
  const body = await res.json();
  assert.ok(Array.isArray(body) && body.length > 0);
  assert.deepEqual(Object.keys(body[0]).sort(), ['email', 'id', 'joinedAt', 'lastSeen', 'messagesSent', 'role', 'status', 'username']);
});

test('GET /api/channels answers the channels table shape', async () => {
  const res = await fetch(`${base}/api/channels`);
  assert.equal(res.status, 200);
  const body = await res.json();
  assert.ok(Array.isArray(body) && body.length > 0);
  assert.deepEqual(Object.keys(body[0]).sort(), ['category', 'createdAt', 'id', 'memberCount', 'messageCount', 'name', 'type']);
});

test('GET /api/messages answers the message list shape and honors limit', async () => {
  const all = await (await fetch(`${base}/api/messages`)).json();
  assert.ok(Array.isArray(all) && all.length > 0);
  assert.deepEqual(Object.keys(all[0]).sort(), ['author', 'channel', 'content', 'id', 'reactions', 'timestamp']);

  const limited = await (await fetch(`${base}/api/messages?limit=1`)).json();
  assert.equal(limited.length, 1);
});

test('GET /api/messages/search filters and wraps in results', async () => {
  const hit = await (await fetch(`${base}/api/messages/search?q=hello`)).json();
  assert.equal(hit.results.length, 1);
  assert.equal(hit.results[0].id, 'm1');

  const miss = await (await fetch(`${base}/api/messages/search?q=zzz-no-match`)).json();
  assert.deepEqual(miss.results, []);
});

test('unknown /api path answers 404 and non-GET answers 405', async () => {
  assert.equal((await fetch(`${base}/api/nope`)).status, 404);
  const post = await fetch(`${base}/api/stats`, { method: 'POST' });
  assert.equal(post.status, 405);
});
