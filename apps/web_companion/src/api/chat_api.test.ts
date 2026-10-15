import { describe, expect, it, vi } from 'vitest';
import {
  ChannelType,
  ChatMessage,
  CoordMetadata,
  GroupMemberAliasUpdatedNotify,
  GroupMemberKickedNotify,
  GroupMemberMutedNotify,
  AchievementMetadata,
  GuildRecruitMetadata,
  ItemMetadata,
  PartyRecruitMetadata,
  TradeMetadata,
  MessageAck,
  MessageDeletedNotify,
  MessageEditedNotify,
  MessageReadNotify,
  MessageReaction,
  MsgType,
  Priority,
  ReactionAddedNotify,
  ReactionRemovedNotify,
  SenderKind,
  TypingIndicator,
} from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { MsgID } from '@chirp/proto/gateway';
import { ChatApi, channelRefOf, type ChannelRef, type SystemSurface } from './chat_api';
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

  it('declares the login level on the wire request', async () => {
    // 世界频道等级门槛（game_chat_features P1）：level 随 LOGIN 上行；
    // 伴侣面缺省 0 = 未声明。
    const h = makeHarness();
    h.conn.setResponder(async () => ({ code: 0 }));
    await h.api.login(SELF, 9);
    const req = h.conn.requests.find((r) => r.msgId === MsgID.LOGIN_REQ);
    expect((req!.req as { level?: number }).level).toBe(9);

    const plain = makeHarness();
    plain.conn.setResponder(async () => ({ code: 0 }));
    await plain.api.login(SELF);
    const req2 = plain.conn.requests.find((r) => r.msgId === MsgID.LOGIN_REQ);
    expect((req2!.req as { level?: number }).level).toBe(0);
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
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      failed: true,
      pending: false,
      failureReason: undefined,
    });
  });

  it('tags a MUTED rejection with the mute failure reason', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async () => ({ code: 12 })); // MUTED
    await h.api.sendMessage(channel, 'while muted');
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      failed: true,
      pending: false,
      failureReason: 'muted',
    });
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

  it('carries the quoted id on the wire and through the views', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (_msgId, req) => {
      expect(req).toMatchObject({ replyToMessageId: 'm-9' });
      return { code: 0, messageId: 'srv-r1' };
    });
    await h.api.sendMessage(channel, 'a reply', 'm-9');
    const list = h.messages.get().byChannel[channel.key];
    expect(list[0]).toMatchObject({ messageId: 'srv-r1', replyToMessageId: 'm-9' });
  });

  it('leaves the reply field off the wire for plain sends', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (_msgId, req) => {
      expect((req as { replyToMessageId?: string }).replyToMessageId).toBeUndefined();
      return { code: 0, messageId: 'srv-p1' };
    });
    await h.api.sendMessage(channel, 'plain');
    const list = h.messages.get().byChannel[channel.key];
    expect(list[0].replyToMessageId).toBeUndefined();
  });

  it('sends COORD_LINK with coord metadata and shows the card optimistically', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      expect(req).toMatchObject({ msgType: MsgType.COORD_LINK });
      return { code: 0, messageId: 'srv-c1' };
    });
    await h.api.sendMessage(channel, '矿点', undefined, {
      mapId: 'field-7',
      x: 120,
      y: 88,
      z: 3,
      label: '矿点',
    });
    const wire = h.conn.requests.find((r) => r.msgId === MsgID.SEND_MESSAGE_REQ)!;
    const coord = CoordMetadata.decode((wire.req as { metadata?: Uint8Array }).metadata ?? new Uint8Array());
    expect(coord.mapId).toBe('field-7');
    expect(coord.x).toBe(120);
    expect(coord.y).toBe(88);
    expect(coord.z).toBe(3);
    expect(coord.label).toBe('矿点');
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      messageId: 'srv-c1',
      coord: { mapId: 'field-7', x: 120, y: 88, z: 3, label: '矿点' },
    });
  });

  it('sends ITEM_LINK with item metadata and shows the card optimistically', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      expect(req).toMatchObject({ msgType: MsgType.ITEM_LINK });
      return { code: 0, messageId: 'srv-i1' };
    });
    await h.api.sendMessage(channel, '出了把橙武', undefined, undefined, {
      itemId: 'item-9001',
      itemName: '烈焰之刃',
      quality: 4,
      count: 2,
      attrs: { 攻击力: '120' },
    });
    const wire = h.conn.requests.find((r) => r.msgId === MsgID.SEND_MESSAGE_REQ)!;
    const item = ItemMetadata.decode(
      (wire.req as { metadata?: Uint8Array }).metadata ?? new Uint8Array(),
    );
    expect(item.itemId).toBe('item-9001');
    expect(item.itemName).toBe('烈焰之刃');
    expect(item.quality).toBe(4);
    expect(item.count).toBe(2);
    expect(item.attrs['攻击力']).toBe('120');
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      messageId: 'srv-i1',
      item: { itemId: 'item-9001', itemName: '烈焰之刃', quality: 4, count: 2 },
    });
  });

  it('sends ACHIEVEMENT with achievement metadata and shows the card optimistically', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      expect(req).toMatchObject({ msgType: MsgType.ACHIEVEMENT });
      return { code: 0, messageId: 'srv-a1' };
    });
    await h.api.sendMessage(channel, '解锁了新成就', undefined, undefined, undefined, {
      achievementId: 'achievement-7001',
      achievementName: '全服首杀·烈焰巨龙',
      description: '首次击杀世界 BOSS 烈焰巨龙',
      rarity: 3,
    });
    const wire = h.conn.requests.find((r) => r.msgId === MsgID.SEND_MESSAGE_REQ)!;
    const achievement = AchievementMetadata.decode(
      (wire.req as { metadata?: Uint8Array }).metadata ?? new Uint8Array(),
    );
    expect(achievement.achievementId).toBe('achievement-7001');
    expect(achievement.achievementName).toBe('全服首杀·烈焰巨龙');
    expect(achievement.rarity).toBe(3);
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      messageId: 'srv-a1',
      achievement: { achievementId: 'achievement-7001', achievementName: '全服首杀·烈焰巨龙', rarity: 3 },
    });
  });

  it('sends TRADE_STATUS with trade metadata and shows the card optimistically', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      expect(req).toMatchObject({ msgType: MsgType.TRADE_STATUS });
      return { code: 0, messageId: 'srv-tr1' };
    });
    await h.api.sendMessage(
      channel,
      '交易已成交',
      undefined,
      undefined,
      undefined,
      undefined,
      { tradeId: 'trade-9001', status: 'COMPLETED', amount: 1500, itemName: '烈焰剑', itemCount: 2 },
    );
    const wire = h.conn.requests.find((r) => r.msgId === MsgID.SEND_MESSAGE_REQ)!;
    const trade = TradeMetadata.decode(
      (wire.req as { metadata?: Uint8Array }).metadata ?? new Uint8Array(),
    );
    expect(trade.tradeId).toBe('trade-9001');
    expect(trade.status).toBe('COMPLETED');
    expect(trade.amount).toBe(1500);
    expect(trade.itemName).toBe('烈焰剑');
    expect(trade.itemCount).toBe(2);
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      messageId: 'srv-tr1',
      trade: { tradeId: 'trade-9001', status: 'COMPLETED', amount: 1500, itemName: '烈焰剑', itemCount: 2 },
    });
  });

  it('sends GUILD_RECRUIT with guild metadata and shows the card optimistically', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEND_MESSAGE_REQ);
      expect(req).toMatchObject({ msgType: MsgType.GUILD_RECRUIT });
      return { code: 0, messageId: 'srv-g1' };
    });
    await h.api.sendMessage(
      channel,
      '招募成员',
      undefined,
      undefined,
      undefined,
      undefined,
      undefined,
      {
        guildId: 'guild-3001',
        guildName: '烈焰军团',
        level: 7,
        memberCount: 48,
        maxMembers: 60,
        description: '周三晚开荒',
      },
    );
    const wire = h.conn.requests.find((r) => r.msgId === MsgID.SEND_MESSAGE_REQ)!;
    const guild = GuildRecruitMetadata.decode(
      (wire.req as { metadata?: Uint8Array }).metadata ?? new Uint8Array(),
    );
    expect(guild.guildId).toBe('guild-3001');
    expect(guild.guildName).toBe('烈焰军团');
    expect(guild.level).toBe(7);
    expect(guild.maxMembers).toBe(60);
    expect(h.messages.get().byChannel[channel.key][0]).toMatchObject({
      messageId: 'srv-g1',
      guildRecruit: {
        guildId: 'guild-3001',
        guildName: '烈焰军团',
        level: 7,
        memberCount: 48,
        maxMembers: 60,
      },
    });
  });

  it('leaves plain sends as TEXT without metadata on the wire', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (_msgId, req) => {
      expect(req).toMatchObject({ msgType: MsgType.TEXT });
      expect((req as { metadata?: Uint8Array }).metadata).toBeUndefined();
      return { code: 0, messageId: 'srv-t1' };
    });
    await h.api.sendMessage(channel, 'plain text');
    expect(h.messages.get().byChannel[channel.key][0].coord).toBeUndefined();
  });
});

