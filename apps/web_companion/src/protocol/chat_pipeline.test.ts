import { LoginRequest, LoginResponse } from '@chirp/proto/auth';
import { ChannelType, ChatMessage, MsgType, SendMessageRequest, SendMessageResponse } from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { KickNotify } from '@chirp/proto/auth';
import { MsgID, Packet } from '@chirp/proto/gateway';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ChirpClient, WebSocketLike } from './chirp_client';
import { encodeFrame } from './frame';
import { ChatPipeline } from './chat_pipeline';
import { MemoryMessageStore } from './hooks';

/**
 * Same fake as chirp_client.test.ts: the test drives the server side
 * (serverOpen / serverFrame) and inspects `sent` for what hit the wire.
 */
class FakeSocket implements WebSocketLike {
  binaryType = 'blob';
  onopen: ((ev: Event) => void) | null = null;
  onmessage: ((ev: MessageEvent) => void) | null = null;
  onclose: ((ev: CloseEvent) => void) | null = null;
  onerror: ((ev: Event) => void) | null = null;
  sent: Uint8Array[] = [];
  closed = false;

  static instances: FakeSocket[] = [];

  constructor(public url: string) {
    FakeSocket.instances.push(this);
  }

  send(data: ArrayBufferView): void {
    this.sent.push(new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice());
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.onclose?.(new CloseEvent('close'));
  }

  serverOpen(): void {
    this.onopen?.(new Event('open'));
  }

  serverFrame(msgId: MsgID, sequence: number, body: Uint8Array): void {
    const packet = Packet.encode(Packet.fromPartial({ msgId, sequence, body })).finish();
    this.onmessage?.({ data: encodeFrame(packet) } as unknown as MessageEvent);
  }

  lastSentPacket(): Packet {
    const frame = this.sent.at(-1)!;
    const length = new DataView(frame.buffer).getUint32(0, false);
    return Packet.decode(frame.slice(4, 4 + length));
  }
}

async function makeHarness(run: (client: ChirpClient, pipeline: ChatPipeline, ws: FakeSocket) => Promise<void>): Promise<void> {
  const client = new ChirpClient({
    url: 'ws://chat.test/ws',
    wsFactory: (url) => new FakeSocket(url),
    reconnectBaseMs: 5,
    reconnectMaxMs: 10,
  });
  const pipeline = new ChatPipeline({
    conn: client,
    selfId: () => 'u1',
    deviceId: () => 'dev-web',
  });
  pipeline.setStore(new MemoryMessageStore());
  pipeline.start();
  const opening = client.connect();
  FakeSocket.instances.at(-1)!.serverOpen();
  await opening;
  await run(client, pipeline, FakeSocket.instances.at(-1)!);
}

/** Settles whatever request the client sent last (login, send, …). */
function settle(ws: FakeSocket, msgId: MsgID, body: Uint8Array): void {
  ws.serverFrame(msgId, ws.lastSentPacket().sequence, body);
}

function loginResp(code: ErrorCode, userId: string): Uint8Array {
  return LoginResponse.encode(LoginResponse.fromPartial({ code, userId })).finish();
}

function sendResp(code: ErrorCode): Uint8Array {
  return SendMessageResponse.encode(SendMessageResponse.fromPartial({ code })).finish();
}

/** Drains the pending microtask chain (await hops inside login()'s renew path). */
async function flushMicrotasks(times = 10): Promise<void> {
  for (let i = 0; i < times; i++) await Promise.resolve();
}

function chatMsg(id: string, senderId: string, content: string, timestamp: number): Uint8Array {
  return ChatMessage.encode(
    ChatMessage.fromPartial({
      messageId: id,
      senderId,
      channelType: ChannelType.PRIVATE,
      channelId: `${senderId}|u1`,
      content: new TextEncoder().encode(content),
      timestamp,
    }),
  ).finish();
}

beforeEach(() => {
  vi.useFakeTimers();
  FakeSocket.instances = [];
});

afterEach(() => {
  vi.useRealTimers();
});

