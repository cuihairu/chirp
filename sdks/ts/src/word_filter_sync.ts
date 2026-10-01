/**
 * Lexicon-distribution sync — TypeScript face of the 词库下发协议
 * (docs/design-notes/word_filter.md「词库下发协议」, MsgID 2245-2247).
 *
 * Wraps a hot-swappable WordFilterInterceptor: FETCH_RESP / UPDATE_NOTIFY
 * payloads rebuild it in place, so the send-side pre-check follows the
 * server's lexicon instead of a manually deployed file. A locally loaded
 * lexicon (loadLocal — the old WordFilterLoader face) remains as fallback
 * until the first server lexicon arrives; a server answering version 0
 * (no lexicon configured) clears the pre-check entirely — never stricter
 * than the server.
 */
import type { SendMessageRequest, WordFilterLexicon } from '@chirp/proto/chat';
import { WordFilterDeliveryPolicy as WirePolicy, WordFilterUpdateNotify } from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { MsgID } from '@chirp/proto/gateway';
import type { PipelineConnection } from './chat_pipeline';
import type { MessageInterceptor } from './hooks';
import { WORD_FILTER_FETCH } from './msg_map';
import { parseWordLexicon, WordFilterInterceptor } from './word_filter';
import type { WordFilterPolicy } from './word_filter';

export interface WordFilterSyncOptions {
  /** Deadline for the fetch round-trip; default 5000ms. */
  timeoutMs?: number;
}

export class WordFilterSync implements MessageInterceptor {
  private inner: WordFilterInterceptor | null = null;
  private version_ = 0;
  private readonly timeoutMs: number;
  private unsubscribes: (() => void)[] = [];

  constructor(
    private readonly conn: PipelineConnection,
    options: WordFilterSyncOptions = {},
  ) {
    this.timeoutMs = options.timeoutMs ?? 5000;
  }

  /** Last applied server lexicon version; 0 = none seen yet. */
  get version(): number {
    return this.version_;
  }

  /** Terms currently loaded for the pre-check (server or local fallback). */
  get wordCount(): number {
    return this.inner?.wordCount ?? 0;
  }

  /** Subscribe UPDATE_NOTIFY. Call once after login — notifies and fetch
   * both ride the authenticated session. */
  start(): void {
    this.unsubscribes.push(
      this.conn.onNotify(MsgID.WORD_FILTER_UPDATE_NOTIFY, (body) => {
        try {
          this.apply(WordFilterUpdateNotify.decode(body).lexicon);
        } catch {
          // Malformed notify: ignore — the next fetch re-syncs.
        }
      }),
    );
  }

  stop(): void {
    for (const off of this.unsubscribes) {
      off();
    }
    this.unsubscribes = [];
  }

  /** Conditional GET. Never throws (timeout / closed / non-OK all resolve
   * false): per the proposal a failed sync must not surface to UI — the
   * fallback lexicon keeps working and the next connection retries. */
  async fetch(): Promise<boolean> {
    try {
      const resp = await this.conn.request(
        WORD_FILTER_FETCH,
        { knownVersion: this.version_ },
        this.timeoutMs,
      );
      if (resp.code !== ErrorCode.OK) {
        return false;
      }
      this.apply(resp.lexicon);
      return true;
    } catch {
      return false;
    }
  }

  /** Local-file fallback: lexicon lines in the server file format (what the
   * platform WordFilterLoader used to hand the shell). Leaves version at 0,
   * so the first server lexicon (version >= 1) supersedes it. */
  loadLocal(
    lines: readonly string[],
    policy: WordFilterPolicy = 'replace',
    replacement = '**',
  ): void {
    this.inner = new WordFilterInterceptor({
      terms: parseWordLexicon(lines),
      policy,
      replacement,
    });
  }

  /** Passthrough when no lexicon is in force (server disabled or record). */
  onBeforeSend(request: SendMessageRequest): boolean {
    return this.inner ? this.inner.onBeforeSend(request) : true;
  }

  /** Version-gated rebuild: anything at or below the applied version is a
   * duplicate or a reordered delivery and is ignored. */
  private apply(lexicon?: WordFilterLexicon): void {
    if (!lexicon || lexicon.version <= this.version_) {
      return;
    }
    this.version_ = lexicon.version;
    // record is server-side audit only — no client pre-check (never block
    // what the server itself would pass through). enabled=false means the
    // server filters nothing: clear ours too.
    if (!lexicon.enabled || lexicon.policy === WirePolicy.WORD_FILTER_POLICY_RECORD) {
      this.inner = null;
      return;
    }
    this.inner = new WordFilterInterceptor({
      terms: parseWordLexicon(lexicon.lexicon.split('\n')),
      policy: lexicon.policy === WirePolicy.WORD_FILTER_POLICY_REJECT ? 'reject' : 'replace',
      replacement: lexicon.replacement || '**',
    });
  }
}
