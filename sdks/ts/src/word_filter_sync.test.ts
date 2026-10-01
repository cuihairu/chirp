import { SendMessageRequest, WordFilterUpdateNotify, type WordFilterLexicon } from '@chirp/proto/chat';
import { WordFilterDeliveryPolicy as WirePolicy } from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { MsgID } from '@chirp/proto/gateway';
import { describe, expect, it } from 'vitest';
import type { PipelineConnection } from './chat_pipeline';
import { RequestError } from './errors';
import type { MessageSpec } from './msg_map';
import { WORD_FILTER_FETCH } from './msg_map';
import { WordFilterSync } from './word_filter_sync';

type NotifyHandler = (body: Uint8Array) => void;

/** Structural PipelineConnection fake: request is answered by requestImpl,
 * notify bodies are pumped by hand through the returned pump. */
function makeConn(requestImpl: (req: { knownVersion: number }) => Promise<unknown>) {
  const handlers = new Map<MsgID, NotifyHandler>();
  const requests: { msgId: MsgID; req: unknown; timeoutMs?: number }[] = [];
  const conn = {
    request: (spec: MessageSpec<unknown, unknown>, req: unknown, timeoutMs?: number) => {
      requests.push({ msgId: spec.reqMsgId, req, timeoutMs });
      return requestImpl(req as { knownVersion: number });
    },
    send: () => {},
    onNotify: (msgId: MsgID, handler: NotifyHandler) => {
      handlers.set(msgId, handler);
      return () => handlers.delete(msgId);
    },
    onStatus: () => () => {},
    onReconnecting: () => () => {},
    onReconnected: () => () => {},
    resetBackoff: () => {},
    status: 'connected' as const,
  };
  return {
    conn: conn as unknown as PipelineConnection,
    requests,
    notify: (msgId: MsgID, body: Uint8Array) => handlers.get(msgId)?.(body),
  };
}

function lexicon(
  version: number,
  enabled: boolean,
  policy: WirePolicy,
  text: string,
): WordFilterLexicon {
  return { version, enabled, policy, replacement: '**', lexicon: text };
}

function fetchResp(lex: WordFilterLexicon): { code: ErrorCode; lexicon: WordFilterLexicon } {
  return { code: ErrorCode.OK, lexicon: lex };
}

function notifyBody(lex: WordFilterLexicon): Uint8Array {
  return WordFilterUpdateNotify.encode(WordFilterUpdateNotify.fromPartial({ lexicon: lex })).finish();
}

/** Runs the sync's pre-check over one send; returns the verdict + content. */
function run(sync: WordFilterSync, content: string): { allowed: boolean; text: string } {
  const request = SendMessageRequest.fromPartial({
    content: new TextEncoder().encode(content),
  });
  const allowed = sync.onBeforeSend(request);
  return { allowed, text: new TextDecoder().decode(request.content) };
}

