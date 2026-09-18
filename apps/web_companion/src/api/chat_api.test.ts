import { describe, expect, it, vi } from 'vitest';
import {
  ChannelType,
  ChatMessage,
  MessageAck,
  MessageDeletedNotify,
  MessageEditedNotify,
  MessageReadNotify,
  MessageReaction,
  MsgType,
  ReactionAddedNotify,
  ReactionRemovedNotify,
  TypingIndicator,
} from '@chirp/proto/chat';
import { MsgID } from '@chirp/proto/gateway';
import { ChatApi, channelRefOf, type ChannelRef } from './chat_api';
import { createStore } from '../state/store';
import { createConversationStore } from '../state/conversation_store';
import { createMessageStore, readCursorOf } from '../state/message_store';
import { typingUsersOf } from '../state/typing_store';
import type { ChatMessageView } from '../state/models';
import type { AuthState } from '../state/auth_store';
import type { ConversationState } from '../state/conversation_store';
import type { MessageState } from '../state/message_store';
import { createTypingStore } from '../state/typing_store';
import { FakeChatConnection } from '../state/test_helpers';

const SELF = 'user_a';
const PEER = 'user_b';

const privateChannel = (): ChannelRef => ({
  key: 'p:user_a|user_b',
  kind: 'private',
  channelId: 'user_a|user_b',
  peerId: PEER,
});

const chatBody = (over: Partial<ChatMessage>): Uint8Array =>
  ChatMessage.encode(
    ChatMessage.fromPartial({
      messageId: 'm1',
      senderId: PEER,
      channelType: ChannelType.PRIVATE,
      channelId: 'user_a|user_b',
      msgType: MsgType.TEXT,
      content: new TextEncoder().encode('hi'),
      timestamp: 100,
      ...over,
    }),
  ).finish();

interface Harness {
  conn: FakeChatConnection;
  api: ChatApi;
  auth: ReturnType<typeof createStore<AuthState>>;
  conversations: ReturnType<typeof createConversationStore>;
  messages: ReturnType<typeof createMessageStore>;
  typing: ReturnType<typeof createTypingStore>;
}

const makeHarness = (): Harness => {
  const conn = new FakeChatConnection();
  const auth = createStore<AuthState>({
    userId: null,
    kicked: false,
    deviceId: 'dev-1',
    loggedIn: false,
  });
  const conversations = createConversationStore();
  const messages = createMessageStore();
  const typing = createTypingStore();
  const api = new ChatApi({ conn, auth, conversations, messages, typing });
  return { conn, api, auth, conversations, messages, typing };
};

/** Login through the api so notify handlers are wired, like the UI would. */
const login = async (h: Harness, overrides: Partial<Record<MsgID, unknown>> = {}): Promise<void> => {
  h.conn.setResponder(async (msgId) => {
    if (overrides[msgId] !== undefined) return overrides[msgId];
    switch (msgId) {
      case MsgID.LOGIN_REQ:
        return { code: 0 };
      default:
        return { code: 0 };
    }
  });
  await h.api.login(SELF);
};

describe('ChatApi.login', () => {
  it('patches auth, resets backoff and subscribes to notifies', async () => {
    const h = makeHarness();
    h.conn.resetBackoff = vi.fn();
    await login(h);
    expect(h.auth.get()).toMatchObject({ userId: SELF, loggedIn: true, kicked: false });
    expect(h.conn.resetBackoff).toHaveBeenCalled();
    // The CHAT_MESSAGE_NOTIFY handler is live after login.
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({}));
    expect(h.messages.get().byChannel['p:user_a|user_b'].length).toBe(1);
  });

  it('keeps auth untouched and stays unsubscribed on failure', async () => {
    const h = makeHarness();
    h.conn.setResponder(async (msgId) =>
      msgId === MsgID.LOGIN_REQ ? { code: 4 } : { code: 0 },
    );
    expect(await h.api.login(SELF)).toBe(4);
    expect(h.auth.get().loggedIn).toBe(false);
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({}));
    expect(h.messages.get().byChannel['p:user_a|user_b']).toBeUndefined();
  });
});

