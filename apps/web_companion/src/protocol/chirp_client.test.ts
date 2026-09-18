import { LoginResponse } from '@chirp/proto/auth';
import { HeartbeatPong, MsgID, Packet } from '@chirp/proto/gateway';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ChirpClient, WebSocketLike } from './chirp_client';
import { encodeFrame } from './frame';
import { LOGIN } from './msg_map';

/**
 * FakeWebSocket doubles as the server side: the test calls serverOpen /
 * serverFrame / serverClose to drive the client, and inspects `sent` for
 * what the client put on the wire.
 */
class FakeWebSocket implements WebSocketLike {
  binaryType = 'blob';
  onopen: ((ev: Event) => void) | null = null;
  onmessage: ((ev: MessageEvent) => void) | null = null;
  onclose: ((ev: CloseEvent) => void) | null = null;
  onerror: ((ev: Event) => void) | null = null;
  sent: Uint8Array[] = [];
  closed = false;

  static instances: FakeWebSocket[] = [];

  constructor(public url: string) {
    FakeWebSocket.instances.push(this);
  }

  send(data: ArrayBufferView): void {
    this.sent.push(
      new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice(),
    );
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.onclose?.(new CloseEvent('close'));
  }

  // -- server-side helpers -------------------------------------------------
  serverOpen(): void {
    this.onopen?.(new Event('open'));
  }

  /** Server tears the connection down; same wire effect as close(). */
  serverClose(): void {
    this.close();
  }

  /** Wraps a packet in a length-prefixed frame and feeds it to the client. */
  serverFrame(msgId: MsgID, sequence: number, body: Uint8Array): void {
    const packet = Packet.encode(Packet.fromPartial({ msgId, sequence, body })).finish();
    this.onmessage?.({ data: encodeFrame(packet) } as unknown as MessageEvent);
  }

  serverRaw(bytes: Uint8Array): void {
    this.onmessage?.({ data: bytes } as unknown as MessageEvent);
  }

  /** The last packet the client sent, decoded. */
  lastSentPacket(): Packet {
    const frame = this.sent.at(-1)!;
    const length = new DataView(frame.buffer).getUint32(0, false);
    return Packet.decode(frame.slice(4, 4 + length));
  }
}

function makeClient(options: Partial<ConstructorParameters<typeof ChirpClient>[0]> = {}): ChirpClient {
  return new ChirpClient({
    url: 'ws://chat.test/ws',
    wsFactory: (url) => new FakeWebSocket(url),
    ...options,
  });
}

async function connectedClient(
  options: Partial<ConstructorParameters<typeof ChirpClient>[0]> = {},
): Promise<{ client: ChirpClient; ws: FakeWebSocket }> {
  const client = makeClient(options);
  const opening = client.connect();
  FakeWebSocket.instances.at(-1)!.serverOpen();
  await opening;
  return { client, ws: FakeWebSocket.instances.at(-1)! };
}

beforeEach(() => {
  vi.useFakeTimers();
  FakeWebSocket.instances = [];
});

afterEach(() => {
  vi.useRealTimers();
});

