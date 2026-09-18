import { HeartbeatPing, HeartbeatPong, MsgID, Packet } from '@chirp/proto/gateway';
import { RequestError } from './errors';
import { FrameDecoder, FrameError, encodeFrame } from './frame';
import type { MessageSpec } from './msg_map';

/**
 * Connection state machine:
 *  idle → connecting → connected ⇄ waiting-reconnect (auto reconnect)
 *                    ↘ kicked        (KICK_NOTIFY: terminal, no reconnect)
 *  any  → closed        (manual disconnect(); terminal until connect())
 */
export type ConnStatus =
  | 'idle'
  | 'connecting'
  | 'connected'
  | 'waiting-reconnect'
  | 'closed'
  | 'kicked';

/** Minimal surface of the browser WebSocket, so tests can inject a fake. */
export interface WebSocketLike {
  binaryType: string;
  onopen: ((ev: Event) => void) | null;
  onmessage: ((ev: MessageEvent) => void) | null;
  onclose: ((ev: CloseEvent) => void) | null;
  onerror: ((ev: Event) => void) | null;
  send(data: ArrayBufferView): void;
  close(code?: number, reason?: string): void;
}

export interface ChirpClientOptions {
  url: string;
  wsFactory?: (url: string) => WebSocketLike;
  /** Server has no idle kick; 25s pings keep intermediaries honest. */
  heartbeatIntervalMs?: number;
  /** Die after this many consecutive pings without any pong. */
  maxMissedPongs?: number;
  requestTimeoutMs?: number;
  reconnectBaseMs?: number;
  reconnectMaxMs?: number;
  /** ± fraction applied to each reconnect delay. */
  jitterRatio?: number;
}

interface PendingEntry {
  resolve: (body: Uint8Array) => void;
  reject: (err: RequestError) => void;
  timer: ReturnType<typeof setTimeout>;
}

const DEFAULTS = {
  heartbeatIntervalMs: 25_000,
  maxMissedPongs: 2,
  requestTimeoutMs: 10_000,
  reconnectBaseMs: 500,
  reconnectMaxMs: 15_000,
  jitterRatio: 0.2,
};

type Timer = ReturnType<typeof setTimeout>;

const defaultWsFactory = (url: string): WebSocketLike => new WebSocket(url);

export class ChirpClient {
  private readonly opts: Required<Omit<ChirpClientOptions, 'url' | 'wsFactory'>> & {
    url: string;
    wsFactory: (url: string) => WebSocketLike;
  };

  private ws: WebSocketLike | null = null;
  private decoder = new FrameDecoder();
  private pending = new Map<number, PendingEntry>();
  private notifyHandlers = new Map<number, Set<(body: Uint8Array) => void>>();
  private statusListeners = new Set<(status: ConnStatus) => void>();

  private _status: ConnStatus = 'idle';
  private seqCounter = 0;
  private attempt = 0;
  private missedPongs = 0;
  private _kicked = false;
  private _clockOffsetMs: number | null = null;

  private reconnectTimer: Timer | null = null;
  private pingTimer: Timer | null = null;

  constructor(options: ChirpClientOptions) {
    this.opts = {
      url: options.url,
      wsFactory: options.wsFactory ?? defaultWsFactory,
      heartbeatIntervalMs: options.heartbeatIntervalMs ?? DEFAULTS.heartbeatIntervalMs,
      maxMissedPongs: options.maxMissedPongs ?? DEFAULTS.maxMissedPongs,
      requestTimeoutMs: options.requestTimeoutMs ?? DEFAULTS.requestTimeoutMs,
      reconnectBaseMs: options.reconnectBaseMs ?? DEFAULTS.reconnectBaseMs,
      reconnectMaxMs: options.reconnectMaxMs ?? DEFAULTS.reconnectMaxMs,
      jitterRatio: options.jitterRatio ?? DEFAULTS.jitterRatio,
    };
  }

  get status(): ConnStatus {
    return this._status;
  }

  /** True after the server sent KICK_NOTIFY; auto reconnect stays off. */
  get kicked(): boolean {
    return this._kicked;
  }

  /** server_time − local_time at the last pong; null before the first. */
  get clockOffsetMs(): number | null {
    return this._clockOffsetMs;
  }

