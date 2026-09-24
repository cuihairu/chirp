/**
 * ChatPipeline — the hook host for the web plane, structurally the sibling of
 * ChatClient on Unity/.NET (sdks/unity) and ChatClient on C++ (sdks/core):
 * command routing, interceptor rewrite/drop, local archive and lifecycle
 * listeners around a plain request/notify connection.
 *
 * Threading: everything runs on the JS event loop, so the C++/C# "io thread →
 * marshal to game thread" contract collapses to plain calls here. Ordering
 * guarantees are preserved exactly: onBeforeReceive drop kills the archive,
 * the listeners and onAfterReceive; other notify subscribers (OnNotify) still
 * see the original wire body, untouched by interceptor rewrites.
 *
 * Messages that never reach the wire throw RequestError('blocked') — command
 * handled locally, unknown command, interceptor drop — identical to the C#
 * SendMessageAsync contract. A returned response may still carry a non-OK
 * code (rate limit, invalid param…); read resp.code.
 */
import {
  ChannelType,
  ChatMessage,
  MsgType,
  SendMessageRequest,
  SendMessageResponse,
} from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { KickNotify } from '@chirp/proto/auth';
import { MsgID } from '@chirp/proto/gateway';
import type { ConnStatus } from './chirp_client';
import { RequestError } from './errors';
import { LOGIN, SEND_MESSAGE } from './msg_map';
import type { MessageSpec } from './msg_map';
import type {
  AuthProvider,
  ChatEventListener,
  CommandHandler,
  MessageInterceptor,
  MessageStore,
  SendOptions,
} from './hooks';

/**
 * Structural subset of ChirpClient + the reconnect event surface, so tests
 * can inject fakes and the api layer can pass the real client as-is.
 */
export interface PipelineConnection {
  request<Req, Resp>(
    spec: MessageSpec<Req, Resp>,
    req: Partial<Req>,
    timeoutMs?: number,
  ): Promise<Resp>;
  send(msgId: MsgID, body: Uint8Array): void;
  onNotify(msgId: MsgID, handler: (body: Uint8Array) => void): () => void;
  onStatus(listener: (status: ConnStatus) => void): () => void;
  onReconnecting(listener: (attempt: number, delayMs: number) => void): () => void;
  onReconnected(listener: () => void): () => void;
  resetBackoff(): void;
  readonly status: ConnStatus;
}

export interface ChatPipelineDeps {
  conn: PipelineConnection;
  /** Current user id (auth store getter); stamps sender and private keys. */
  selfId: () => string;
  /** Device id for LOGIN (auth store getter). */
  deviceId: () => string;
}

interface ListenerRecord {
  listener: ChatEventListener;
}

const encoder = new TextEncoder();

export class ChatPipeline {
  private readonly conn: PipelineConnection;
  private readonly selfId: () => string;
  private readonly deviceId: () => string;

  private interceptor: MessageInterceptor | null = null;
  private provider: AuthProvider | null = null;
  private store: MessageStore | null = null;
  private listeners: ListenerRecord[] = [];
  private commands: CommandHandler[] = [];
  private unsubs: Array<() => void> = [];

  constructor(deps: ChatPipelineDeps) {
    this.conn = deps.conn;
    this.selfId = deps.selfId;
    this.deviceId = deps.deviceId;
  }

  // ---- hook registration (C++/C# setter parity) ---------------------------

  setInterceptor(interceptor: MessageInterceptor | null): void {
    this.interceptor = interceptor;
  }

  setProvider(provider: AuthProvider | null): void {
    this.provider = provider;
  }

  setStore(store: MessageStore | null): void {
    this.store = store;
  }

  addListener(listener: ChatEventListener): () => void {
    const record = { listener };
    this.listeners.push(record);
    return () => {
      this.listeners = this.listeners.filter((r) => r !== record);
    };
  }

  registerCommand(handler: CommandHandler): () => void {
    this.commands.push(handler);
    return () => {
      this.commands = this.commands.filter((c) => c !== handler);
    };
  }

  // ---- lifecycle wiring ----------------------------------------------------