describe('ChatApi incoming notifies', () => {
  it('parses the coordinate card out of a COORD_LINK notify metadata', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        msgType: MsgType.COORD_LINK,
        content: new TextEncoder().encode('矿点'),
        metadata: CoordMetadata.encode(
          CoordMetadata.fromPartial({ mapId: 'field-7', x: 120, y: 88, label: '矿点' }),
        ).finish(),
      }),
    );
    const view = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(view).toMatchObject({ coord: { mapId: 'field-7', x: 120, y: 88, z: 0, label: '矿点' } });
  });

  it('drops the coordinate card when the metadata does not parse', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ msgType: MsgType.COORD_LINK, metadata: new Uint8Array([0xde, 0xad, 0xbe, 0xef]) }),
    );
    const view = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(view.coord).toBeUndefined();
    expect(view.content).toBe('hi');
  });

  it('parses the item card out of an ITEM_LINK notify metadata', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        msgType: MsgType.ITEM_LINK,
        content: new TextEncoder().encode('出了把橙武'),
        metadata: ItemMetadata.encode(
          ItemMetadata.fromPartial({
            itemId: 'item-9001',
            itemName: '烈焰之刃',
            quality: 4,
            count: 2,
            attrs: { 攻击力: '120' },
          }),
        ).finish(),
      }),
    );
    const view = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(view).toMatchObject({
      item: { itemId: 'item-9001', itemName: '烈焰之刃', quality: 4, count: 2, attrs: { 攻击力: '120' } },
    });
  });

  it('drops the item card when the metadata does not parse or lacks item_id', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ msgType: MsgType.ITEM_LINK, metadata: new Uint8Array([0xde, 0xad, 0xbe, 0xef]) }),
    );
    const first = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(first.item).toBeUndefined();
    expect(first.content).toBe('hi');

    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm2',
        msgType: MsgType.ITEM_LINK,
        metadata: ItemMetadata.encode(ItemMetadata.fromPartial({ itemName: '无名之物' })).finish(),
      }),
    );
    const second = h.messages.get().byChannel['p:user_a|user_b'][1];
    expect(second.item).toBeUndefined();
    expect(second.content).toBe('hi');
  });

  it('parses the achievement card out of an ACHIEVEMENT notify metadata', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        msgType: MsgType.ACHIEVEMENT,
        content: new TextEncoder().encode('解锁了新成就'),
        metadata: AchievementMetadata.encode(
          AchievementMetadata.fromPartial({
            achievementId: 'achievement-7001',
            achievementName: '全服首杀·烈焰巨龙',
            description: '首次击杀世界 BOSS 烈焰巨龙',
            rarity: 3,
          }),
        ).finish(),
      }),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      achievement: {
        achievementId: 'achievement-7001',
        achievementName: '全服首杀·烈焰巨龙',
        rarity: 3,
      },
    });
  });

  it('drops the achievement card when the metadata does not parse or lacks achievement_id', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ msgType: MsgType.ACHIEVEMENT, metadata: new Uint8Array([0xde, 0xad, 0xbe, 0xef]) }),
    );
    const first = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(first.achievement).toBeUndefined();
    expect(first.content).toBe('hi');

    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm2',
        msgType: MsgType.ACHIEVEMENT,
        metadata: AchievementMetadata.encode(
          AchievementMetadata.fromPartial({ achievementName: '无名成就' }),
        ).finish(),
      }),
    );
    const second = h.messages.get().byChannel['p:user_a|user_b'][1];
    expect(second.achievement).toBeUndefined();
    expect(second.content).toBe('hi');
  });

  it('parses the trade card out of a TRADE_STATUS notify metadata', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        msgType: MsgType.TRADE_STATUS,
        content: new TextEncoder().encode('交易已成交'),
        metadata: TradeMetadata.encode(
          TradeMetadata.fromPartial({
            tradeId: 'trade-9001',
            status: 'COMPLETED',
            amount: 1500,
            itemName: '烈焰剑',
            itemCount: 2,
          }),
        ).finish(),
      }),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      trade: {
        tradeId: 'trade-9001',
        status: 'COMPLETED',
        amount: 1500,
        itemName: '烈焰剑',
        itemCount: 2,
      },
    });
  });

  it('drops the trade card when the metadata does not parse or lacks trade_id', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ msgType: MsgType.TRADE_STATUS, metadata: new Uint8Array([0xde, 0xad, 0xbe, 0xef]) }),
    );
    const first = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(first.trade).toBeUndefined();
    expect(first.content).toBe('hi');

    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm2',
        msgType: MsgType.TRADE_STATUS,
        metadata: TradeMetadata.encode(
          TradeMetadata.fromPartial({ status: 'CREATED', amount: 100 }),
        ).finish(),
      }),
    );
    const second = h.messages.get().byChannel['p:user_a|user_b'][1];
    expect(second.trade).toBeUndefined();
    expect(second.content).toBe('hi');
  });

  it('parses the recruit cards out of GUILD/PARTY_RECRUIT notify metadata', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        msgType: MsgType.GUILD_RECRUIT,
        content: new TextEncoder().encode('招募成员'),
        metadata: GuildRecruitMetadata.encode(
          GuildRecruitMetadata.fromPartial({
            guildId: 'guild-3001',
            guildName: '烈焰军团',
            level: 7,
            memberCount: 48,
            maxMembers: 60,
          }),
        ).finish(),
      }),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      guildRecruit: { guildId: 'guild-3001', guildName: '烈焰军团', level: 7, memberCount: 48, maxMembers: 60 },
    });

    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm2',
        msgType: MsgType.PARTY_RECRUIT,
        metadata: PartyRecruitMetadata.encode(
          PartyRecruitMetadata.fromPartial({
            partyId: 'party-4001',
            dungeonName: '熔岩之心',
            needed: 5,
            current: 3,
            leaderName: 'alice',
          }),
        ).finish(),
      }),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][1]).toMatchObject({
      partyRecruit: { partyId: 'party-4001', dungeonName: '熔岩之心', needed: 5, current: 3, leaderName: 'alice' },
    });
  });

  it('drops the recruit cards when the metadata does not parse or lacks ids', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ msgType: MsgType.GUILD_RECRUIT, metadata: new Uint8Array([0xde, 0xad, 0xbe, 0xef]) }),
    );
    const first = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(first.guildRecruit).toBeUndefined();
    expect(first.content).toBe('hi');

    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm2',
        msgType: MsgType.PARTY_RECRUIT,
        metadata: PartyRecruitMetadata.encode(
          PartyRecruitMetadata.fromPartial({ dungeonName: '无名副本' }),
        ).finish(),
      }),
    );
    const second = h.messages.get().byChannel['p:user_a|user_b'][1];
    expect(second.partyRecruit).toBeUndefined();
    expect(second.content).toBe('hi');
  });

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

  it('maps the quoted id from notifies into the view', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ replyToMessageId: 'm-0' }));
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      replyToMessageId: 'm-0',
    });
    // Empty on the wire = not a reply: absent from the view.
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'm2' }));
    expect(h.messages.get().byChannel['p:user_a|user_b'][1].replyToMessageId).toBeUndefined();
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

