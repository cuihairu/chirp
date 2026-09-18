import { describe, expect, it, vi } from 'vitest';
import {
  bumpUnread,
  clearUnread,
  createConversationStore,
  touchConversation,
  upsertConversation,
} from './conversation_store';
import { ensureDeviceId } from './auth_store';
import {
  addPendingMessage,
  appendMessage,
  applyDeleteById,
  applyEditById,
  createMessageStore,
  failPendingMessage,
  prependHistory,
} from './message_store';
import { createStore, patch } from './store';
import type { ChatMessageView, Conversation } from './models';

const message = (over: Partial<ChatMessageView>): ChatMessageView => ({
  messageId: 'm1',
  senderId: 'u1',
  channelKey: 'p:a|b',
  channelType: 0,
  channelId: 'a|b',
  content: 'hello',
  timestamp: 100,
  pending: false,
  ...over,
});

describe('createStore', () => {
  it('notifies subscribers on change and supports unsubscribe', () => {
    const store = createStore({ n: 1 });
    const listener = vi.fn();
    const off = store.subscribe(listener);
    store.set({ n: 2 });
    off();
    store.set({ n: 3 });
    expect(listener).toHaveBeenCalledTimes(1);
    expect(store.get().n).toBe(3);
  });

  it('supports functional updates and skips identical snapshots', () => {
    const store = createStore({ n: 1 });
    const listener = vi.fn();
    store.subscribe(listener);
    store.set((prev) => ({ n: prev.n + 1 }));
    store.set((prev) => ({ n: prev.n })); // identical object content? new object → notify
    store.set(store.get()); // same reference → no notify
    expect(listener).toHaveBeenCalledTimes(2);
  });

  it('patches merge partial changes', () => {
    const store = createStore({ a: 1, b: 2 });
    patch(store, { b: 3 });
    expect(store.get()).toEqual({ a: 1, b: 3 });
  });
});

describe('auth device id', () => {
  it('is stable within a browser and always namespaced for web', () => {
    const id = ensureDeviceId();
    expect(id.startsWith('web-')).toBe(true);
    expect(ensureDeviceId()).toBe(id);
  });
});

describe('conversation_store', () => {
  const conversation = (over: Partial<Conversation>): Conversation => ({
    kind: 'private',
    key: 'p:a|b',
    channelId: 'a|b',
    peerId: 'b',
    title: 'b',
    unreadLocal: 0,
    ...over,
  });

  it('upserts and keeps most-recent-first order', () => {
    const store = createConversationStore();
    upsertConversation(store, conversation({ key: 'p:a|b', lastMessageAt: 10 }));
    upsertConversation(store, conversation({ key: 'p:a|c', lastMessageAt: 20 }));
    upsertConversation(store, conversation({ key: 'p:a|b', lastMessageAt: 30 }));
    expect(store.get().conversations.map((c) => c.key)).toEqual(['p:a|b', 'p:a|c']);
  });

  it('upsert keeps the existing unread count', () => {
    const store = createConversationStore();
    upsertConversation(store, conversation({ unreadLocal: 3 }));
    upsertConversation(store, conversation({ unreadLocal: 0, title: 'renamed' }));
    expect(store.get().conversations[0].unreadLocal).toBe(3);
    expect(store.get().conversations[0].title).toBe('renamed');
  });

  it('touch updates only the tail', () => {
    const store = createConversationStore();
    upsertConversation(store, conversation({ lastMessageAt: 10 }));
    touchConversation(store, 'p:missing', 'hi', 99);
    touchConversation(store, 'p:a|b', 'hi', 99);
    expect(store.get().conversations[0].lastMessagePreview).toBe('hi');
    expect(store.get().conversations[0].lastMessageAt).toBe(99);
    expect(store.get().conversations.length).toBe(1);
  });

  it('bump and clear unread', () => {
    const store = createConversationStore();
    upsertConversation(store, conversation({}));
    bumpUnread(store, 'p:a|b');
    bumpUnread(store, 'p:a|b');
    expect(store.get().conversations[0].unreadLocal).toBe(2);
    clearUnread(store, 'p:a|b');
    expect(store.get().conversations[0].unreadLocal).toBe(0);
  });
});

describe('message_store', () => {
  it('appends live messages and dedupes by server id', () => {
    const store = createMessageStore();
    appendMessage(store, message({ messageId: 'm1' }));
    appendMessage(store, message({ messageId: 'm1' }));
    appendMessage(store, message({ messageId: 'm2' }));
    expect(store.get().byChannel['p:a|b'].map((m) => m.messageId)).toEqual(['m1', 'm2']);
  });

  it('replaces the optimistic placeholder via clientId and dedupes by id', () => {
    const store = createMessageStore();
    addPendingMessage(store, message({ messageId: 'pending-1', clientId: 'pending-1', pending: true }));
    appendMessage(store, message({ messageId: 'srv-1', clientId: 'pending-1' }));
    const list = store.get().byChannel['p:a|b'];
    expect(list.length).toBe(1);
    expect(list[0].messageId).toBe('srv-1');
    expect(list[0].pending).toBe(false);
  });

  it('marks pending sends failed', () => {
    const store = createMessageStore();
    addPendingMessage(store, message({ messageId: 'pending-1', clientId: 'pending-1', pending: true }));
    failPendingMessage(store, 'p:a|b', 'pending-1');
    expect(store.get().byChannel['p:a|b'][0].failed).toBe(true);
    expect(store.get().byChannel['p:a|b'][0].pending).toBe(false);
  });

  it('prepends older history without duplicates and tracks hasMore', () => {
    const store = createMessageStore();
    appendMessage(store, message({ messageId: 'm5', timestamp: 500 }));
    prependHistory(
      store,
      'p:a|b',
      [message({ messageId: 'm3', timestamp: 300 }), message({ messageId: 'm5', timestamp: 500 })],
      true,
    );
    expect(store.get().byChannel['p:a|b'].map((m) => m.messageId)).toEqual(['m3', 'm5']);
    expect(store.get().hasMore['p:a|b']).toBe(true);
  });

  it('applies edits and deletes in place by message id', () => {
    const store = createMessageStore();
    appendMessage(store, message({ messageId: 'm1', content: 'before' }));
    applyEditById(store, 'm1', 'after');
    expect(store.get().byChannel['p:a|b'][0].content).toBe('after');
    expect(store.get().byChannel['p:a|b'][0].edited).toBe(true);
    applyDeleteById(store, 'm1');
    expect(store.get().byChannel['p:a|b'][0].deleted).toBe(true);
    expect(store.get().byChannel['p:a|b'][0].content).toBe('');
  });
});