  /** Idempotent; call again after stop() (same pattern as ChatApi.start). */
  start(): void {
    if (this.unsubs.length > 0) return;
    this.unsubs = [
      this.conn.onNotify(MsgID.CHAT_MESSAGE_NOTIFY, (body) => this.onIncoming(body)),
      this.conn.onNotify(MsgID.KICK_NOTIFY, (body) => this.onKickBody(body)),
      this.conn.onStatus((status) => {
        for (const { listener } of this.snapshotListeners()) {
          try {
            listener.onConnectionStateChanged?.(status);
          } catch {
            // A throwing listener must not starve the others.
          }
        }
      }),
      this.conn.onReconnecting((attempt, delayMs) => {
        for (const { listener } of this.snapshotListeners()) {
          try {
            listener.onReconnecting?.(attempt, delayMs);
          } catch {
            // Same isolation rule.
          }
        }
      }),
      this.conn.onReconnected(() => {
        for (const { listener } of this.snapshotListeners()) {
          try {
            listener.onReconnected?.();
          } catch {
            // Same isolation rule.
          }
        }
      }),
    ];
  }

  stop(): void {
    for (const off of this.unsubs) off();
    this.unsubs = [];
  }

  /**
   * LOGIN with token sourcing: explicit token wins, then provider.getToken(),
   * then userId itself (scaffold mode). AUTH_FAILED gives the provider one
   * renewal chance (renewToken → a second LOGIN round); at most one renewal
   * per login chain. Terminal outcome fans out onLoginResult + onAuthResult.
   * Returns the final code (0 = logged in).
   */
  async login(userId: string, token?: string): Promise<ErrorCode> {
    const initial = token ?? this.provider?.getToken() ?? userId;
    let code = await this.loginRound(initial);
    if (code === ErrorCode.AUTH_FAILED && this.provider?.renewToken) {
      const fresh = await this.provider.renewToken();
      if (fresh) {
        code = await this.loginRound(fresh);
      }
    }
    this.terminalAuth(code, userId);
    return code;
  }

  /**
   * Full-pipeline send. Throws RequestError('closed') when not connected,
   * RequestError('blocked') for messages consumed before the wire, and
   * TypeError/RangeError for invalid arguments (C# ArgumentException parity).
   */
  async send(options: SendOptions, content: string): Promise<SendMessageResponse> {
    if (this.conn.status !== 'connected') {
      // 状态检查先于参数校验(C++ 参考实现顺序)。
      throw new RequestError('closed');
    }
    if (content.length === 0) {
      throw new RangeError('content is empty');
    }
    if (options.channelType === ChannelType.PRIVATE) {
      if (!options.receiverId) {
        throw new TypeError('private send needs receiverId');
      }
    } else if (!options.channelId) {
      throw new TypeError(`${options.channelType} send needs an explicit channelId`);
    }

    if (content.startsWith('/')) {
      const routed = this.routeCommand(content, this.selfId());
      if (routed !== null) {
        throw new RequestError('blocked', undefined, routed);
      }
    }

    const request = this.buildSendRequest(options, content);
    const interceptor = this.interceptor;
    if (interceptor?.onBeforeSend) {
      let allowed: boolean;
      try {
        allowed = interceptor.onBeforeSend(request);
      } catch {
        allowed = false; // a throwing interceptor is a blocking one
      }
      if (!allowed) {
        throw new RequestError('blocked', undefined, 'message blocked by interceptor');
      }
    }

    const store = this.store;
    if (store) {
      try {
        store.save(this.storedCopyOf(request));
      } catch {
        // Archive failures must not block the send.
      }
    }

    const resp = await this.conn.request(SEND_MESSAGE, request);
    try {
      interceptor?.onAfterSend?.(request);
    } catch {
      // Audit hooks must not break the caller.
    }
    return resp;
  }

  // ---- archive forwards (C++ LoadHistory/MarkRead/GetUnreadCount/Cleanup) --

  /** Newest-first slice; empty when no store is installed. */
  loadHistory(
    channelType: ChannelType,
    channelId: string,
    limit: number,
    beforeTimestamp?: number,
  ): ChatMessage[] {
    return this.store?.load(channelType, channelId, limit, beforeTimestamp) ?? [];
  }

  markRead(channelType: ChannelType, channelId: string, messageId: string): void {
    this.store?.markRead?.(channelType, channelId, messageId);
  }

