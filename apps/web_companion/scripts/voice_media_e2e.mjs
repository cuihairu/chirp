#!/usr/bin/env node
// Real-browser voice media E2E: two headless chromium clients with fake mic
// (--use-fake-device-for-media-stream + --use-fake-ui-for-media-stream) join
// one voice room through the real UI, and the test asserts the actual media
// plane: SDP negotiation completes on both sides, a remote audio track
// arrives, and audio frames flow (inbound/outbound byte counters grow, with
// the candidate-pair RTT logged as evidence). The speaking dots in the
// dialog are the analyser path exercised against the browser's fake tone.
//
// Orchestration mirrors scripts/web_smoke.sh: real chirp_chat / chirp_social
// / chirp_voice in in-memory mode on deduped free ports, plus a vite dev
// server whose /ws/* proxy is retargeted at those ports. Party and device
// planes are left to degrade (the app hides those features when down).
//
// Requires the backend binaries (build them first):
//   ./build/services/shared/chat/chirp_chat
//   ./build/services/social/chirp_social
//   ./build/services/voice/chirp_voice
//
// Run:  node scripts/voice_media_e2e.mjs
// Evidence lands in e2e-artifacts/ (screenshots) and stdout (stats).

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from 'vite';
import { chromium } from 'playwright';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const APP_DIR = path.resolve(SCRIPT_DIR, '..');
const REPO_ROOT = path.resolve(APP_DIR, '..', '..');
const ART_DIR = path.join(APP_DIR, 'e2e-artifacts');

const CHAT_BIN = process.env.CHAT_BIN ?? path.join(REPO_ROOT, 'build/services/shared/chat/chirp_chat');
const SOCIAL_BIN = process.env.SOCIAL_BIN ?? path.join(REPO_ROOT, 'build/services/social/chirp_social');
const VOICE_BIN = process.env.VOICE_BIN ?? path.join(REPO_ROOT, 'build/services/voice/chirp_voice');

const log = (...a) => console.log('[e2e]', ...a);
const fail = (...a) => console.error('[e2e] FAIL:', ...a);
// First bytes of a ws frame as hex — the ChirpClient header carries the msg
// id, so the sequence of frame sizes/ids shows where signalling stops.
const hexHead = (payload) => {
  const b = typeof payload === 'string' ? Buffer.from(payload) : Buffer.from(payload ?? []);
  return b.subarray(0, 6).toString('hex');
};

// Same bind(0)->close dedup as web_smoke.sh: the kernel hands the port right
// back to the next bind(0), so redraw on repeats and on occupied ports.
const issued = new Set();
function pickPort() {
  return new Promise((resolve, reject) => {
    const tryOnce = (attempt) => {
      const srv = net.createServer();
      srv.listen(0, '127.0.0.1', () => {
        const port = srv.address().port;
        srv.close(() => {
          if (issued.has(port)) return tryOnce(attempt + 1);
          // Occupied by another process? A quick connect probe redraws.
          const probe = net.connect({ port, host: '127.0.0.1', timeout: 200 });
          probe.on('connect', () => {
            probe.destroy();
            if (attempt < 200) tryOnce(attempt + 1);
            else reject(new Error('pickPort: no unique free port'));
          });
          probe.on('error', () => {
            issued.add(port);
            resolve(port);
          });
        });
      });
    };
    tryOnce(0);
  });
}

function waitPort(port, name, timeoutS = 15) {
  const deadline = Date.now() + timeoutS * 1000;
  return new Promise((resolve, reject) => {
    const tryOnce = () => {
      const s = net.connect({ port, host: '127.0.0.1', timeout: 500 });
      s.on('connect', () => { s.destroy(); resolve(); });
      s.on('error', () => {
        if (Date.now() > deadline) reject(new Error(`${name} port ${port} not ready`));
        else setTimeout(tryOnce, 100);
      });
      s.on('timeout', () => { s.destroy(); });
    };
    tryOnce();
  });
}