describe('ChatApi system display surfaces (P0)', () => {
  it('fires marquee for URGENT with ttl and carries the wire fields on the view', async () => {
    const h = makeHarness();
    await login(h);
    const surfaces: SystemSurface[] = [];
    h.api.onSystemSurface((s) => surfaces.push(s));
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm9',
        priority: Priority.PRIORITY_URGENT,
        ttlSeconds: 12,
        senderKind: SenderKind.SENDER_SYSTEM,
        content: new TextEncoder().encode('维护公告'),
      }),
    );
    expect(surfaces).toEqual([
      { type: 'marquee', content: '维护公告', ttlSeconds: 12, messageId: 'm9' },
    ]);
    const view = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(view).toMatchObject({
      priority: Priority.PRIORITY_URGENT,
      ttlSeconds: 12,
      senderKind: SenderKind.SENDER_SYSTEM,
    });
  });

  it('fires announce for HIGH priority, announce wins over the marquee rule', async () => {
    const h = makeHarness();
    await login(h);
    const surfaces: SystemSurface[] = [];
    h.api.onSystemSurface((s) => surfaces.push(s));
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm10',
        priority: Priority.PRIORITY_HIGH,
        ttlSeconds: 30,
        content: new TextEncoder().encode('全服奖励'),
      }),
    );
    expect(surfaces).toEqual([
      { type: 'announce', content: '全服奖励', ttlSeconds: 0, messageId: 'm10' },
    ]);
  });

  it('keeps normal and URGENT-without-ttl messages off the system surfaces', async () => {
    const h = makeHarness();
    await login(h);
    const surfaces: SystemSurface[] = [];
    h.api.onSystemSurface((s) => surfaces.push(s));
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'm11' }));
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ messageId: 'm12', priority: Priority.PRIORITY_URGENT }),
    );
    expect(surfaces).toEqual([]);
  });

  it('stops firing after unsubscribe', async () => {
    const h = makeHarness();
    await login(h);
    const surfaces: SystemSurface[] = [];
    const off = h.api.onSystemSurface((s) => surfaces.push(s));
    off();
    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({ messageId: 'm13', priority: Priority.PRIORITY_HIGH }),
    );
    expect(surfaces).toEqual([]);
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

  it('createGroup forwards ttl seconds and the roster carries the expiry', async () => {
    const h = makeHarness();
    await login(h, {
      [MsgID.CREATE_GROUP_REQ]: { code: 0, groupId: 'guild-ttl' },
      [MsgID.GET_USER_GROUPS_REQ]: {
        code: 0,
        groups: [{ groupId: 'guild-ttl', groupName: 'Raid', expiresAt: 1792032098833 }],
      },
    });
    expect(await h.api.createGroup('Raid', '', 3600)).toBe('guild-ttl');
    const create = h.conn.requests.find((r) => r.msgId === MsgID.CREATE_GROUP_REQ);
    expect(create?.req).toMatchObject({ ttlSeconds: 3600 });
    expect(
      h.conversations.get().conversations.find((c) => c.key === 'g:guild-ttl')?.expiresAt,
    ).toBe(1792032098833);
  });

  it('refreshGroups prunes group conversations missing from the roster', async () => {
    const h = makeHarness();
    await login(h, { [MsgID.GET_USER_GROUPS_REQ]: { code: 0, groups: [] } });
    h.conversations.set((prev) => ({
      conversations: [
        ...prev.conversations,
        {
          kind: 'group',
          key: 'g:gone',
          channelId: 'gone',
          peerId: 'gone',
          title: 'Gone',
          unreadLocal: 1,
        },
        {
          kind: 'private',
          key: 'p:user_a|user_x',
          channelId: 'user_a|user_x',
          peerId: 'user_x',
          title: 'user_x',
          unreadLocal: 0,
        },
      ],
    }));

    await h.api.refreshGroups();

    const keys = h.conversations.get().conversations.map((c) => c.key);
    // 名册外群（临时频道到点 / 退群）本地消失；私聊不参与名册修剪。
    expect(keys).not.toContain('g:gone');
    expect(keys).toContain('p:user_a|user_x');
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

describe('ChatApi.groups C9', () => {
  const seedGroup = (h: Harness, ownerId = SELF): void => {
    h.conversations.set((prev) => ({
      conversations: [
        ...prev.conversations,
        {
          kind: 'group',
          key: 'g:guild-1',
          channelId: 'guild-1',
          peerId: 'guild-1',
          title: 'Raiders',
          ownerId,
          unreadLocal: 0,
        },
      ],
    }));
    h.messages.set((prev) => ({
      ...prev,
      byChannel: {
        ...prev.byChannel,
        'g:guild-1': [
          {
            messageId: 'gm1',
            senderId: PEER,
            channelKey: 'g:guild-1',
            channelType: ChannelType.GUILD,
            channelId: 'guild-1',
            content: 'hello guild',
            timestamp: 100,
            pending: false,
          },
        ],
      },
    }));
  };

  it('inviteToGroup adds directly and refreshes the roster on success', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.INVITE_TO_GROUP_REQ) {
        expect(req).toMatchObject({ inviterId: SELF, groupId: 'guild-1', targetUserId: PEER });
        return { code: 0 };
      }
      return { code: 0, groups: [] };
    });
    expect(await h.api.inviteToGroup('guild-1', PEER)).toBe(0);
    // The roster pull after a successful invite.
    expect(h.conn.requests.some((r) => r.msgId === MsgID.GET_USER_GROUPS_REQ)).toBe(true);
  });

  it('inviteToGroup surfaces the error code without a roster refresh', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async () => ({ code: 3 }));
    expect(await h.api.inviteToGroup('guild-1', PEER)).toBe(3);
    expect(h.conn.requests.some((r) => r.msgId === MsgID.GET_USER_GROUPS_REQ)).toBe(false);
  });

  it('kickMember refreshes the roster on success', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      if (msgId === MsgID.KICK_MEMBER_REQ) {
        expect(req).toMatchObject({ requesterId: SELF, groupId: 'guild-1', targetUserId: PEER });
        return { code: 0 };
      }
      return { code: 0, groups: [] };
    });
    expect(await h.api.kickMember('guild-1', PEER)).toBe(0);
    expect(h.conn.requests.some((r) => r.msgId === MsgID.GET_USER_GROUPS_REQ)).toBe(true);
  });

  it('leaveGroup drops the conversation and cached messages on success', async () => {
    const h = makeHarness();
    await login(h);
    seedGroup(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.LEAVE_GROUP_REQ);
      expect(req).toMatchObject({ userId: SELF, groupId: 'guild-1' });
      return { code: 0 };
    });
    h.api.setActiveChannel('g:guild-1');
    expect(await h.api.leaveGroup('guild-1')).toBe(0);
    // The MEMBER_LEFT notify excludes the actor, so the drop is local.
    expect(h.conversations.get().conversations.some((c) => c.key === 'g:guild-1')).toBe(false);
    expect(h.messages.get().byChannel['g:guild-1']).toBeUndefined();
  });

  it('leaveGroup keeps everything on an error code', async () => {
    const h = makeHarness();
    await login(h);
    seedGroup(h);
    h.conn.setResponder(async () => ({ code: 5 }));
    expect(await h.api.leaveGroup('guild-1')).toBe(5);
    expect(h.conversations.get().conversations.some((c) => c.key === 'g:guild-1')).toBe(true);
    expect(h.messages.get().byChannel['g:guild-1']).toHaveLength(1);
  });

  it('forgets the group locally when kicked (2120), refreshes for others', async () => {
    const h = makeHarness();
    await login(h);
    seedGroup(h);
    const kickedBody = (userId: string): Uint8Array =>
      GroupMemberKickedNotify.encode(
        GroupMemberKickedNotify.fromPartial({ groupId: 'guild-1', userId, kickedBy: SELF }),
      ).finish();
    h.conn.emit(MsgID.GROUP_MEMBER_KICKED_NOTIFY, kickedBody(PEER));
    await vi.waitFor(() =>
      expect(h.conn.requests.some((r) => r.msgId === MsgID.GET_USER_GROUPS_REQ)).toBe(true),
    );
    // Someone else was kicked: my membership stays.
    expect(h.conversations.get().conversations.some((c) => c.key === 'g:guild-1')).toBe(true);

    h.conn.emit(MsgID.GROUP_MEMBER_KICKED_NOTIFY, kickedBody(SELF));
    expect(h.conversations.get().conversations.some((c) => c.key === 'g:guild-1')).toBe(false);
    expect(h.messages.get().byChannel['g:guild-1']).toBeUndefined();
  });

  it('refreshes the roster on GROUP_UPDATED (2121)', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.emit(MsgID.GROUP_UPDATED_NOTIFY, new Uint8Array());
    await vi.waitFor(() =>
      expect(h.conn.requests.some((r) => r.msgId === MsgID.GET_USER_GROUPS_REQ)).toBe(true),
    );
  });

  it('refreshGroups keeps group ownerId for the UI', async () => {
    const h = makeHarness();
    await login(h, {
      [MsgID.GET_USER_GROUPS_REQ]: {
        code: 0,
        groups: [{ groupId: 'guild-1', groupName: 'Raiders', ownerId: SELF }],
      },
    });
    await h.api.refreshGroups();
    expect(h.conversations.get().conversations[0]).toMatchObject({
      key: 'g:guild-1',
      ownerId: SELF,
    });
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

describe('ChatApi.searchMessages', () => {
  it('short-circuits without login or empty keyword, sending nothing', async () => {
    const h = makeHarness();
    const expired = await h.api.searchMessages('boss');
    expect(expired).toMatchObject({ code: ErrorCode.SESSION_EXPIRED, matches: [], hasMore: false });
    expect(h.conn.requests).toHaveLength(0); // nothing sent while logged out

    await login(h);
    const loggedInRequests = h.conn.requests.length;
    const empty = await h.api.searchMessages('');
    expect(empty).toMatchObject({ code: ErrorCode.INVALID_PARAM, matches: [], hasMore: false });
    expect(h.conn.requests.length).toBe(loggedInRequests); // no packet for empty keyword
  });

  it('sends SEARCH_MESSAGE_REQ with defaults and passes the response through', async () => {
    const h = makeHarness();
    await login(h);
    const match = {
      messageId: 'm9',
      channelId: 'world',
      channelType: ChannelType.WORLD,
      senderId: PEER,
      senderKind: 0,
      msgType: MsgType.TEXT,
      timestamp: 1234,
      content: 'boss is up',
    };
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEARCH_MESSAGE_REQ);
      expect(req).toMatchObject({
        keyword: 'boss',
        channelId: '',
        contentTypes: [],
        beforeTimestamp: 0,
        beforeMessageId: '',
        limit: 20,
      });
      return { code: 0, matches: [match], hasMore: true };
    });
    const resp = await h.api.searchMessages('boss');
    expect(resp).toMatchObject({ code: 0, hasMore: true });
    expect(resp.matches).toHaveLength(1);
    expect(resp.matches[0]).toMatchObject(match);
  });

  it('forwards full options in one packet', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SEARCH_MESSAGE_REQ);
      expect(req).toMatchObject({
        keyword: 'loot',
        channelId: 'g:guild-1',
        contentTypes: [MsgType.TEXT, MsgType.ITEM_LINK],
        beforeTimestamp: 42,
        beforeMessageId: 'm8',
        limit: 5,
      });
      return { code: 0, matches: [], hasMore: false };
    });
    const resp = await h.api.searchMessages('loot', {
      channelId: 'g:guild-1',
      contentTypes: [MsgType.TEXT, MsgType.ITEM_LINK],
      beforeTimestamp: 42,
      beforeMessageId: 'm8',
      limit: 5,
    });
    expect(resp.matches).toEqual([]);
  });
});