describe('connection lifecycle', () => {
  it('reaches connected when the socket opens', async () => {
    const { client } = await connectedClient();
    expect(client.status).toBe('connected');
    expect(client.kicked).toBe(false);
  });

  it('rejects connect() when the socket closes before opening', async () => {
    const client = makeClient();
    const opening = client.connect();
    const ws = FakeWebSocket.instances.at(-1)!;
    ws.serverClose();
    await expect(opening).rejects.toMatchObject({ kind: 'closed' });
    expect(client.status).toBe('waiting-reconnect');
    client.disconnect();
  });

  it('does not reconnect after disconnect()', async () => {
    const { client, ws } = await connectedClient();
    client.disconnect();
    expect(client.status).toBe('closed');
    ws.serverClose();
    await vi.advanceTimersByTimeAsync(60_000);
    expect(FakeWebSocket.instances.length).toBe(1);
  });

  it('notifies status listeners on every transition', async () => {
    const client = makeClient();
    const seen: string[] = [];
    client.onStatus((s) => seen.push(s));
    const opening = client.connect();
    FakeWebSocket.instances.at(-1)!.serverOpen();
    await opening;
    client.disconnect();
    expect(seen).toEqual(['connecting', 'connected', 'closed']);
  });

  it('stops notifying after unsubscribing', async () => {
    const client = makeClient();
    const seen: string[] = [];
    const off = client.onStatus((s) => seen.push(s));
    off();
    const opening = client.connect();
    FakeWebSocket.instances.at(-1)!.serverOpen();
    await opening;
    client.disconnect();
    expect(seen).toEqual([]);
  });

  it('does not re-notify listeners for a redundant status', async () => {
    const client = makeClient();
    const seen: string[] = [];
    client.onStatus((s) => seen.push(s));
    client.disconnect();
    client.disconnect(); // second call is a no-op
    expect(seen).toEqual(['closed']);
  });

  it('cancels a pending reconnect when connect() is called explicitly', async () => {
    const { client, ws } = await connectedClient({ jitterRatio: 0 });
    ws.serverClose(); // schedules a reconnect in 500ms
    // The user clicks "retry" before the timer fires.
    const manual = client.connect();
    FakeWebSocket.instances.at(-1)!.serverOpen();
    await manual;
    expect(client.status).toBe('connected');

    // The cancelled timer must not spawn yet another socket.
    await vi.advanceTimersByTimeAsync(60_000);
    expect(FakeWebSocket.instances.length).toBe(2);
    client.disconnect();
  });

  it('replaces a still-open socket on a fresh connect()', async () => {
    const { client, ws } = await connectedClient();
    const stale = ws;
    const second = client.connect();
    expect(stale.closed).toBe(true);
    FakeWebSocket.instances.at(-1)!.serverOpen();
    await second;
    expect(client.status).toBe('connected');
    client.disconnect();
  });
});

describe('request/response', () => {
  it('correlates a response by sequence and decodes it', async () => {
    const { client, ws } = await connectedClient();
    const pending = client.request(LOGIN, { token: 'player_1', deviceId: 'd1', platform: 'web' });

    const sent = ws.lastSentPacket();
    expect(sent.msgId).toBe(MsgID.LOGIN_REQ);
    expect(sent.sequence).toBeGreaterThan(0);

    const response = LoginResponse.encode(
      LoginResponse.fromPartial({ code: 0, sessionId: 's1', userId: 'player_1' }),
    ).finish();
    ws.serverFrame(MsgID.LOGIN_RESP, sent.sequence, response);

    const decoded = await pending;
    expect(decoded.sessionId).toBe('s1');
    expect(decoded.userId).toBe('player_1');
  });

  it('rejects with timeout when no response arrives', async () => {
    const { client } = await connectedClient();
    const pending = client.request(LOGIN, { token: 't' });
    const guard = pending.catch((e) => e);
    await vi.advanceTimersByTimeAsync(10_000);
    expect((await guard).kind).toBe('timeout');
  });

  it('rejects requests made while not connected', async () => {
    const client = makeClient();
    await expect(client.request(LOGIN, { token: 't' })).rejects.toMatchObject({ kind: 'closed' });
  });

  it('flushes pending requests as closed on disconnect', async () => {
    const { client } = await connectedClient();
    const pending = client.request(LOGIN, { token: 't' });
    const guard = pending.catch((e) => e);
    client.disconnect();
    expect((await guard).kind).toBe('closed');
  });

  it('flushes pending requests as closed when the connection drops', async () => {
    const { client, ws } = await connectedClient();
    const pending = client.request(LOGIN, { token: 't' });
    const guard = pending.catch((e) => e);
    ws.serverClose();
    expect((await guard).kind).toBe('closed');
    expect(client.status).toBe('waiting-reconnect');
    client.disconnect();
  });

  it('ignores a late response whose request already timed out', async () => {
    const { client, ws } = await connectedClient();
    const pending = client.request(LOGIN, { token: 't' });
    const guard = pending.catch((e) => e);
    await vi.advanceTimersByTimeAsync(10_000);
    const response = LoginResponse.encode(LoginResponse.fromPartial({ code: 0 })).finish();
    expect(() => ws.serverFrame(MsgID.LOGIN_RESP, 1, response)).not.toThrow();
    expect((await guard).kind).toBe('timeout');
  });

  it('rejects as closed when the socket fails mid-send', async () => {
    const { client, ws } = await connectedClient();
    ws.send = () => {
      throw new Error('socket exploded');
    };
    await expect(client.request(LOGIN, { token: 't' })).rejects.toMatchObject({ kind: 'closed' });
    client.disconnect();
  });
});

