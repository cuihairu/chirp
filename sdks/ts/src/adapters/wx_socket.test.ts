import { LoginResponse } from '@chirp/proto/auth';
import { MsgID, Packet } from '@chirp/proto/gateway';
import { describe, expect, it } from 'vitest';
import { ChirpClient, WebSocketLike } from '../chirp_client';
import { FrameDecoder, encodeFrame } from '../frame';
import { LOGIN } from '../msg_map';
import { createWxSocketFactory } from './wx_socket';
import type { WxSocketTaskLike } from './wx_socket';

/**
 * Callback-registry fake of the wx SocketTask: the test drives the transport
 * with open/frame/close_/error and inspects `sent` / `closed`.
 */
class FakeSocketTask implements WxSocketTaskLike {
  onOpenCb: (() => void) | null = null;
  onMessageCb: ((res: { data: ArrayBuffer | string }) => void) | null = null;
  onCloseCb: ((res: { code: number; reason: string }) => void) | null = null;
  onErrorCb: ((res: { errMsg: string }) => void) | null = null;
  sent: (ArrayBuffer | string)[] = [];
  closed: { code?: number; reason?: string } | null = null;

  onOpen(cb: () => void): void {
    this.onOpenCb = cb;
  }
  onMessage(cb: (res: { data: ArrayBuffer | string }) => void): void {
    this.onMessageCb = cb;
  }
  onClose(cb: (res: { code: number; reason: string }) => void): void {
    this.onCloseCb = cb;
  }
  onError(cb: (res: { errMsg: string }) => void): void {
    this.onErrorCb = cb;
  }
  send(opts: { data: ArrayBuffer | string }): void {
    this.sent.push(opts.data);
  }
  close(opts?: { code?: number; reason?: string }): void {
    this.closed = opts ?? {};
  }

  // -- wx-side drivers -----------------------------------------------------
  open(): void {
    this.onOpenCb?.();
  }
  frame(bytes: ArrayBufferLike): void {
    this.onMessageCb?.({ data: bytes as ArrayBuffer });
  }
  closeFromServer(code: number, reason: string): void {
    this.onCloseCb?.({ code, reason });
  }
  error(errMsg: string): void {
    this.onErrorCb?.({ errMsg });
  }
}

class FakeWx {
  static instances: FakeSocketTask[] = [];
  static lastUrl = '';

  connectSocket(opts: { url: string }): WxSocketTaskLike {
    FakeWx.lastUrl = opts.url;
    const task = new FakeSocketTask();
    FakeWx.instances.push(task);
    return task;
  }
}

/** Socket + its task, guaranteed to be the same connection. */
const setup = (): { socket: WebSocketLike; task: FakeSocketTask } => {
  const socket = createWxSocketFactory(new FakeWx())('wss://chat.example/ws');
  return { socket, task: FakeWx.instances[FakeWx.instances.length - 1] };
};

describe('wx_socket adapter', () => {
  it('connects to the requested url', () => {
    createWxSocketFactory(new FakeWx())('wss://game.example/chirp');
    expect(FakeWx.lastUrl).toBe('wss://game.example/chirp');
  });

  it('fires nothing when no handler is assigned yet', () => {
    const { task } = setup();
    expect(() => {
      task.open();
      task.frame(new ArrayBuffer(0));
      task.closeFromServer(1000, '');
      task.error('boom');
    }).not.toThrow();
  });

  it('forwards the open event to a late-assigned onopen', () => {
    const { socket, task } = setup();
    const opened: boolean[] = [];
    socket.onopen = () => opened.push(true);
    task.open();
    expect(opened).toEqual([true]);
  });

  it('forwards ArrayBuffer messages through ev.data', () => {
    const { socket, task } = setup();
    const seen: unknown[] = [];
    socket.onmessage = (ev) => seen.push(ev.data);
    const payload = new Uint8Array([1, 2, 3]).buffer;
    task.frame(payload);
    expect(seen).toEqual([payload]);
  });

  it('forwards close code and reason', () => {
    const { socket, task } = setup();
    const closes: { code: number; reason: string }[] = [];
    socket.onclose = (ev) => closes.push({ code: ev.code, reason: ev.reason });
    task.closeFromServer(1006, 'server quit');
    expect(closes).toEqual([{ code: 1006, reason: 'server quit' }]);
  });

  it('forwards errors with the wx errMsg carried along', () => {
    const { socket, task } = setup();
    const errs: unknown[] = [];
    socket.onerror = (ev) => errs.push(ev);
    task.error('connect fail');
    expect((errs[0] as unknown as { errMsg: string }).errMsg).toBe(
      'connect fail',
    );
  });

  it('sends full-view Uint8Array frames as their whole buffer', () => {
    const { socket, task } = setup();
    const wire = new Uint8Array([9, 8, 7]);
    socket.send(wire);
    expect(new Uint8Array(task.sent[0] as ArrayBuffer)).toEqual(wire);
  });

  it('slices sub-views so only the visible bytes go on the wire', () => {
    const { socket, task } = setup();
    const backing = new Uint8Array([1, 2, 3, 4, 5]);
    socket.send(backing.subarray(1, 4));
    expect(new Uint8Array(task.sent[0] as ArrayBuffer)).toEqual(
      new Uint8Array([2, 3, 4]),
    );
  });

  it('close() defaults to code 1000 with empty reason', () => {
    const { socket, task } = setup();
    socket.close();
    expect(task.closed).toEqual({ code: 1000, reason: '' });
    socket.close(3001, 'switching');
    expect(task.closed).toEqual({ code: 3001, reason: 'switching' });
  });

  it('drives a real ChirpClient login round-trip over the adapter', async () => {
    const client = new ChirpClient({
      url: 'wss://chat.example/ws',
      wsFactory: createWxSocketFactory(new FakeWx()),
      requestTimeoutMs: 500,
    });
    // Connection and authentication are separate phases: connect() opens the
    // socket, the api layer then issues the LOGIN request over it.
    const opening = client.connect();
    const task = FakeWx.instances[FakeWx.instances.length - 1];
    expect(task.sent).toEqual([]);
    task.open();
    await opening;
    expect(client.status).toBe('connected');

    const pending = client.request(LOGIN, {
      token: 'wx-user',
      deviceId: 'd1',
      platform: 'wx',
    });

    // First wire bytes must decode as the LOGIN handshake frame — decoded
    // through the same FrameDecoder path the client uses, so the test never
    // assumes the send buffer is an exact (non-pooled) ArrayBuffer.
    const decoder = new FrameDecoder();
    const [frame] = decoder.feed(new Uint8Array(task.sent[0] as ArrayBuffer));
    const req = Packet.decode(frame);
    expect(req.msgId).toBe(MsgID.LOGIN_REQ);

    // Server accepts; the correlated response must resolve the request.
    const resp = LoginResponse.encode(
      LoginResponse.fromPartial({ code: 0, userId: 'wx-user' }),
    ).finish();
    // slice() gives the exact standalone ArrayBuffer a real wx runtime
    // would hand the adapter, rather than a view into a shared buffer.
    task.frame(encodeFrame(
      Packet.encode(Packet.fromPartial({ msgId: MsgID.LOGIN_RESP, sequence: req.sequence, body: resp })).finish(),
    ).slice().buffer as ArrayBuffer);
    const body = await pending;
    expect(body).toBeTruthy();
    client.disconnect();
  });
});