describe('ChatApi.setMemberAlias', () => {
  it('short-circuits without login, echoing the request fields', async () => {
    const h = makeHarness();
    const resp = await h.api.setMemberAlias('guild-1', PEER, '苍老师');
    expect(resp).toMatchObject({
      code: ErrorCode.SESSION_EXPIRED,
      groupId: 'guild-1',
      userId: PEER,
      alias: '苍老师',
    });
    expect(h.conn.requests).toHaveLength(0);
  });

  it('sends SET_MEMBER_ALIAS_REQ and passes the response through', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SET_MEMBER_ALIAS_REQ);
      expect(req).toMatchObject({ groupId: 'guild-1', targetUserId: PEER, alias: '苍老师' });
      return { code: 0, groupId: 'guild-1', userId: PEER, alias: '苍老师' };
    });
    expect(await h.api.setMemberAlias('guild-1', PEER, '苍老师')).toMatchObject({ code: 0 });
  });
});

describe('ChatApi group alias notify', () => {
  it('refreshes groups on a well-formed 2124 notify', async () => {
    const h = makeHarness();
    await login(h);
    const before = h.conn.requests.length;
    const notify = GroupMemberAliasUpdatedNotify.encode(
      GroupMemberAliasUpdatedNotify.fromPartial({ groupId: 'guild-1', userId: PEER, alias: '苍' }),
    ).finish();
    h.conn.emit(MsgID.GROUP_MEMBER_ALIAS_UPDATED_NOTIFY, notify);
    await vi.waitFor(() => {
      expect(h.conn.requests.length).toBeGreaterThan(before);
    });
    expect(h.conn.requests.at(-1)?.msgId).toBe(MsgID.GET_USER_GROUPS_REQ);
  });

  it('ignores a malformed 2124 body without touching groups', async () => {
    const h = makeHarness();
    await login(h);
    const before = h.conn.requests.length;
    h.conn.emit(MsgID.GROUP_MEMBER_ALIAS_UPDATED_NOTIFY, new Uint8Array([0xff, 0x00, 0x01]));
    expect(h.conn.requests.length).toBe(before);
  });
});

