import webSocket from '@ohos.net.webSocket';
import type { WebSocketLike } from '@chirp/app-protocol/chirp_client';

/**
 * WebSocketLike 的鸿蒙实现（@ohos.net.webSocket）：浏览器端默认工厂
 * `new WebSocket(url)` 在构造时即发起连接，协议核（ChirpClient）依赖该
 * 生命周期——工厂返回后才赋 onopen/onmessage/onclose/onerror——所以这里
 * 必须在构造函数内自启 connect，否则连接永远不会被拨出。
 *
 * 事件对象走「最小字面量 + cast 到契约事件形态」：协议核只读
 * message.data（chirp_client.ts:198），open/close/error 的载荷被整体
 * 忽略，而鸿蒙没有 DOM Event 构造器可造真事件。事件类型全部经
 * Parameters<> 从契约提取，不在本文件具名 DOM 类型。
 */
type OpenEvent = Parameters<NonNullable<WebSocketLike['onopen']>>[0];
type MessageEventShape = Parameters<NonNullable<WebSocketLike['onmessage']>>[0];
type CloseEventShape = Parameters<NonNullable<WebSocketLike['onclose']>>[0];
type ErrorEventShape = Parameters<NonNullable<WebSocketLike['onerror']>>[0];

export class HarmonyWebSocket implements WebSocketLike {
  binaryType: string = 'arraybuffer';
  onopen: ((ev: OpenEvent) => void) | null = null;
  onmessage: ((ev: MessageEventShape) => void) | null = null;
  onclose: ((ev: CloseEventShape) => void) | null = null;
  onerror: ((ev: ErrorEventShape) => void) | null = null;

  private readonly ws: webSocket.WebSocket;
  private closedByUs: boolean = false;

  constructor(url: string) {
    this.ws = webSocket.createWebSocket();
    this.wireEvents();
    // fire-and-forget：失败走 onerror/onclose（协议核的状态机负责重连）。
    this.ws.connect(url, (err: Error | undefined) => {
      if (err) this.emitError(err.message);
    });
  }

  send(data: ArrayBufferView): void {
    // ArrayBufferView → ArrayBuffer 拷贝（view 可能只是 buffer 的一段）。
    const view = data as Uint8Array;
    const buffer = view.buffer.slice(view.byteOffset, view.byteOffset + view.byteLength) as ArrayBuffer;
    this.ws.send(buffer, (err: Error | undefined) => {
      if (err) this.emitError(err.message);
    });
  }

  close(code?: number, reason?: string): void {
    this.closedByUs = true;
    // 未开成的 socket close 会报错：close 语义是「不再期待任何事件」，
    // 协议核已按 closed 收尾，错误无需外抛。
    this.ws.close(code ?? 1000, reason ?? '', (_err: Error | undefined) => {});
  }

  private wireEvents(): void {
    this.ws.on('open', () => {
      this.onopen?.({} as OpenEvent);
    });
    this.ws.on('message', (_err: Error | undefined, value: string | ArrayBuffer) => {
      if (!this.onmessage) return;
      // 文本帧仅可能来自服务器的非协议面（协议核只发二进制）；同样按
      // 字节面交给 framing，不在适配层做第二套解码。
      const data: ArrayBuffer =
        typeof value === 'string' ? encodeToBuffer(value) : value;
      this.onmessage({ data } as MessageEventShape);
    });
    this.ws.on('close', (_err: Error | undefined, value: webSocket.CloseResult) => {
      const handler = this.onclose;
      this.onclose = null;
      handler?.({
        code: value?.code ?? 1006,
        reason: value?.reason ?? '',
        wasClean: this.closedByUs,
      } as CloseEventShape);
    });
    this.ws.on('error', (err: Error | undefined) => {
      this.emitError(err?.message ?? 'websocket error');
    });
  }

  private emitError(message: string): void {
    this.onerror?.({ message } as ErrorEventShape);
  }
}

function encodeToBuffer(text: string): ArrayBuffer {
  return new TextEncoder().encode(text).buffer as ArrayBuffer;
}
