import { MsgID } from '@chirp/proto/gateway';
import type { MessageSpec } from '@chirp/protocol/msg_map';
import { RequestError } from '@chirp/protocol/errors';
import type { ConnStatus } from '@chirp/protocol/chirp_client';
import type { ChatConnection } from '../api/chat_api';

/**
 * In-memory ChatConnection double. Tests either resolve requests through a
 * canned handler or let them time out; `emit` drives the notify path.
 */
export class FakeChatConnection implements ChatConnection {
  status: ConnStatus = 'connected';
  kicked = false;
  requests: Array<{ msgId: MsgID; req: unknown }> = [];

  private handlers = new Map<MsgID, Set<(body: Uint8Array) => void>>();
  private statusHandlers = new Set<(status: ConnStatus) => void>();
  private responder: ((msgId: MsgID, req: unknown) => Promise<unknown>) | null = null;
  private failNext = false;

  connect(): Promise<void> {
    return Promise.resolve();
  }

  disconnect(): void {}

  resetBackoff(): void {}

  heartbeatNow(): void {}

  setResponder(responder: (msgId: MsgID, req: unknown) => Promise<unknown>): void {
    this.responder = responder;
  }

  /** Make exactly one upcoming request reject (connection drop, timeout...). */
  failNextRequest(): void {
    this.failNext = true;
  }

  async request<Req, Resp>(
    spec: MessageSpec<Req, Resp>,
    req: Partial<Req>,
  ): Promise<Resp> {
    this.requests.push({ msgId: spec.reqMsgId, req });
    if (this.failNext) {
      this.failNext = false;
      // Mirrors ChirpClient: connection-level failures reject with RequestError.
      throw new RequestError('closed');
    }
    if (!this.responder) throw new Error('no responder configured');
    return (await this.responder(spec.reqMsgId, req)) as Resp;
  }

  send(msgId: MsgID, body: Uint8Array): void {
    this.requests.push({ msgId, req: body });
  }

  onNotify(msgId: MsgID, handler: (body: Uint8Array) => void): () => void {
    let set = this.handlers.get(msgId);
    if (!set) {
      set = new Set();
      this.handlers.set(msgId, set);
    }
    set.add(handler);
    return () => set.delete(handler);
  }

  onStatus(listener: (status: ConnStatus) => void): () => void {
    this.statusHandlers.add(listener);
    return () => this.statusHandlers.delete(listener);
  }

  /** Push a notify to the api layer as if the server had sent it. */
  emit(msgId: MsgID, body: Uint8Array): void {
    for (const handler of this.handlers.get(msgId) ?? []) handler(body);
  }

  /** Drive onStatus subscribers as if the client state machine moved. */
  emitStatus(status: ConnStatus): void {
    this.status = status;
    for (const handler of this.statusHandlers) handler(status);
  }
}