describe('ChatApi group alias snapshot', () => {
  it('seeds aliases from the roster and notifies subscribers', async () => {
    const h = makeHarness();
    await login(h);
    const bumps: number[] = [];
    h.api.subscribeGroupAlias(() => bumps.push(bumps.length));
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
        return {
          code: 0,
          members: [
            { userId: PEER, username: PEER, alias: '苍' },
            { userId: SELF, username: SELF },
          ],
        };
      }
      return { code: 0 };
    });
    expect(await h.api.loadGroupMembers('guild-1')).toHaveLength(2);
    expect(h.api.groupAliasOf('guild-1', PEER)).toBe('苍');
    expect(h.api.groupAliasOf('guild-1', SELF)).toBe('');
    expect(h.api.groupAliasOf('guild-2', PEER)).toBe('');
    expect(bumps).toEqual([0]);
  });

  it('applies 2124 notifies to the snapshot, clearing on empty alias', async () => {
    const h = makeHarness();
    await login(h);
    const bumps: number[] = [];
    h.api.subscribeGroupAlias(() => bumps.push(bumps.length));
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_GROUP_MEMBERS_REQ) {
        return { code: 0, members: [{ userId: PEER, username: PEER, alias: '苍' }] };
      }
      return { code: 0 };
    });
    await h.api.loadGroupMembers('guild-1');
    const emitAlias = (alias: string): void =>
      h.conn.emit(
        MsgID.GROUP_MEMBER_ALIAS_UPDATED_NOTIFY,
        GroupMemberAliasUpdatedNotify.encode(
          GroupMemberAliasUpdatedNotify.fromPartial({ groupId: 'guild-1', userId: PEER, alias }),
        ).finish(),
      );
    emitAlias('苍老师');
    expect(h.api.groupAliasOf('guild-1', PEER)).toBe('苍老师');
    emitAlias('');
    expect(h.api.groupAliasOf('guild-1', PEER)).toBe('');
    expect(bumps).toEqual([0, 1, 2]);
  });

  it('skips snapshot updates for groups without a loaded roster', async () => {
    const h = makeHarness();
    await login(h);
    const bumps: number[] = [];
    h.api.subscribeGroupAlias(() => bumps.push(bumps.length));
    h.conn.emit(
      MsgID.GROUP_MEMBER_ALIAS_UPDATED_NOTIFY,
      GroupMemberAliasUpdatedNotify.encode(
        GroupMemberAliasUpdatedNotify.fromPartial({ groupId: 'guild-1', userId: PEER, alias: '苍' }),
      ).finish(),
    );
    expect(h.api.groupAliasOf('guild-1', PEER)).toBe('');
    expect(bumps).toEqual([]);
  });
});