  unreadCount(channelType: ChannelType, channelId: string): number {
    return this.store?.getUnreadCount?.(channelType, channelId) ?? 0;
  }

  cleanup(olderThanMs: number): void {
    this.store?.cleanup?.(olderThanMs);
  }

  // ---- internals -----------------------------------------------------------

  /** null = pass through (no handlers); a string = the blocked reason. */
  private routeCommand(content: string, senderId: string): string | null {
    if (this.commands.length === 0) return null;
    let name = content.slice(1);
    let args = '';
    const space = name.indexOf(' ');
    if (space >= 0) {
      args = name.slice(space + 1);
      name = name.slice(0, space);
    }
    let handled = false;
    for (const command of this.commands) {
      if (command.name !== name) continue;
      try {
        handled = command.execute(args, senderId);
      } catch {
        handled = false; // a throwing handler declines, like C++/C#
      }
      if (handled) break;
    }
    return handled ? 'command handled locally' : `unknown command, dropped locally: ${content}`;
  }

  private buildSendRequest(options: SendOptions, content: string): SendMessageRequest {
    const senderId = this.selfId();
    let channelId = options.channelId;
    if (options.channelType === ChannelType.PRIVATE) {
      // 双方 id 字典序小者在前,与服务端/c++/c# 同款。
      channelId = [senderId, options.receiverId].sort().join('|');
    }
    return SendMessageRequest.fromPartial({
      senderId,
      receiverId: options.receiverId,
      channelType: options.channelType,
      channelId,
      msgType: options.msgType ?? MsgType.TEXT,
      content: encoder.encode(content),
      clientTimestamp: Date.now(),
      replyToMessageId: options.replyToMessageId ?? '',
    });
  }

  private storedCopyOf(request: SendMessageRequest): ChatMessage {
    return ChatMessage.fromPartial({
      senderId: request.senderId,
      receiverId: request.receiverId,
      channelType: request.channelType,
      channelId: request.channelId,
      msgType: request.msgType,
      content: request.content,
      timestamp: request.clientTimestamp,
      replyToMessageId: request.replyToMessageId,
    });
  }

  /** Interceptor → archive → listeners → onAfterReceive; drop kills all. */
  private onIncoming(body: Uint8Array): void {
    let msg: ChatMessage;
    try {
      msg = ChatMessage.decode(body);
    } catch {
      return; // undecodable: other notify subscribers still got the raw body
    }
    const interceptor = this.interceptor;
    if (interceptor?.onBeforeReceive) {
      let allowed: boolean;
      try {
        allowed = interceptor.onBeforeReceive(msg);
      } catch {
        allowed = false; // a throwing interceptor is a blocking one
      }
      if (!allowed) return;
    }
    const store = this.store;
    if (store) {
      try {
        store.save(msg);
      } catch {
        // Archive failures must not kill the fan-out.
      }
    }
    for (const { listener } of this.snapshotListeners()) {
      try {
        listener.onMessageReceived?.(msg);
      } catch {
        // A throwing listener must not starve the others.
      }
    }
    try {
      interceptor?.onAfterReceive?.(msg);
    } catch {
      // Same isolation rule.
    }
  }

  private onKickBody(body: Uint8Array): void {
    let kick: KickNotify;
    try {
      kick = KickNotify.decode(body);
    } catch {
      kick = KickNotify.fromPartial({});
    }
    for (const { listener } of this.snapshotListeners()) {
      try {
        listener.onKicked?.(kick.reason);
      } catch {
        // Same isolation rule.
      }
    }
  }

  private async loginRound(token: string): Promise<ErrorCode> {
    const resp = await this.conn.request(LOGIN, {
      token,
      deviceId: this.deviceId(),
      platform: 'web',
    });
    if (resp.code === 0) {
      this.conn.resetBackoff();
    }
    return resp.code;
  }

  private terminalAuth(code: ErrorCode, userId: string): void {
    for (const { listener } of this.snapshotListeners()) {
      try {
        listener.onLoginResult?.(code, userId);
      } catch {
        // Same isolation rule.
      }
    }
    try {
      this.provider?.onAuthResult?.(code, userId);
    } catch {
      // Same isolation rule.
    }
  }

  private snapshotListeners(): ListenerRecord[] {
    return [...this.listeners];
  }
}