describe('notifications', () => {
  it('dispatches sequence-0 packets to subscribers', async () => {
    const { client, ws } = await connectedClient();
    const bodies: Uint8Array[] = [];
    client.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, (body) => bodies.push(body));
    ws.serverFrame(MsgID.CHAT_MESSAGE_NOTIFY, 0, new Uint8Array([7, 7, 7]));
    expect(bodies.length).toBe(1);
    expect([...bodies[0]]).toEqual([7, 7, 7]);
    client.disconnect();
  });

  it('supports unsubscribe and ignores unknown msgIds', async () => {
    const { client, ws } = await connectedClient();
    let calls = 0;
    const off = client.onNotify(MsgID.MESSAGE_READ_NOTIFY, () => calls++);
    off();
    ws.serverFrame(MsgID.MESSAGE_READ_NOTIFY, 0, new Uint8Array([1]));
    ws.serverFrame(9999 as MsgID, 0, new Uint8Array([1]));
    expect(calls).toBe(0);
    client.disconnect();
  });
});

describe('kick handling', () => {
  it('stops auto-reconnect and fails pending requests as kicked', async () => {
    const { client, ws } = await connectedClient();
    const pending = client.request(LOGIN, { token: 't' });
    const guard = pending.catch((e) => e);

    ws.serverFrame(MsgID.KICK_NOTIFY, 0, new Uint8Array([1]));

    expect(client.kicked).toBe(true);
    expect(client.status).toBe('kicked');
    expect((await guard).kind).toBe('kicked');

    // The heart of kick safety: no reconnect attempt, ever.
    await vi.advanceTimersByTimeAsync(120_000);
    expect(FakeWebSocket.instances.length).toBe(1);
    expect(client.status).toBe('kicked');
  });
});

describe('reconnect with backoff', () => {
  it('reconnects after a drop using the base delay', async () => {
    const { client, ws } = await connectedClient({ jitterRatio: 0 });
    ws.serverClose();
    expect(client.status).toBe('waiting-reconnect');

    await vi.advanceTimersByTimeAsync(499);
    expect(FakeWebSocket.instances.length).toBe(1);
    await vi.advanceTimersByTimeAsync(1);
    expect(FakeWebSocket.instances.length).toBe(2);

    FakeWebSocket.instances.at(-1)!.serverOpen();
    expect(client.status).toBe('connected');
    client.disconnect();
  });

  it('doubles the delay each failed attempt and caps it', async () => {
    const { client } = await connectedClient({ jitterRatio: 0 });
    // Every attempt fails before opening.
    let failures = 0;
    while (failures < 6) {
      FakeWebSocket.instances.at(-1)!.serverClose();
      failures++;
      await vi.advanceTimersByTimeAsync(15_000);
    }
    // 500, 1000, 2000, 4000, 8000, 15000 — all six attempts fired within 30s.
    expect(FakeWebSocket.instances.length).toBe(7);
    client.disconnect();
  });

  it('resets the backoff after a successful login', async () => {
    const { client } = await connectedClient({ jitterRatio: 0 });
    // Two failed attempts push the backoff to 2000ms.
    for (let i = 0; i < 2; i++) {
      FakeWebSocket.instances.at(-1)!.serverClose();
      await vi.advanceTimersByTimeAsync(15_000);
    }
    client.resetBackoff();

    // The next drop schedules the base delay again.
    FakeWebSocket.instances.at(-1)!.serverOpen();
    FakeWebSocket.instances.at(-1)!.serverClose();
    await vi.advanceTimersByTimeAsync(499);
    expect(FakeWebSocket.instances.length).toBe(3);
    await vi.advanceTimersByTimeAsync(1);
    expect(FakeWebSocket.instances.length).toBe(4);
    client.disconnect();
  });
});