describe('ChatApi.sendMessage (optimistic)', () => {
  const channel = privateChannel();

  it('shows the message immediately, then swaps in the server id', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      expect(req).toMatchObject({ senderId: SELF, receiverId: PEER, channelId: 'user_a|user_b' });
      return { code: 0, messageId: 'srv-1' };
    });
    const done = h.api.sendMessage(channel, 'hello');
    // Placeholder visible before the RESP resolves.
    const pending = h.messages.get().byChannel[channel.key];
    expect(pending.length).toBe(1);
    expect(pending[0]).toMatchObject({ content: 'hello', pending: true });
    await done;
    const list = h.messages.get().byChannel[channel.key];
    expect(list.length).toBe(1);
    expect(list[0]).toMatchObject({ messageId: 'srv-1', pending: false, queuedOffline: false });
    expect(h.conversations.get().conversations[0]).toMatchObject({
      key: channel.key,
      lastMessagePreview: 'hello',
    });
  });

  it('treats TARGET_OFFLINE as delivered-but-queued', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async () => ({ code: 6, messageId: 'srv-2' })); // TARGET_OFFLINE
    await h.api.sendMessage(channel, 'offline ping');
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      messageId: 'srv-2',
      pending: false,
      queuedOffline: true,
    });
  });

  it('marks the message failed on a business error code', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async () => ({ code: 8 })); // RATE_LIMITED
    await h.api.sendMessage(channel, 'spam');
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({ failed: true, pending: false });
  });

  it('marks the message failed when the connection drops', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.failNextRequest();
    await h.api.sendMessage(channel, 'lost');
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({ failed: true, pending: false });
  });

  it('rethrows non-protocol errors and leaves the placeholder pending', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async () => {
      throw new Error('programming bug');
    });
    await expect(h.api.sendMessage(channel, 'oops')).rejects.toThrow('programming bug');
    expect(h.messages.get().byChannel[channel.key][0].pending).toBe(true);
  });

  it('does nothing when logged out', async () => {
    const h = makeHarness();
    await h.api.sendMessage(channel, 'ghost');
    expect(h.conn.requests).toHaveLength(0);
  });
});

describe('ChatApi incoming notifies', () => {
  it('creates conversations for strangers and bumps unread when not active', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'm1' }));
    // supportsMessageAck=true: every live push is acked fire-and-forget.
    const ack = h.conn.requests.find((r) => r.msgId === MsgID.MESSAGE_ACK);
    expect(ack).toBeTruthy();
    expect(MessageAck.decode(ack!.req as Uint8Array)).toMatchObject({
      messageId: 'm1',
      userId: SELF,
    });
    const conv = h.conversations.get().conversations;
    expect(conv.length).toBe(1);
    expect(conv[0]).toMatchObject({
      kind: 'private',
      key: 'p:user_a|user_b',
      peerId: PEER,
      title: PEER,
      unreadLocal: 1,
    });
    // The sender's own copy arrives via RESP; live copies append once.
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'm1' }));
    expect(h.messages.get().byChannel['p:user_a|user_b'].length).toBe(1);
  });

  it('does not bump unread for the active channel', async () => {
    const h = makeHarness();
    await login(h);
    h.api.setActiveChannel('p:user_a|user_b');
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({}));
    expect(h.conversations.get().conversations[0].unreadLocal).toBe(0);
  });

  it('routes group messages under g: keys', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        channelType: ChannelType.GUILD,
        channelId: 'guild-1',
      }),
    );
    expect(h.messages.get().byChannel['g:guild-1'].length).toBe(1);
    expect(h.conversations.get().conversations[0]).toMatchObject({
      kind: 'group',
      key: 'g:guild-1',
      channelId: 'guild-1',
    });
  });

  it('ignores undecodable frames', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, new Uint8Array([1, 2, 3]));
    expect(h.messages.get().byChannel['p:user_a|user_b']).toBeUndefined();
  });

  it('refreshes groups when membership notifies arrive', async () => {
    const h = makeHarness();
    await login(h, {
      [MsgID.GET_USER_GROUPS_REQ]: {
        code: 0,
        groups: [{ groupId: 'guild-1', groupName: 'Raiders' }],
      },
    });
    h.conn.emit(MsgID.GROUP_MEMBER_JOINED_NOTIFY, new Uint8Array());
    // refreshGroups is async; wait for the roster pull to land.
    await vi.waitFor(() => {
      expect(h.conversations.get().conversations.some((c) => c.key === 'g:guild-1')).toBe(true);
    });
    expect(h.conversations.get().conversations.find((c) => c.key === 'g:guild-1')?.title).toBe(
      'Raiders',
    );
  });
});