describe('ChatPipeline send pipeline', () => {
  it('sends to the wire with normalized private channel and reply id', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      const pending = pipeline.login('u1');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.OK, 'u1'));
      expect(await pending).toBe(ErrorCode.OK);

      const sending = pipeline.send(
        { channelType: ChannelType.PRIVATE, receiverId: 'peer9', replyToMessageId: 'm-42' },
        'gg',
      );
      const req = SendMessageRequest.decode(ws.lastSentPacket().body);
      expect(ws.lastSentPacket().msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      expect(req.senderId).toBe('u1');
      expect(req.receiverId).toBe('peer9');
      expect(req.channelType).toBe(ChannelType.PRIVATE);
      expect(req.channelId).toBe('peer9|u1'); // sorted pair, server parity
      expect(req.msgType).toBe(MsgType.TEXT);
      expect(req.replyToMessageId).toBe('m-42');
      expect(new TextDecoder().decode(req.content)).toBe('gg');

      settle(ws, MsgID.SEND_MESSAGE_RESP, sendResp(ErrorCode.OK));
      const resp = await sending;
      expect(resp.code).toBe(ErrorCode.OK);
    });
  });

  it('passes /-text through when no handlers are registered', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      const sending = pipeline.send(
        { channelType: ChannelType.PRIVATE, receiverId: 'p' },
        '/dance',
      );
      expect(ws.lastSentPacket().msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      settle(ws, MsgID.SEND_MESSAGE_RESP, sendResp(ErrorCode.OK));
      await sending;
    });
  });

  it('consumes /-commands locally: hit and miss both stay off the wire', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      const hit = pipeline.registerCommand({ name: 'trade', execute: () => true });
      const miss = pipeline.registerCommand({ name: 'trade', execute: () => false });
      for (const text of ['/trade alice 100', '/nothing here']) {
        const before = ws.sent.length;
        await expect(
          pipeline.send({ channelType: ChannelType.PRIVATE, receiverId: 'p' }, text),
        ).rejects.toMatchObject({ kind: 'blocked' });
        expect(ws.sent.length).toBe(before);
      }
      hit();
      miss();
    });
  });

  it('interceptor rewrites reach the wire; drops and throws never do', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      pipeline.setInterceptor({
        onBeforeSend: (req) => {
          req.content = new TextEncoder().encode('rewritten');
          return true;
        },
      });
      const sending = pipeline.send({ channelType: ChannelType.PRIVATE, receiverId: 'p' }, 'raw');
      const req = SendMessageRequest.decode(ws.lastSentPacket().body);
      expect(new TextDecoder().decode(req.content)).toBe('rewritten');
      settle(ws, MsgID.SEND_MESSAGE_RESP, sendResp(ErrorCode.OK));
      await sending;

      pipeline.setInterceptor({ onBeforeSend: () => false });
      const before = ws.sent.length;
      await expect(
        pipeline.send({ channelType: ChannelType.PRIVATE, receiverId: 'p' }, 'x'),
      ).rejects.toMatchObject({ kind: 'blocked' });
      expect(ws.sent.length).toBe(before);

      pipeline.setInterceptor({
        onBeforeSend: () => {
          throw new Error('hook bug');
        },
      });
      await expect(
        pipeline.send({ channelType: ChannelType.PRIVATE, receiverId: 'p' }, 'x'),
      ).rejects.toMatchObject({ kind: 'blocked' });
      expect(ws.sent.length).toBe(before);
    });
  });

  it('validates in the C++ order: connection first, then arguments', async () => {
    await makeHarness(async (client, pipeline) => {
      // Argument checks fire while connected (C# ArgumentException parity).
      await expect(
        pipeline.send({ channelType: ChannelType.PRIVATE }, 'x'),
      ).rejects.toThrow(TypeError);
      await expect(
        pipeline.send({ channelType: ChannelType.TEAM }, 'x'),
      ).rejects.toThrow(TypeError);
      await expect(
        pipeline.send({ channelType: ChannelType.PRIVATE, receiverId: 'p' }, ''),
      ).rejects.toThrow(RangeError);

      client.disconnect(); // …but the status check wins over all of them
      await expect(
        pipeline.send({ channelType: ChannelType.PRIVATE }, ''),
      ).rejects.toMatchObject({ kind: 'closed' });
    });
  });

  it('archives both directions newest-first; unread is 0 without tracking', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      const pending = pipeline.login('u1');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.OK, 'u1'));
      await pending;

      const sending = pipeline.send(
        { channelType: ChannelType.PRIVATE, receiverId: 'p9' },
        'out',
      );
      settle(ws, MsgID.SEND_MESSAGE_RESP, sendResp(ErrorCode.OK));
      await sending;

      ws.serverFrame(MsgID.CHAT_MESSAGE_NOTIFY, 0, chatMsg('in-1', 'p9', 'in', Date.now()));

      const history = pipeline.loadHistory(ChannelType.PRIVATE, 'p9|u1', 10);
      expect(history.map((m) => new TextDecoder().decode(m.content))).toEqual(['in', 'out']);
      expect(pipeline.unreadCount(ChannelType.PRIVATE, 'p9|u1')).toBe(0);
    });
  });

  it('receive pipeline: listeners get the message; drop kills the fan-out only', async () => {
    await makeHarness(async (client, pipeline, ws) => {
      const seen: string[] = [];
      const rawCount = { n: 0 };
      const offRaw = client.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, () => rawCount.n++);
      const off = pipeline.addListener({
        onMessageReceived: (msg) => seen.push(new TextDecoder().decode(msg.content)),
      });

      ws.serverFrame(MsgID.CHAT_MESSAGE_NOTIFY, 0, chatMsg('m1', 'p9', 'hello', 5));
      expect(seen).toEqual(['hello']);
      expect(rawCount.n).toBe(1); // raw notify subscribers unaffected

      pipeline.setInterceptor({ onBeforeReceive: () => false });
      ws.serverFrame(MsgID.CHAT_MESSAGE_NOTIFY, 0, chatMsg('m2', 'p9', 'dropped', 6));
      expect(seen).toEqual(['hello']); // listener never saw the drop
      expect(rawCount.n).toBe(2); // …but raw subscribers still did

      off();
      offRaw();
    });
  });
});