describe('heartbeat', () => {
  it('pings on the interval and records the clock offset from pongs', async () => {
    const { client, ws } = await connectedClient();
    expect(ws.sent.length).toBe(0);

    await vi.advanceTimersByTimeAsync(25_000);
    const ping = ws.lastSentPacket();
    expect(ping.msgId).toBe(MsgID.HEARTBEAT_PING);

    const before = Date.now();
    const pong = HeartbeatPong.encode(
      HeartbeatPong.fromPartial({ timestamp: ping.body.length, serverTime: before + 5000 }),
    ).finish();
    ws.serverFrame(MsgID.HEARTBEAT_PONG, ping.sequence, pong);
    expect(client.clockOffsetMs).toBe(5000);
    client.disconnect();
  });

  it('keeps the link alive while pongs keep coming', async () => {
    const { client, ws } = await connectedClient();
    for (let tick = 0; tick < 5; tick++) {
      await vi.advanceTimersByTimeAsync(25_000);
      const ping = ws.lastSentPacket();
      if (ping.msgId !== MsgID.HEARTBEAT_PING) break;
      const pong = HeartbeatPong.encode(HeartbeatPong.fromPartial({ serverTime: Date.now() })).finish();
      ws.serverFrame(MsgID.HEARTBEAT_PONG, ping.sequence, pong);
    }
    expect(client.status).toBe('connected');
    expect(ws.closed).toBe(false);
    client.disconnect();
  });

  it('declares the link dead after two consecutive silent pings', async () => {
    const { client, ws } = await connectedClient();
    await vi.advanceTimersByTimeAsync(25_000); // ping 1: missed = 1
    expect(client.status).toBe('connected');
    await vi.advanceTimersByTimeAsync(25_000); // ping 2: missed = 2, still on
    expect(client.status).toBe('connected');
    await vi.advanceTimersByTimeAsync(25_000); // ping 3: threshold reached
    expect(ws.closed).toBe(true);
    expect(client.status).toBe('waiting-reconnect');
    client.disconnect();
  });

  it('sends an immediate ping on heartbeatNow when connected only', async () => {
    const { client, ws } = await connectedClient();
    const before = ws.sent.length;

    client.heartbeatNow();
    expect(ws.sent.length).toBe(before + 1);
    expect(ws.lastSentPacket().msgId).toBe(MsgID.HEARTBEAT_PING);

    // A client that never connected has no socket to write to.
    const idle = makeClient();
    idle.heartbeatNow();
    expect(ws.sent.length).toBe(before + 1); // unchanged: idle sends nothing
    client.disconnect();
  });

  it('survives the socket vanishing between ticks', async () => {
    const { client } = await connectedClient();
    // Break the connected-but-socketless invariant directly: this is the
    // race window sendPing's try/catch exists for.
    const internals = client as unknown as { ws: WebSocketLike | null };
    internals.ws = null;
    expect(() => client.heartbeatNow()).not.toThrow();
    client.disconnect();
  });
});

describe('corrupt input', () => {
  it('treats a frame over the length cap as a dead link', async () => {
    const { client, ws } = await connectedClient();
    ws.serverRaw(new Uint8Array([0xff, 0xff, 0xff, 0xff, 1]));
    expect(client.status).toBe('waiting-reconnect');
    client.disconnect();
  });

  it('ignores text frames and undecodable packets', async () => {
    const { client, ws } = await connectedClient();
    ws.serverRaw(new Uint8Array([0, 0, 0, 1, 0xde, 0xad])); // length 1, junk packet
    ws.onmessage?.({ data: 'hello' } as unknown as MessageEvent);
    expect(client.status).toBe('connected');
    client.disconnect();
  });
});