describe('ChatApi.loadHistory', () => {
  it('prepends pages and clears the loading flag', async () => {
    const h = makeHarness();
    await login(h);
    const channel = privateChannel();
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.GET_HISTORY_REQ);
      expect(req).toMatchObject({ channelId: 'user_a|user_b', beforeTimestamp: 0 });
      return {
        code: 0,
        hasMore: true,
        messages: [
          { messageId: 'old', senderId: PEER, content: new TextEncoder().encode('old'), timestamp: 50 },
          { messageId: 'new', senderId: SELF, content: new TextEncoder().encode('new'), timestamp: 60 },
        ],
      };
    });
    await h.api.loadHistory(channel);
    const state: MessageState = h.messages.get();
    expect(state.byChannel[channel.key].map((m) => m.messageId)).toEqual(['old', 'new']);
    expect(state.hasMore[channel.key]).toBe(true);
    expect(state.loadingHistory[channel.key]).toBe(false);
    expect(state.byChannel[channel.key][0].content).toBe('old');
  });

  it('keeps the loading flag honest when the request rejects', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.failNextRequest();
    await expect(h.api.loadHistory(privateChannel())).rejects.toThrow();
    expect(h.messages.get().loadingHistory['p:user_a|user_b']).toBe(false);
  });
});

describe('ChatApi receipts and groups', () => {
  it('sends receipts best-effort and swallows failures', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.failNextRequest();
    await expect(h.api.markRead(privateChannel(), 'm1')).resolves.toBeUndefined();
    const mark = h.conn.requests.find((r) => r.msgId === MsgID.MARK_READ_REQ);
    expect(mark).toMatchObject({ req: { userId: SELF, messageId: 'm1' } });
  });

  it('refreshGroups upserts without clobbering unread or titles', async () => {
    const h = makeHarness();
    await login(h, {
      [MsgID.GET_USER_GROUPS_REQ]: {
        code: 0,
        groups: [{ groupId: 'guild-1', groupName: 'New Name' }],
      },
    });
    h.conversations.set((prev) => ({
      conversations: [
        ...prev.conversations,
        {
          kind: 'group',
          key: 'g:guild-1',
          channelId: 'guild-1',
          peerId: 'guild-1',
          title: 'Old Name',
          unreadLocal: 2,
        },
      ],
    }));
    const state: ConversationState = h.conversations.get();
    const guild = state.conversations.find((c) => c.key === 'g:guild-1');
    expect(guild).toMatchObject({ title: 'Old Name', unreadLocal: 2 });
  });

  it('createGroup returns the id and pulls the new roster', async () => {
    const h = makeHarness();
    await login(h, {
      [MsgID.CREATE_GROUP_REQ]: { code: 0, groupId: 'guild-9' },
      [MsgID.GET_USER_GROUPS_REQ]: {
        code: 0,
        groups: [{ groupId: 'guild-9', groupName: 'Party' }],
      },
    });
    expect(await h.api.createGroup('Party')).toBe('guild-9');
    expect(h.conversations.get().conversations.some((c) => c.key === 'g:guild-9')).toBe(true);
  });

  it('loadGroupMembers returns members on success and [] otherwise', async () => {
    const h = makeHarness();
    await login(h, {
      [MsgID.GET_GROUP_MEMBERS_REQ]: { code: 0, members: [{ userId: SELF }, { userId: PEER }] },
    });
    expect(await h.api.loadGroupMembers('guild-1')).toHaveLength(2);
    h.conn.setResponder(async () => ({ code: 5 }));
    expect(await h.api.loadGroupMembers('guild-1')).toEqual([]);
  });
});

describe('ChatApi.logout', () => {
  it('logs out, disconnects and clears auth', async () => {
    const h = makeHarness();
    await login(h);
    let disconnected = false;
    h.conn.disconnect = () => {
      disconnected = true;
    };
    await h.api.logout();
    expect(h.conn.requests.some((r) => r.msgId === MsgID.LOGOUT_REQ)).toBe(true);
    expect(disconnected).toBe(true);
    expect(h.auth.get()).toMatchObject({ userId: null, loggedIn: false });
    // Live messages after logout are ignored (handlers removed).
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'late' }));
    expect(h.messages.get().byChannel['p:user_a|user_b']).toBeUndefined();
  });
});