describe('ChatPipeline login orchestration', () => {
  it('prefers the explicit token, then falls back to the provider', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      const sentTokens = () => LoginRequest.decode(ws.lastSentPacket().body).token;
      pipeline.setProvider({ getToken: () => 'from-provider' });

      const p1 = pipeline.login('u1', 'explicit-token');
      expect(sentTokens()).toBe('explicit-token');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.OK, 'u1'));
      await p1;

      const p2 = pipeline.login('u1');
      expect(sentTokens()).toBe('from-provider');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.OK, 'u1'));
      await p2;
    });
  });

  it('renews exactly once on AUTH_FAILED and fans out the terminal result', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      let renews = 0;
      const authResults: Array<[number, string]> = [];
      const loginResults: Array<[number, string]> = [];
      pipeline.setProvider({
        getToken: () => 'stale',
        renewToken: () => {
          renews++;
          return Promise.resolve('fresh');
        },
        onAuthResult: (code, userId) => authResults.push([code, userId]),
      });
      pipeline.addListener({ onLoginResult: (code, userId) => loginResults.push([code, userId]) });

      const p = pipeline.login('u1');
      expect(LoginRequest.decode(ws.lastSentPacket().body).token).toBe('stale');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.AUTH_FAILED, ''));
      await flushMicrotasks();
      expect(LoginRequest.decode(ws.lastSentPacket().body).token).toBe('fresh');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.OK, 'u1'));
      expect(await p).toBe(ErrorCode.OK);
      expect(renews).toBe(1);
      expect(authResults).toEqual([[ErrorCode.OK, 'u1']]);
      expect(loginResults).toEqual([[ErrorCode.OK, 'u1']]);
    });
  });

  it('renewal failing again ends the login at AUTH_FAILED, once', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      pipeline.setProvider({
        getToken: () => 'stale',
        renewToken: () => Promise.resolve('also-stale'),
      });
      const p = pipeline.login('u1');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.AUTH_FAILED, ''));
      await flushMicrotasks();
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.AUTH_FAILED, ''));
      expect(await p).toBe(ErrorCode.AUTH_FAILED);
      expect(ws.sent.length).toBe(2); // two LOGINs, no third
    });
  });

  it('a declining provider ends the login immediately', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      pipeline.setProvider({
        getToken: () => 'stale',
        renewToken: () => Promise.resolve(null),
      });
      const p = pipeline.login('u1');
      settle(ws, MsgID.LOGIN_RESP, loginResp(ErrorCode.AUTH_FAILED, ''));
      await flushMicrotasks();
      expect(await p).toBe(ErrorCode.AUTH_FAILED);
    });
  });
});

describe('ChatPipeline listener wiring', () => {
  it('sees reconnecting/reconnected and the kick reason', async () => {
    await makeHarness(async (_client, pipeline, ws) => {
      const events: string[] = [];
      let lastDelay = 0;
      let kickReason = '';
      const off = pipeline.addListener({
        onReconnecting: (attempt, delayMs) => {
          events.push(`reconnecting-${attempt}`);
          lastDelay = delayMs;
        },
        onReconnected: () => events.push('reconnected'),
        onKicked: (reason) => {
          kickReason = reason;
          events.push('kicked');
        },
      });

      ws.close(); // transport drop → backoff scheduled synchronously
      expect(events).toEqual(['reconnecting-1']);
      expect(lastDelay).toBeGreaterThan(0);

      await vi.advanceTimersByTimeAsync(20); // fires the 5-10ms reconnect timer
      FakeSocket.instances.at(-1)!.serverOpen();
      expect(events).toEqual(['reconnecting-1', 'reconnected']);

      FakeSocket.instances.at(-1)!.serverFrame(
        MsgID.KICK_NOTIFY,
        0,
        KickNotify.encode(KickNotify.fromPartial({ reason: 'top-up elsewhere' })).finish(),
      );
      expect(kickReason).toBe('top-up elsewhere');
      off();
    });
  });

  it('forwards cleanup to the store', async () => {
    await makeHarness(async (_client, pipeline) => {
      const cleanups: number[] = [];
      pipeline.setStore({
        save: () => undefined,
        load: () => [],
        cleanup: (olderThanMs) => cleanups.push(olderThanMs),
      });
      pipeline.cleanup(1234);
      expect(cleanups).toEqual([1234]);
    });
  });
});