  onStatus(listener: (status: ConnStatus) => void): () => void {
    this.statusListeners.add(listener);
    return () => this.statusListeners.delete(listener);
  }

  /**
   * Subscribe to server pushes (sequence === 0 packets). Unknown msgIds are
   * ignored, so subscribing to one is purely optional. Returns an
   * unsubscribe function.
   */
  onNotify(msgId: MsgID, handler: (body: Uint8Array) => void): () => void {
    let set = this.notifyHandlers.get(msgId);
    if (!set) {
      set = new Set();
      this.notifyHandlers.set(msgId, set);
    }
    set.add(handler);
    return () => set.delete(handler);
  }

  /** Opens the socket. Resolves on open, rejects if it closes first. */
  connect(): Promise<void> {
    // An explicit connect() is a user action: clear kick/terminal state and
    // let the backoff start over.
    this._kicked = false;
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws !== null) {
      const stale = this.ws;
      this.ws = null;
      stale.onclose = null;
      stale.close();
    }

    return new Promise<void>((resolve, reject) => {
      this.setStatus('connecting');
      const ws = this.opts.wsFactory(this.opts.url);
      this.ws = ws;
      ws.binaryType = 'arraybuffer';
      let settled = false;

      ws.onopen = () => {
        settled = true;
        this.missedPongs = 0;
        this.setStatus('connected');
        this.startHeartbeat();
        resolve();
      };
      ws.onmessage = (ev) => this.handleData(ev.data);
      ws.onclose = () => {
        if (!settled) {
          settled = true;
          reject(new RequestError('closed'));
        }
        this.handleClose();
      };
      ws.onerror = () => {
        // The close event always follows; nothing to do here.
      };
    });
  }

  /** Manual close; no reconnect follows. The close event is left attached:
   *  handleClose sees status === 'closed' and stops right there. */
  disconnect(): void {
    this.setStatus('closed');
    this.clearHeartbeat();
    this.rejectAllPending('closed');
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.ws?.close();
    this.ws = null;
  }

  /**
   * Reset the reconnect backoff after a successful login. Opening the socket
   * alone proves little (the server may still refuse the token), so the
   * api layer calls this once the LOGIN round-trip succeeded.
   */
  resetBackoff(): void {
    this.attempt = 0;
  }

  /**
   * Typed request/response. Rejects with RequestError('closed') when not
   * connected, ('timeout') on deadline, ('server', code) is decided by the
   * api layer from the decoded response.
   */
  request<Req, Resp>(
    spec: MessageSpec<Req, Resp>,
    req: Partial<Req>,
    timeoutMs: number = this.opts.requestTimeoutMs,
  ): Promise<Resp> {
    if (this._status !== 'connected' || this.ws === null) {
      return Promise.reject(new RequestError('closed'));
    }
    const sequence = ++this.seqCounter;
    return new Promise<Uint8Array>((resolve, reject) => {
      const entry: PendingEntry = {
        resolve,
        reject,
        timer: setTimeout(() => {
          this.pending.delete(sequence);
          reject(new RequestError('timeout'));
        }, timeoutMs),
      };
      this.pending.set(sequence, entry);
      try {
        this.rawSend(spec.reqMsgId, spec.encodeRequest(req), sequence);
      } catch (err) {
        clearTimeout(entry.timer);
        this.pending.delete(sequence);
        reject(err instanceof RequestError ? err : new RequestError('closed'));
      }
    }).then((body) => spec.decodeResponse(body));
  }

  /** Immediate heartbeat, e.g. when a background tab comes back to the foreground. */
  heartbeatNow(): void {
    if (this._status === 'connected') {
      this.sendPing();
    }
  }

  private setStatus(status: ConnStatus): void {
    if (this._status === status) return;
    this._status = status;
    for (const listener of this.statusListeners) listener(status);
  }

  private rawSend(msgId: MsgID, body: Uint8Array, sequence: number): void {
    const ws = this.ws;
    if (ws === null) throw new RequestError('closed');
    const packet = Packet.encode(Packet.fromPartial({ msgId, sequence, body })).finish();
    ws.send(encodeFrame(packet));
  }

  private startHeartbeat(): void {
    this.clearHeartbeat();
    this.pingTimer = setInterval(() => this.sendPing(), this.opts.heartbeatIntervalMs);
  }

  private clearHeartbeat(): void {
    if (this.pingTimer !== null) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }

  private sendPing(): void {
    if (this.missedPongs >= this.opts.maxMissedPongs) {
      // Consecutive missed pongs: the link is dead in practice — close it and
      // let the reconnect path take over.
      this.ws?.close();
      return;
    }
    this.missedPongs++;
    const ping = HeartbeatPing.encode(HeartbeatPing.fromPartial({ timestamp: Date.now() })).finish();
    try {
      this.rawSend(MsgID.HEARTBEAT_PING, ping, ++this.seqCounter);
    } catch {
      // Socket died underneath us; onclose will fire.
    }
  }

  private handleData(data: unknown): void {
    let bytes: Uint8Array;
    if (data instanceof ArrayBuffer) {
      bytes = new Uint8Array(data);
    } else if (data instanceof Uint8Array) {
      bytes = data;
    } else {
      return; // text frames / blobs are not part of this protocol
    }
    let frames: Uint8Array[];
    try {
      frames = this.decoder.feed(bytes);
    } catch (err) {
      // A corrupted stream cannot be resynchronized; treat it as a dead link.
      void err;
      this.ws?.close();
      return;
    }
    for (const frame of frames) this.handleFrame(frame);
  }

  private handleFrame(frame: Uint8Array): void {
    let packet: Packet;
    try {
      packet = Packet.decode(frame);
    } catch {
      return; // undecodable packet: ignore rather than kill the connection
    }
    if (packet.sequence === 0) {
      this.dispatchNotify(packet.msgId, packet.body);
      return;
    }
    if (packet.msgId === MsgID.HEARTBEAT_PONG) {
      // Pong echoes the ping's sequence and is not in the pending table.
      this.onPong(packet.body);
      return;
    }
    const entry = this.pending.get(packet.sequence);
    if (!entry) return; // response to an already timed-out request
    clearTimeout(entry.timer);
    this.pending.delete(packet.sequence);
    entry.resolve(packet.body);
  }

  private onPong(body: Uint8Array): void {
    this.missedPongs = 0;
    try {
      const pong = HeartbeatPong.decode(body);
      this._clockOffsetMs = pong.serverTime - Date.now();
    } catch {
      // A malformed pong still proves the link is alive.
    }
  }

  private dispatchNotify(msgId: MsgID, body: Uint8Array): void {
    if (msgId === MsgID.KICK_NOTIFY) {
      this.markKicked();
      return;
    }
    const handlers = this.notifyHandlers.get(msgId);
    if (!handlers) return; // unknown or uninteresting msgId
    for (const handler of handlers) {
      try {
        handler(body);
      } catch {
        // One bad handler must not starve the others.
      }
    }
  }

  private markKicked(): void {
    // KICK then reconnect would just fight the new device, so auto reconnect
    // stays off and the user is sent back to login. The close event stays
    // attached; handleClose checks _kicked and stops there.
    this._kicked = true;
    this.clearHeartbeat();
    this.rejectAllPending('kicked');
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.ws?.close();
    this.ws = null;
  }

  private handleClose(): void {
    this.ws = null;
    this.clearHeartbeat();
    this.rejectAllPending('closed');
    this.decoder.reset();
    if (this._kicked) {
      this.setStatus('kicked');
      return;
    }
    if (this._status === 'closed') return; // manual disconnect()
    this.setStatus('waiting-reconnect');
    this.scheduleReconnect();
  }

  private rejectAllPending(kind: 'closed' | 'kicked'): void {
    for (const entry of this.pending.values()) {
      clearTimeout(entry.timer);
      entry.reject(new RequestError(kind));
    }
    this.pending.clear();
  }

  private scheduleReconnect(): void {
    const base = Math.min(
      this.opts.reconnectBaseMs * 2 ** this.attempt,
      this.opts.reconnectMaxMs,
    );
    const jitter = base * this.opts.jitterRatio;
    const delay = base + (Math.random() * 2 - 1) * jitter;
    this.attempt++;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      // A failed attempt loops straight back through handleClose.
      this.connect().catch(() => undefined);
    }, delay);
  }
}

// Re-exported for callers that want to react to corrupted frames distinctly.
export { FrameError };