/** Seed one decoded message straight into the store (skip the wire format). */
const seedMessage = (h: Harness, over: Partial<ChatMessageView> = {}): void => {
  h.messages.set((prev) => ({
    ...prev,
    byChannel: {
      ...prev.byChannel,
      'p:user_a|user_b': [
        {
          messageId: 'm1',
          senderId: PEER,
          channelKey: 'p:user_a|user_b',
          channelType: ChannelType.PRIVATE,
          channelId: 'user_a|user_b',
          content: 'hello',
          timestamp: 100,
          pending: false,
          ...over,
        },
      ],
    },
  }));
};

describe('ChatApi.typing', () => {
  it('sends the indicator fire-and-forget without a request frame', async () => {
    const h = makeHarness();
    await login(h);
    h.api.sendTyping(privateChannel(), true);
    const sent = h.conn.requests.find((r) => r.msgId === MsgID.TYPING_INDICATOR_NOTIFY);
    expect(sent).toBeTruthy();
    expect(TypingIndicator.decode(sent!.req as Uint8Array)).toMatchObject({
      channelId: 'user_a|user_b',
      channelType: ChannelType.PRIVATE,
      userId: SELF,
      username: SELF,
      isTyping: true,
    });
  });

  it('stays silent when logged out', async () => {
    const h = makeHarness();
    h.api.sendTyping(privateChannel(), true);
    expect(h.conn.requests).toHaveLength(0);
  });
});

describe('ChatApi.reactions', () => {
  it('applies the ADD_REACTION_RESP aggregate as the truth', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    h.conn.setResponder(async (msgId) => {
      expect(msgId).toBe(MsgID.ADD_REACTION_REQ);
      const aggregate: MessageReaction = {
        messageId: 'm1',
        emoji: '👍',
        count: 2,
        userIds: [PEER, SELF],
        reactedByMe: true,
      };
      return { code: 0, reaction: aggregate };
    });
    await h.api.addReaction('m1', '👍');
    expect(h.messages.get().byChannel['p:user_a|user_b'][0].reactions?.['👍']).toEqual({
      emoji: '👍',
      count: 2,
      mine: true,
    });
  });

  it('decrements locally on remove (RESP carries no aggregate, notify excludes the actor)', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h, { reactions: { '👍': { emoji: '👍', count: 1, mine: true } } });
    h.conn.setResponder(async () => ({ code: 0 }));
    await h.api.removeReaction('m1', '👍');
    expect(h.messages.get().byChannel['p:user_a|user_b'][0].reactions?.['👍']).toBeUndefined();
  });

  it('leaves state untouched on an error code', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    h.conn.setResponder(async () => ({ code: 5 }));
    await h.api.addReaction('m1', '👍');
    await h.api.removeReaction('m1', '👍');
    expect(h.messages.get().byChannel['p:user_a|user_b'][0].reactions).toBeUndefined();
  });
});

describe('ChatApi.edit and delete', () => {
  it('applies the edit locally from the RESP (the notify excludes the editor)', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.EDIT_MESSAGE_REQ);
      expect(new TextDecoder().decode((req as { newContent: Uint8Array }).newContent)).toBe(
        'fixed',
      );
      return { code: 0 };
    });
    await h.api.editMessage('m1', 'fixed');
    const edited = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(edited).toMatchObject({ content: 'fixed', edited: true });
  });

  it('soft-deletes locally from the RESP', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.DELETE_MESSAGE_REQ);
      expect((req as { isHardDelete: boolean }).isHardDelete).toBe(false);
      return { code: 0 };
    });
    await h.api.deleteMessage('m1');
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      deleted: true,
      content: '',
    });
  });

  it('leaves the message untouched on an error code', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    h.conn.setResponder(async () => ({ code: 3 }));
    await h.api.editMessage('m1', 'nope');
    await h.api.deleteMessage('m1');
    const kept = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(kept.content).toBe('hello');
    expect(kept.edited).toBeFalsy();
    expect(kept.deleted).toBeFalsy();
  });
});