describe('WordFilterSync', () => {
  it('fetch loads the server lexicon and rewrites sends', async () => {
    const { conn, requests } = makeConn(() =>
      Promise.resolve(fetchResp(lexicon(3, true, WirePolicy.WORD_FILTER_POLICY_REPLACE, 'spam\nbad\n'))));
    const sync = new WordFilterSync(conn);
    await expect(sync.fetch()).resolves.toBe(true);
    expect(sync.version).toBe(3);
    expect(sync.wordCount).toBe(2);
    expect(requests[0].msgId).toBe(WORD_FILTER_FETCH.reqMsgId);
    expect(requests[0].req).toEqual({ knownVersion: 0 });
    expect(run(sync, 'hello spam')).toEqual({ allowed: true, text: 'hello **' });
  });

  it('conditional GET skips the rebuild when the server version matches', async () => {
    const { conn, requests } = makeConn(() =>
      Promise.resolve(fetchResp(lexicon(3, true, WirePolicy.WORD_FILTER_POLICY_REPLACE, 'spam\n'))));
    const sync = new WordFilterSync(conn);
    await sync.fetch();
    // Second round: server at the same version answers without text.
    await expect(sync.fetch()).resolves.toBe(true);
    expect(requests[1].req).toEqual({ knownVersion: 3 });
    expect(sync.version).toBe(3);
    expect(sync.wordCount).toBe(1);
    expect(run(sync, 'hello spam').text).toBe('hello **');
  });

  it('UPDATE_NOTIFY hot-swaps the pre-check', async () => {
    const { conn, notify } = makeConn(() => Promise.reject(new RequestError('timeout')));
    const sync = new WordFilterSync(conn);
    sync.start();
    notify(MsgID.WORD_FILTER_UPDATE_NOTIFY, notifyBody(lexicon(4, true, WirePolicy.WORD_FILTER_POLICY_REPLACE, 'evil')));
    expect(sync.version).toBe(4);
    expect(run(sync, 'you evil one')).toEqual({ allowed: true, text: 'you ** one' });
  });

  it('stale notifies are ignored (reorder/dup immunity)', async () => {
    const { conn, notify } = makeConn(() => Promise.reject(new RequestError('closed')));
    const sync = new WordFilterSync(conn);
    sync.start();
    notify(MsgID.WORD_FILTER_UPDATE_NOTIFY, notifyBody(lexicon(5, true, WirePolicy.WORD_FILTER_POLICY_REPLACE, 'fresh')));
    notify(MsgID.WORD_FILTER_UPDATE_NOTIFY, notifyBody(lexicon(2, true, WirePolicy.WORD_FILTER_POLICY_REPLACE, 'stale')));
    expect(sync.version).toBe(5);
    expect(sync.wordCount).toBe(1);
    expect(run(sync, 'fresh')).toEqual({ allowed: true, text: '**' });
    expect(run(sync, 'stale').text).toBe('stale');
  });

  it('a server without a lexicon clears even the local fallback', async () => {
    const { conn, notify } = makeConn(() => Promise.reject(new RequestError('closed')));
    const sync = new WordFilterSync(conn);
    sync.loadLocal(['spam']);
    expect(run(sync, 'hello spam').text).toBe('hello **');
    sync.start();
    notify(MsgID.WORD_FILTER_UPDATE_NOTIFY, notifyBody(lexicon(1, false, WirePolicy.WORD_FILTER_POLICY_REPLACE, '')));
    expect(sync.version).toBe(1);
    expect(sync.wordCount).toBe(0);
    expect(run(sync, 'hello spam')).toEqual({ allowed: true, text: 'hello spam' });
  });

  it('record policy passes through without a pre-check', async () => {
    const { conn, notify } = makeConn(() => Promise.reject(new RequestError('closed')));
    const sync = new WordFilterSync(conn);
    sync.start();
    notify(MsgID.WORD_FILTER_UPDATE_NOTIFY, notifyBody(lexicon(2, true, WirePolicy.WORD_FILTER_POLICY_RECORD, 'spam')));
    expect(sync.wordCount).toBe(0);
    expect(run(sync, 'hello spam')).toEqual({ allowed: true, text: 'hello spam' });
  });

  it('reject policy blocks the send before the wire', async () => {
    const { conn } = makeConn(() =>
      Promise.resolve(fetchResp(lexicon(1, true, WirePolicy.WORD_FILTER_POLICY_REJECT, 'spam'))));
    const sync = new WordFilterSync(conn);
    await sync.fetch();
    expect(run(sync, 'hello spam').allowed).toBe(false);
    expect(run(sync, 'clean').allowed).toBe(true);
  });

  it('loadLocal is the fallback until the first server lexicon supersedes it', async () => {
    let round = 0;
    const { conn } = makeConn(() =>
      Promise.resolve(fetchResp(lexicon(1, true, WirePolicy.WORD_FILTER_POLICY_REPLACE, 'serverbad'))));
    const sync = new WordFilterSync(conn);
    sync.loadLocal(['localbad'], 'reject');
    expect(run(sync, 'x localbad').allowed).toBe(false);
    round = 1;
    await sync.fetch();
    expect(sync.version).toBe(1);
    expect(run(sync, 'x localbad')).toEqual({ allowed: true, text: 'x localbad' });
    expect(run(sync, 'x serverbad').text).toBe('x **');
    expect(round).toBe(1);
  });

  it('fetch failures resolve false and keep the fallback working', async () => {
    const { conn, requests } = makeConn(() => Promise.reject(new RequestError('timeout')));
    const sync = new WordFilterSync(conn, { timeoutMs: 1234 });
    sync.loadLocal(['spam']);
    await expect(sync.fetch()).resolves.toBe(false);
    expect(sync.version).toBe(0);
    expect(requests[0].timeoutMs).toBe(1234);
    expect(run(sync, 'hello spam').text).toBe('hello **');
  });

  it('a non-OK fetch code resolves false and applies nothing', async () => {
    const { conn } = makeConn(() =>
      Promise.resolve({
        code: ErrorCode.AUTH_FAILED,
        lexicon: undefined,
      }));
    const sync = new WordFilterSync(conn);
    await expect(sync.fetch()).resolves.toBe(false);
    expect(sync.version).toBe(0);
    expect(sync.wordCount).toBe(0);
  });

  it('stop() detaches the notify subscription', async () => {
    const { conn, notify } = makeConn(() => Promise.reject(new RequestError('closed')));
    const sync = new WordFilterSync(conn);
    sync.start();
    sync.stop();
    notify(MsgID.WORD_FILTER_UPDATE_NOTIFY, notifyBody(lexicon(9, true, WirePolicy.WORD_FILTER_POLICY_REPLACE, 'late')));
    expect(sync.version).toBe(0);
  });
});
