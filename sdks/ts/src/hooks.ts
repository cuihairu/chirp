/**
 * Five hook interfaces for the chat pipeline — TypeScript face of the same
 * contract the C++ core (sdks/core/include/chirp/*) and the Unity SDK
 * (sdks/unity/Runtime/Chirp/ChirpHooks.cs) ship:
 *
 * - MessageInterceptor: rewrite/audit points around send & receive.
 *   Returning false from onBeforeSend/onBeforeReceive drops the message;
 *   the receive side drop also skips the archive and listeners.
 * - AuthProvider: token sourcing for login + one renewal on AUTH_FAILED.
 * - MessageStore: local archive; the pipeline saves both directions.
 * - ChatEventListener: lifecycle fan-out. Events without a trigger source
 *   on this plane stay undefined (same as the C++/C# no-op defaults).
 * - CommandHandler: local '/'-command routing.
 *
 * Every hook is optional at the registration site: the pipeline skips a
 * hook kind entirely when unset, and behaviour is byte-identical to the
 * pre-hook pipeline then (the C++/C# "zero hooks = zero change" rule).
 */
import { ChatMessage } from '@chirp/proto/chat';
import type { ChannelType, MsgType, SendMessageRequest } from '@chirp/proto/chat';
import type { ConnStatus } from './chirp_client';

/** Send parameters for ChatPipeline.send (C++ SendOptions / C# SendOptions). */
export interface SendOptions {
  channelType: ChannelType;
  /** Required for every non-private channel; private derives the sorted pair. */
  channelId?: string;
  /** Required for private; the wire channel id is the sorted "a|b" pair. */
  receiverId?: string;
  /** Quoted message id; empty string = not a reply. */
  replyToMessageId?: string;
  msgType?: MsgType;
}

export interface MessageInterceptor {
  /** false = the message never reaches the wire. */
  onBeforeSend?(req: SendMessageRequest): boolean;
  onAfterSend?(req: SendMessageRequest): void;
  /** false = dropped for the archive, the listeners and onAfterReceive. */
  onBeforeReceive?(msg: ChatMessage): boolean;
  onAfterReceive?(msg: ChatMessage): void;
}

export interface AuthProvider {
  /** Used when no explicit token is passed to login(). */
  getToken(): string;
  /**
   * AUTH_FAILED gives this one chance to hand back a fresh token (the
   * pipeline retries the login round once). Returning null/undefined ends
   * the login with the failure — at most one renewal per login chain,
   * identical to C++ OnTokenExpired / C# RenewTokenAsync.
   */
  renewToken?(): Promise<string | null | undefined>;
  /** Terminal auth outcome: success or final failure. */
  onAuthResult?(code: number, userId: string): void;
}

export interface MessageStore {
  /** Called for both directions (sent copies carry an empty messageId). */
  save(msg: ChatMessage): void;
  /** Newest first; beforeTimestamp 0/undefined = no bound. */
  load(
    channelType: ChannelType,
    channelId: string,
    limit: number,
    beforeTimestamp?: number,
  ): ChatMessage[];
  markRead?(channelType: ChannelType, channelId: string, messageId: string): void;
  /** Without an implementation the pipeline reports 0 (C++/C# parity). */
  getUnreadCount?(channelType: ChannelType, channelId: string): number;
  cleanup?(olderThanMs: number): void;
}

/**
 * In-memory archive keyed by `${channelType}|${channelId}`. Newest-first
 * loads, oldest-first eviction beyond the cap — the C#/C++ MemoryMessageStore
 * semantics. markRead/getUnreadCount are not tracked (0, like the reference).
 */
export class MemoryMessageStore implements MessageStore {
  private readonly channels = new Map<string, ChatMessage[]>();

  constructor(private readonly maxPerChannel = 200) {}

  save(msg: ChatMessage): void {
    const key = `${msg.channelType}|${msg.channelId}`;
    const bucket = this.channels.get(key) ?? [];
    // Snapshot via a codec round-trip: callers may reuse/mutate the message
    // after save (C++ stores by value); the archive must not alias the caller.
    bucket.push(ChatMessage.decode(ChatMessage.encode(msg).finish()));
    if (this.maxPerChannel > 0 && bucket.length > this.maxPerChannel) {
      bucket.splice(0, bucket.length - this.maxPerChannel);
    }
    this.channels.set(key, bucket);
  }

  load(
    channelType: ChannelType,
    channelId: string,
    limit: number,
    beforeTimestamp?: number,
  ): ChatMessage[] {
    const out: ChatMessage[] = [];
    if (limit <= 0) return out;
    const bucket = this.channels.get(`${channelType}|${channelId}`);
    if (!bucket) return out;
    for (let i = bucket.length - 1; i >= 0 && out.length < limit; i--) {
      const msg = bucket[i]!;
      if (beforeTimestamp === undefined || beforeTimestamp === 0 || msg.timestamp < beforeTimestamp) {
        out.push(msg);
      }
    }
    return out;
  }

  cleanup(olderThanMs: number): void {
    for (const [key, bucket] of this.channels) {
      const kept = bucket.filter((m) => m.timestamp >= olderThanMs);
      if (kept.length === 0) this.channels.delete(key);
      else this.channels.set(key, kept);
    }
  }
}

/**
 * Lifecycle listener. onReconnecting/onReconnected/onMessageReceived are
 * wired by ChatPipeline; onLoginResult/onKicked/onConnectionStateChanged are
 * the terminal reports. Unread/presence/typing/marquee/announcement have no
 * trigger source on this plane yet — keep them undefined.
 */
export interface ChatEventListener {
  onConnectionStateChanged?(status: ConnStatus): void;
  onLoginResult?(code: number, userId: string): void;
  onKicked?(reason: string): void;
  onReconnecting?(attempt: number, delayMs: number): void;
  onReconnected?(): void;
  onMessageReceived?(msg: ChatMessage): void;
}

export interface CommandHandler {
  /** Command word after the '/', matched in registration order. */
  readonly name: string;
  /** Display form; defaults to `/${name}` like the C++/C# GetUsage default. */
  readonly usage?: string;
  /** false = the next handler with the same name gets a try. */
  execute(args: string, senderId: string): boolean;
}