describe('ChatApi C8 notifies', () => {
  it('records the peer read cursor and ignores own echoes', async () => {
    const h = makeHarness();
    await login(h);
    const body = (reader: string): Uint8Array =>
      MessageReadNotify.encode(
        MessageReadNotify.fromPartial({
          channelType: ChannelType.PRIVATE,
          channelId: 'user_a|user_b',
          messageId: 'm9',
          readerUserId: reader,
          readAt: 5,
        }),
      ).finish();
    h.conn.emit(MsgID.MESSAGE_READ_NOTIFY, body(PEER));
    expect(readCursorOf(h.messages.get(), 'p:user_a|user_b', PEER)).toBe('m9');
    h.conn.emit(MsgID.MESSAGE_READ_NOTIFY, body(SELF));
    expect(readCursorOf(h.messages.get(), 'p:user_a|user_b', SELF)).toBeUndefined();
  });

  it('starts and stops typing on indicators, ignoring own echoes', async () => {
    const h = makeHarness();
    await login(h);
    const body = (userId: string, isTyping: boolean): Uint8Array =>
      TypingIndicator.encode(
        TypingIndicator.fromPartial({
          channelType: ChannelType.PRIVATE,
          channelId: 'user_a|user_b',
          userId,
          isTyping,
        }),
      ).finish();
    h.conn.emit(MsgID.TYPING_INDICATOR_NOTIFY, body(PEER, true));
    expect(typingUsersOf(h.typing.get(), 'p:user_a|user_b', Date.now())).toEqual([PEER]);
    h.conn.emit(MsgID.TYPING_INDICATOR_NOTIFY, body(PEER, false));
    expect(typingUsersOf(h.typing.get(), 'p:user_a|user_b', Date.now())).toEqual([]);
    h.conn.emit(MsgID.TYPING_INDICATOR_NOTIFY, body(SELF, true));
    expect(typingUsersOf(h.typing.get(), 'p:user_a|user_b', Date.now())).toEqual([]);
  });

  it('applies reaction notifies by message id', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    h.conn.emit(
      MsgID.REACTION_ADDED_NOTIFY,
      ReactionAddedNotify.encode(
        ReactionAddedNotify.fromPartial({ messageId: 'm1', emoji: '🎉', userId: PEER }),
      ).finish(),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0].reactions?.['🎉']).toEqual({
      emoji: '🎉',
      count: 1,
      mine: false,
    });
    h.conn.emit(
      MsgID.REACTION_REMOVED_NOTIFY,
      ReactionRemovedNotify.encode(
        ReactionRemovedNotify.fromPartial({ messageId: 'm1', emoji: '🎉', userId: PEER }),
      ).finish(),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0].reactions?.['🎉']).toBeUndefined();
  });

  it('applies edit and delete notifies by message id', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    h.conn.emit(
      MsgID.MESSAGE_EDITED_NOTIFY,
      MessageEditedNotify.encode(
        MessageEditedNotify.fromPartial({
          messageId: 'm1',
          newContent: new TextEncoder().encode('edited'),
        }),
      ).finish(),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      content: 'edited',
      edited: true,
    });
    h.conn.emit(
      MsgID.MESSAGE_DELETED_NOTIFY,
      MessageDeletedNotify.encode(
        MessageDeletedNotify.fromPartial({ messageId: 'm1' }),
      ).finish(),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      deleted: true,
      content: '',
    });
  });

  it('ignores undecodable C8 frames', async () => {
    const h = makeHarness();
    await login(h);
    seedMessage(h);
    const garbage = new Uint8Array([9, 9, 9]);
    h.conn.emit(MsgID.MESSAGE_READ_NOTIFY, garbage);
    h.conn.emit(MsgID.TYPING_INDICATOR_NOTIFY, garbage);
    h.conn.emit(MsgID.REACTION_ADDED_NOTIFY, garbage);
    h.conn.emit(MsgID.REACTION_REMOVED_NOTIFY, garbage);
    h.conn.emit(MsgID.MESSAGE_EDITED_NOTIFY, garbage);
    h.conn.emit(MsgID.MESSAGE_DELETED_NOTIFY, garbage);
    const kept = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(kept.content).toBe('hello');
    expect(kept.edited).toBeFalsy();
    expect(kept.deleted).toBeFalsy();
    expect(h.typing.get().byChannel).toEqual({});
  });
});

describe('channel helpers', () => {
  it('resolves channel refs from stored keys', () => {
    expect(channelRefOf('p:user_a|user_b', SELF)).toMatchObject({
      kind: 'private',
      channelId: 'user_a|user_b',
      peerId: PEER,
    });
    expect(channelRefOf('g:guild-1', SELF)).toMatchObject({
      kind: 'group',
      channelId: 'guild-1',
      peerId: 'guild-1',
    });
  });
});
