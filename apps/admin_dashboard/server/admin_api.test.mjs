// Contract test for the admin dashboard dev API server.
//
// Boots the server on an ephemeral port and asserts every /api/* endpoint
// the pages call answers 200 with the exact shape the frontend suites pin
// (App/Users/Channels/Messages test files). Runs with the built-in node
// test runner: pnpm test:api.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn } from 'node:child_process';

let child;
let base;

before(async () => {
  // PORT=0 makes the OS assign an ephemeral port: a fixed random range can
  // collide with another process and turn the whole suite into a spurious
  // "did not come up" failure. The actual port is read back from the
  // server's startup log (it prints server.address().port).
  child = spawn(process.execPath, ['server/admin_api.mjs'], {
    cwd: new URL('..', import.meta.url).pathname,
    env: { ...process.env, PORT: '0' },
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  base = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('admin API server did not come up')), 5000);
    child.stdout.on('data', (chunk) => {
      const match = String(chunk).match(/listening on port (\d+)/);
      if (match) {
        clearTimeout(timer);
        resolve(`http://127.0.0.1:${match[1]}`);
      }
    });
  });
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

test('invalid limit values fall back to the default instead of corrupting the slice', async () => {
  const total = (await (await fetch(`${base}/api/messages`)).json()).length;
  // Negative, fractional, non-numeric, zero and empty values must all yield
  // the default (the whole seed here) - not truncated or empty arrays.
  for (const bad of ['-1', '-999', '2.5', 'abc', '0', '']) {
    const body = await (await fetch(`${base}/api/messages?limit=${encodeURIComponent(bad)}`)).json();
    assert.equal(body.length, total, `limit=${bad}`);
  }
});

test('a malformed Host header must not crash the server', async () => {
  // "Host: a b" passes the HTTP parser but used to throw out of new URL and
  // kill the process. Speak raw HTTP over a socket to bypass any client-side
  // header sanitization.
  const reply = await new Promise((resolve, reject) => {
    const sock = net.connect(Number(new URL(base).port), '127.0.0.1', () => {
      sock.write('GET /api/stats HTTP/1.1\r\nHost: a b\r\nConnection: close\r\n\r\n');
    });
    sock.on('data', (d) => { resolve(String(d)); sock.end(); });
    sock.on('error', reject);
  });
  assert.match(reply, /HTTP\/1\.1 200/);

  // The server must still be alive and answering afterwards.
  const after = await fetch(`${base}/api/stats`);
  assert.equal(after.status, 200);
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
