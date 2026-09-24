// WeChat minigame transport adapter: bridges wx.connectSocket's SocketTask
// (callback registration, { data } envelope objects) onto the WebSocketLike
// surface ChirpClient expects. Lets LayaAir / Cocos / plain minigame targets
// reuse the whole @chirp/protocol stack unchanged — the minigame runtime has
// no DOM constructors, so the DOM event types are satisfied with shaped
// literals; ChirpClient only ever reads ev.data / ev.code / ev.reason.
import type { WebSocketLike } from '../chirp_client';

/** Subset of the wx SocketTask the adapter relies on. */
export interface WxSocketTaskLike {
  onOpen(cb: () => void): void;
  onMessage(cb: (res: { data: ArrayBuffer | string }) => void): void;
  onClose(cb: (res: { code: number; reason: string }) => void): void;
  onError(cb: (res: { errMsg: string }) => void): void;
  send(opts: { data: ArrayBuffer | string }): void;
  close(opts?: { code?: number; reason?: string }): void;
}

/** Subset of the wx global the adapter relies on. */
export interface WxGlobalLike {
  connectSocket(opts: { url: string; protocols?: string[] }): WxSocketTaskLike;
}

/**
 * Returns a wsFactory for ChirpClientOptions:
 *
 * ```ts
 * const client = new ChirpClient({
 *   url: 'wss://chat.example.com/ws',
 *   wsFactory: createWxSocketFactory(wx),
 * });
 * ```
 *
 * Binary frames flow as ArrayBuffer in both directions; the text branch of
 * SocketTask.onMessage is forwarded untouched (ChirpClient's FrameDecoder
 * rejects it with the same error it would raise in a browser).
 */
export function createWxSocketFactory(
  wx: WxGlobalLike,
): (url: string) => WebSocketLike {
  return (url: string): WebSocketLike => {
    const task = wx.connectSocket({ url });
    const socket: WebSocketLike = {
      binaryType: 'arraybuffer',
      onopen: null,
      onmessage: null,
      onclose: null,
      onerror: null,
      send(data) {
        // wx wants a true ArrayBuffer; slice views that don't own their
        // buffer whole (Uint8Array frames normally do).
        const src = data.buffer as ArrayBuffer;
        const buf =
          data.byteOffset === 0 && data.byteLength === src.byteLength
            ? src
            : src.slice(data.byteOffset, data.byteOffset + data.byteLength);
        task.send({ data: buf });
      },
      close(code, reason) {
        task.close({ code: code ?? 1000, reason: reason ?? '' });
      },
    };
    task.onOpen(() => socket.onopen?.({ type: 'open' } as Event));
    task.onMessage((res) => {
      socket.onmessage?.({ data: res.data } as MessageEvent);
    });
    task.onClose((res) => {
      socket.onclose?.({ code: res.code, reason: res.reason } as CloseEvent);
    });
    task.onError((res) => {
      socket.onerror?.({ type: 'error', errMsg: res.errMsg } as unknown as Event);
    });
    return socket;
  };
}