const children = [];
function start(bin, args, logFile, env = {}) {
  const out = fs.openSync(logFile, 'w');
  const child = spawn(bin, args, { cwd: REPO_ROOT, stdio: ['ignore', out, out], env: { ...process.env, ...env } });
  children.push(child);
  return child;
}
async function stopAll() {
  for (const c of children.splice(0)) {
    if (c.exitCode === null && !c.killed) {
      c.kill('SIGTERM');
      await new Promise((r) => setTimeout(r, 500));
      try { c.kill('SIGKILL'); } catch { /* already gone */ }
    }
  }
}

// Injected before app code on every page: record every RTCPeerConnection so
// the test can inspect negotiation state and pull WebRTC stats live. With
// FORCE_RELAY the pc is pinned to relay-only ICE (iceTransportPolicy) so the
// connection can only complete through the TURN allocation — the honest way
// to prove the relay path instead of trusting a host-candidate win.
const PC_CAPTURE = (forceRelay) => `
  window.__chirpPcs = [];
  const NativePC = window.RTCPeerConnection;
  class CapturedPC extends NativePC {
    constructor(cfg) {
      if (${forceRelay} && cfg) cfg = { ...cfg, iceTransportPolicy: 'relay' };
      super(cfg);
      window.__chirpPcs.push(this);
      window.__chirpPcConfig = cfg;
    }
  }
  window.RTCPeerConnection = CapturedPC;
`;

async function waitFor(fn, what, timeoutMs = 20000, stepMs = 250) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      const v = await fn();
      if (v) return v;
      last = v;
    } catch (e) { last = `error: ${e.message}`; }
    await new Promise((r) => setTimeout(r, stepMs));
  }
  throw new Error(`timeout waiting for ${what}${last !== undefined ? ` (last: ${JSON.stringify(last)})` : ''}`);
}

async function login(page, base, userId) {
  await page.goto(base, { waitUntil: 'domcontentloaded', timeout: 60000 });
  await page.getByLabel('用户 ID').fill(userId);
  await page.getByRole('button', { name: '登录' }).click();
  await page.waitForURL('**/chat', { timeout: 30000 });
  log(`${userId} logged in`);
}

// Pull the media-plane facts off the page's captured pcs.
function meshFacts() {
  return window.__chirpPcs.map((pc) => ({
    connectionState: pc.connectionState,
    signalingState: pc.signalingState,
    hasLocal: !!pc.localDescription,
    hasRemote: !!pc.remoteDescription,
    remoteTracks: pc.getReceivers().map((r) => ({
      kind: r.track?.kind,
      readyState: r.track?.readyState,
      muted: r.track?.muted,
    })),
  }));
}