describe('ChatApi.setGroupMute', () => {
  it('short-circuits without login, echoing the request fields', async () => {
    const h = makeHarness();
    const resp = await h.api.setGroupMute('guild-1', PEER, 600);
    expect(resp).toMatchObject({
      code: ErrorCode.SESSION_EXPIRED,
      groupId: 'guild-1',
      userId: PEER,
      mutedUntilTs: 0,
    });
    expect(h.conn.requests).toHaveLength(0);
  });

  it('sends SET_GROUP_MUTE_REQ with durationSec and passes the response through', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.SET_GROUP_MUTE_REQ);
      expect(req).toMatchObject({ groupId: 'guild-1', targetUserId: PEER, durationSec: 600 });
      return { code: 0, groupId: 'guild-1', userId: PEER, mutedUntilTs: 12345 };
    });
    expect(await h.api.setGroupMute('guild-1', PEER, 600)).toMatchObject({ code: 0 });
  });
});

describe('ChatApi.reportMessage', () => {
  it('short-circuits an empty messageId without a network call', async () => {
    const h = makeHarness();
    const resp = await h.api.reportMessage('');
    expect(resp).toMatchObject({ code: ErrorCode.INVALID_PARAM, messageId: '', reportCount: 0 });
    expect(h.conn.requests).toHaveLength(0);
  });

  it('sends REPORT_MESSAGE_REQ with the reason and passes the count through', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (msgId, req) => {
      expect(msgId).toBe(MsgID.REPORT_MESSAGE_REQ);
      expect(req).toMatchObject({ messageId: 'm1', reason: 'spam' });
      return { code: 0, messageId: 'm1', reportCount: 2 };
    });
    const resp = await h.api.reportMessage('m1', 'spam');
    expect(resp).toMatchObject({ code: 0, messageId: 'm1', reportCount: 2 });
  });

  it('leaves reason empty by default', async () => {
    const h = makeHarness();
    await login(h);
    h.conn.setResponder(async (_msgId, req) => {
      expect((req as { reason?: string }).reason).toBe('');
      return { code: 0, messageId: 'm2', reportCount: 1 };
    });
    expect(await h.api.reportMessage('m2')).toMatchObject({ reportCount: 1 });
  });
});

describe('ChatApi group mute notify', () => {
  it('refreshes groups on a well-formed 2252 notify', async () => {
    const h = makeHarness();
    await login(h);
    const before = h.conn.requests.length;
    const notify = GroupMemberMutedNotify.encode(
      GroupMemberMutedNotify.fromPartial({
        groupId: 'guild-1',
        userId: PEER,
        mutedUntilTs: 12345,
        operatorId: SELF,
      }),
    ).finish();
    h.conn.emit(MsgID.GROUP_MEMBER_MUTED_NOTIFY, notify);
    await vi.waitFor(() => {
      expect(h.conn.requests.length).toBeGreaterThan(before);
    });
    expect(h.conn.requests.at(-1)?.msgId).toBe(MsgID.GET_USER_GROUPS_REQ);
  });

  it('ignores a malformed 2252 body without touching groups', async () => {
    const h = makeHarness();
    await login(h);
    const before = h.conn.requests.length;
    h.conn.emit(MsgID.GROUP_MEMBER_MUTED_NOTIFY, new Uint8Array([0xff, 0x00, 0x01]));
    expect(h.conn.requests.length).toBe(before);
  });
});