async function main() {
  for (const bin of [CHAT_BIN, SOCIAL_BIN, VOICE_BIN]) {
    if (!fs.existsSync(bin)) {
      fail(`binary missing: ${bin} (build first)`);
      process.exit(1);
    }
  }
  fs.mkdirSync(ART_DIR, { recursive: true });

  // TURN mode: E2E_TURN_URI + E2E_TURN_SECRET hand the voice service its
  // coturn REST credentials (--turn_uri/--turn_static_secret); with
  // E2E_FORCE_RELAY=1 the pcs are pinned to relay-only ICE so the media can
  // only flow through the TURN allocation.
  const turnUri = process.env.E2E_TURN_URI ?? '';
  const turnSecret = process.env.E2E_TURN_SECRET ?? '';
  const forceRelay = process.env.E2E_FORCE_RELAY === '1';
  if (forceRelay && !turnUri) {
    fail('E2E_FORCE_RELAY=1 needs E2E_TURN_URI (relay-only ICE has no other path)');
    process.exit(1);
  }
  log(`mode: ${turnUri ? `turn (${turnUri})` : 'host-candidates'}${forceRelay ? ', relay-only ICE' : ''}`);

  const chatPort = await pickPort();
  const chatWsPort = await pickPort();
  const socialPort = await pickPort();
  const socialWsPort = await pickPort();
  const voicePort = await pickPort();
  const voiceWsPort = await pickPort();
  const vitePort = await pickPort();

  const chatLog = '/tmp/chirp_media_e2e_chat.log';
  const socialLog = '/tmp/chirp_media_e2e_social.log';
  const voiceLog = '/tmp/chirp_media_e2e_voice.log';
  start(CHAT_BIN, ['--port', String(chatPort), '--ws_port', String(chatWsPort)], chatLog);
  start(SOCIAL_BIN, ['--port', String(socialPort), '--ws_port', String(socialWsPort)], socialLog);
  const voiceArgs = ['--port', String(voicePort), '--ws_port', String(voiceWsPort)];
  if (turnUri) voiceArgs.push('--turn_uri', turnUri, '--turn_static_secret', turnSecret);
  start(VOICE_BIN, voiceArgs, voiceLog);
  await waitPort(chatWsPort, 'chat-ws');
  await waitPort(socialWsPort, 'social-ws');
  await waitPort(voiceWsPort, 'voice-ws');
  log(`backends up: chat ws :${chatWsPort}, social ws :${socialWsPort}, voice ws :${voiceWsPort}`);

  // Vite dev server with the /ws/* proxy retargeted at the picked ports and
  // the same source aliases the vitest integration config uses (the repo
  // resolves @chirp/* outside node_modules).
  const vite = await createServer({
    root: APP_DIR,
    logLevel: 'error',
    resolve: {
      alias: {
        '@chirp/proto': path.resolve(APP_DIR, '../../proto/ts/proto'),
        '@chirp/app-protocol': path.resolve(APP_DIR, '../../sdks/ts/src'),
      },
    },
    server: {
      port: vitePort,
      strictPort: true,
      proxy: {
        '/ws/chat': { target: `ws://127.0.0.1:${chatWsPort}`, ws: true },
        '/ws/social': { target: `ws://127.0.0.1:${socialWsPort}`, ws: true },
        '/ws/voice': { target: `ws://127.0.0.1:${voiceWsPort}`, ws: true },
      },
    },
  });
  await vite.listen();
  log(`vite dev server on http://127.0.0.1:${vitePort}`);

  const browser = await chromium.launch({
    headless: true,
    args: [
      '--use-fake-device-for-media-stream',
      '--use-fake-ui-for-media-stream',
      '--autoplay-policy=no-user-gesture-required',
      '--no-sandbox',
    ],
  });
  const base = `http://127.0.0.1:${vitePort}`;

  const ctxA = await browser.newContext();
  const ctxB = await browser.newContext();
  await ctxA.addInitScript(PC_CAPTURE(forceRelay));
  await ctxB.addInitScript(PC_CAPTURE(forceRelay));
  const alice = await ctxA.newPage();
  const bob = await ctxB.newPage();
  for (const p of [alice, bob]) {
    p.on('pageerror', (e) => fail('pageerror:', e.message));
    if (process.env.E2E_DEBUG === '1') {
      p.on('console', (m) => log(`console[${m.type()}]:`, m.text().slice(0, 200)));
      p.on('websocket', (ws) => {
        const tag = ws.url().replace(/^.*\/ws\//, 'ws:').slice(0, 24);
        log(`+socket ${tag}`);
        ws.on('framesent', (f) => log(`${tag} >> ${String(f.payload).length}b ${hexHead(f.payload)}`));
        ws.on('framereceived', (f) => log(`${tag} << ${String(f.payload).length}b ${hexHead(f.payload)}`));
      });
    }
  }

  try {
    // ---- drive the real UI: alice creates, bob joins by the shown id ----
    await login(alice, base, 'alice');
    await login(bob, base, 'bob');
    await alice.getByTestId('voice-button').click();
    // MUI TextField puts the testid on the wrapper; the input is inside.
    await alice.getByTestId('voice-room-name').locator('input').fill('media-e2e');
    await alice.getByRole('button', { name: '创建房间' }).click();
    const roomId = await waitFor(
      () => alice.getByTestId('voice-room-id').textContent(),
      'room id caption after create',
    );
    log(`alice created room: ${roomId}`);

    await bob.getByTestId('voice-button').click();
    await bob.getByTestId('voice-join-input').locator('input').fill(roomId.replace(/^房间号:\s*/, ''));
    await bob.getByRole('button', { name: '加入' }).click();
    await waitFor(
      () => bob.getByTestId('voice-room-id').textContent(),
      'bob joined room',
    );
    log(`bob joined room ${roomId}`);

    // ---- SDP done + track arrived, both sides ----
    const diag = async (page, who) => {
      const d = await page.evaluate(() => ({
        pcs: (window.__chirpPcs ?? []).length,
        facts: (window.__chirpPcs ?? []).map((pc) => ({ s: pc.signalingState, c: pc.connectionState })),
        mediaError: document.querySelector('[data-testid="voice-media-error"]')?.textContent ?? null,
        members: document.querySelector('[data-testid="voice-member-list"]')?.textContent ?? null,
      }));
      log(`${who} diag:`, JSON.stringify(d));
      return d;
    };
    await diag(alice, 'alice');
    await diag(bob, 'bob');

    const factsA = await waitFor(async () => {
      const f = await alice.evaluate(meshFacts);
      return f.length === 1 && f[0].connectionState === 'connected' ? f : null;
    }, 'alice mesh connected');
    const factsB = await waitFor(async () => {
      const f = await bob.evaluate(meshFacts);
      return f.length === 1 && f[0].connectionState === 'connected' ? f : null;
    }, 'bob mesh connected');

    for (const [who, f] of [['alice', factsA], ['bob', factsB]]) {
      const pc = f[0];
      if (pc.signalingState !== 'stable' || !pc.hasLocal || !pc.hasRemote) {
        throw new Error(`${who}: SDP not settled: ${JSON.stringify(pc)}`);
      }
      const audio = pc.remoteTracks.filter((t) => t.kind === 'audio' && t.readyState === 'live');
      if (audio.length === 0) throw new Error(`${who}: no live remote audio track: ${JSON.stringify(pc)}`);
    }
    log('SDP settled + live remote audio track on both sides');
    log('alice mesh:', JSON.stringify(factsA[0]));
    log('bob mesh:', JSON.stringify(factsB[0]));

    // ---- TURN surface: the join response must have carried the configured
    // ice servers down to the pc config, with REST credentials when set ----
    const cfg = await alice.evaluate(() => window.__chirpPcConfig ?? {});
    const cfgServers = cfg.iceServers ?? [];
    log('pc iceServers:', JSON.stringify(cfgServers));
    if (turnUri) {
      if (cfgServers.length === 0) throw new Error('TURN mode: pc config carries no iceServers');
      const urls = cfgServers.flatMap((s) => (Array.isArray(s.urls) ? s.urls : [s.urls]));
      if (!urls.some((u) => String(u).startsWith('turn:'))) {
        throw new Error(`TURN mode: no turn: url in pc config: ${urls.join(',')}`);
      }
      if (turnSecret && (!cfgServers[0].username || !cfgServers[0].credential)) {
        throw new Error('TURN mode: REST credentials missing from pc config');
      }
    } else if (cfgServers.length > 0) {
      throw new Error(`host mode: unexpected iceServers in pc config: ${JSON.stringify(cfgServers)}`);
    }

    // ---- audio frames flow: bytes must grow over a 3s window ----
    const sample = async (page, label) => {
      const s = await page.evaluate(async () => {
        const pc = window.__chirpPcs[0];
        const report = await pc.getStats();
        const out = { inboundBytes: 0, outboundBytes: 0, rttMs: null, packetsIn: 0, pair: null };
        const cands = new Map();
        report.forEach((st) => {
          if (st.type === 'inbound-rtp' && st.kind === 'audio') {
            out.inboundBytes = st.bytesReceived ?? 0;
            out.packetsIn = st.packetsReceived ?? 0;
          }
          if (st.type === 'outbound-rtp' && st.kind === 'audio') out.outboundBytes = st.bytesSent ?? 0;
          if (st.type.endsWith('candidate')) cands.set(st.id ?? st.address, st);
          if (st.type === 'candidate-pair' && st.state === 'succeeded' && st.nominated && st.currentRoundTripTime != null) {
            out.rttMs = Math.round(st.currentRoundTripTime * 1000);
            out.pair = { localId: st.localCandidateId, remoteId: st.remoteCandidateId };
          }
        });
        if (out.pair) {
          const t = (id) => cands.get(id)?.candidateType ?? '?';
          out.pair = `${t(out.pair.localId)}->${t(out.pair.remoteId)}`;
        }
        return out;
      });
      log(`${label} stats:`, JSON.stringify(s));
      return s;
    };
    const a1 = await sample(alice, 'alice t0');
    const b1 = await sample(bob, 'bob t0');
    await new Promise((r) => setTimeout(r, 3000));
    const a2 = await sample(alice, 'alice t1');
    const b2 = await sample(bob, 'bob t1');

    if (a2.inboundBytes <= a1.inboundBytes) throw new Error(`alice inbound bytes not flowing: ${a1.inboundBytes} -> ${a2.inboundBytes}`);
    if (b2.inboundBytes <= b1.inboundBytes) throw new Error(`bob inbound bytes not flowing: ${b1.inboundBytes} -> ${b2.inboundBytes}`);
    log(`alice inbound bytes +${a2.inboundBytes - a1.inboundBytes} over 3s (rtt ${a2.rttMs ?? '?'}ms, pair ${a2.pair ?? '?'})`);
    log(`bob inbound bytes +${b2.inboundBytes - b1.inboundBytes} over 3s (rtt ${b2.rttMs ?? '?'}ms, pair ${b2.pair ?? '?'})`);
    if (forceRelay) {
      for (const [who, s] of [['alice', a2], ['bob', b2]]) {
        if (!s.pair || !s.pair.startsWith('relay->relay')) {
          throw new Error(`${who}: relay-only ICE selected pair ${s.pair}, not relay->relay`);
        }
      }
    }

    // ---- speaking dots: the fake device's pulsed tone drives the analyser
    // path in a real browser; a dot must appear within the poll window ----
    const countDots = (page) => page.locator('[data-testid^="voice-speaking-"]').count();
    let speakingA = 0;
    let speakingB = 0;
    const dotDeadline = Date.now() + 12000;
    while (Date.now() < dotDeadline) {
      speakingA = Math.max(speakingA, await countDots(alice));
      speakingB = Math.max(speakingB, await countDots(bob));
      if (speakingA > 0 && speakingB > 0) break;
      await new Promise((r) => setTimeout(r, 200));
    }
    if (speakingA === 0) throw new Error('no speaking dot ever lit on alice (analyser path dead?)');
    if (speakingB === 0) throw new Error('no speaking dot ever lit on bob (analyser path dead?)');
    log(`speaking dots lit: alice sees ${speakingA}, bob sees ${speakingB}`);

    // ---- evidence screenshots ----
    const shotA = path.join(ART_DIR, 'voice-alice.png');
    const shotB = path.join(ART_DIR, 'voice-bob.png');
    await alice.screenshot({ path: shotA });
    await bob.screenshot({ path: shotB });
    log(`screenshots: ${shotA}, ${shotB}`);

    console.log('\n=== VOICE MEDIA E2E PASS ===');
    console.log(`mode=${turnUri ? (forceRelay ? 'turn-relay' : 'turn') : 'host'} room=${roomId} bytes alice +${a2.inboundBytes - a1.inboundBytes} / bob +${b2.inboundBytes - b1.inboundBytes} rtt ${a2.rttMs ?? '?'}ms/${b2.rttMs ?? '?'}ms pair ${a2.pair ?? '?'}/${b2.pair ?? '?'} speaking A:${speakingA} B:${speakingB}`);
  } finally {
    await browser.close().catch(() => {});
    await vite.close().catch(() => {});
    await stopAll();
  }
}

main()
  .then(() => process.exit(0))
  .catch((e) => {
    fail(e.stack ?? e.message);
    for (const f of ['/tmp/chirp_media_e2e_chat.log', '/tmp/chirp_media_e2e_social.log', '/tmp/chirp_media_e2e_voice.log']) {
      try { console.error(`--- ${f} (tail) ---\n` + fs.readFileSync(f, 'utf8').split('\n').slice(-15).join('\n')); } catch { /* none */ }
    }
    process.exit(1);
  });
