import { describe, expect, it, vi } from 'vitest';
import { AchievementMetadata, ChannelType, ChatMessage, GuildRecruitMetadata, ItemMetadata, MsgType, PartyRecruitMetadata, Priority, SenderKind, TradeMetadata } from '@chirp/proto/chat';
import { MsgID } from '@chirp/proto/gateway';
import { ChatApi, channelRefOf, type ChannelRef, type SystemSurface } from './chat_api';
import { createStore } from '../state/store';
import { createConversationStore } from '../state/conversation_store';
import { createMessageStore } from '../state/message_store';
import { createTypingStore } from '../state/typing_store';
import { FakeChatConnection } from '../state/test_helpers';
import type { AuthState } from '../state/auth_store';

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
  messages: ReturnType<typeof createMessageStore>;
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
  return { conn, api, messages };
};

const login = async (h: Harness): Promise<void> => {
  h.conn.setResponder(async (msgId) => {
    if (msgId === MsgID.LOGIN_REQ) return { code: 0, userId: SELF };
    return { code: 0 };
  });
  await h.api.login(SELF);
};

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

describe('ChatApi.history surfaces', () => {
  it('back-fills priority/ttl/sender_kind on history messages', async () => {
    const h = makeHarness();
    await login(h);
    const channel = channelRefOf(privateChannel().key, SELF);
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_HISTORY_REQ) {
        return {
          code: 0,
          hasMore: false,
          messages: [
            ChatMessage.fromPartial({
              messageId: 'h1',
              senderId: PEER,
              channelType: ChannelType.PRIVATE,
              channelId: 'user_a|user_b',
              msgType: MsgType.TEXT,
              content: new TextEncoder().encode('历史公告'),
              timestamp: 60,
              priority: Priority.PRIORITY_URGENT,
              ttlSeconds: 5,
              senderKind: SenderKind.SENDER_SYSTEM,
            }),
          ],
        };
      }
      return { code: 0 };
    });
    await h.api.loadHistory(channel);
    const view = h.messages.get().byChannel[channel.key][0];
    expect(view).toMatchObject({
      priority: Priority.PRIORITY_URGENT,
      ttlSeconds: 5,
      senderKind: SenderKind.SENDER_SYSTEM,
    });
  });

  it('leaves the channel list empty when history comes back with no messages', async () => {
    const h = makeHarness();
    await login(h);
    const channel = channelRefOf(privateChannel().key, SELF);
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_HISTORY_REQ) return { code: 0, hasMore: false, messages: [] };
      return { code: 0 };
    });
    await expect(h.api.loadHistory(channel)).resolves.toBeUndefined();
    expect(h.messages.get().byChannel[channel.key]).toEqual([]);
  });
});

describe('ChatApi sender_kind passthrough', () => {
  it('leaves plain player messages at the proto zero values', async () => {
    const h = makeHarness();
    await login(h);
    const onMessage = vi.fn();
    h.api.onMessage(onMessage);
    h.conn.emit(MsgID.CHAT_MESSAGE_NOTIFY, chatBody({ messageId: 'm14' }));
    const view = h.messages.get().byChannel['p:user_a|user_b'][0];
    expect(view.senderKind).toBe(SenderKind.SENDER_USER);
    // proto3 zero value is PRIORITY_LOW, not PRIORITY_NORMAL.
    expect(view.priority).toBe(Priority.PRIORITY_LOW);
    expect(onMessage).toHaveBeenCalledTimes(1);
  });
});

describe('ChatApi item link cards (P0 ITEM_LINK)', () => {
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
            description: '周三晚开荒',
          }),
        ).finish(),
      }),
    );
    expect(h.messages.get().byChannel['p:user_a|user_b'][0]).toMatchObject({
      guildRecruit: {
        guildId: 'guild-3001',
        guildName: '烈焰军团',
        level: 7,
        memberCount: 48,
        maxMembers: 60,
        description: '周三晚开荒',
      },
    });

    h.conn.emit(
      MsgID.CHAT_MESSAGE_NOTIFY,
      chatBody({
        messageId: 'm2',
        msgType: MsgType.PARTY_RECRUIT,
        content: new TextEncoder().encode('缺 2 人'),
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
      partyRecruit: {
        partyId: 'party-4001',
        dungeonName: '熔岩之心',
        needed: 5,
        current: 3,
        leaderName: 'alice',
      },
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
          PartyRecruitMetadata.fromPartial({ dungeonName: '熔岩之心' }),
        ).finish(),
      }),
    );
    const second = h.messages.get().byChannel['p:user_a|user_b'][1];
    expect(second.partyRecruit).toBeUndefined();
    expect(second.content).toBe('hi');
  });
});
/** 临时频道（P1 群 TTL）：建群带 ttl、名册下发过期时刻、名册外群修剪。 */
describe('ChatApi temp channel ttl (P1 group TTL)', () => {
  const makeGroupHarness = () => {
    const conn = new FakeChatConnection();
    const auth = createStore<AuthState>({
      userId: SELF,
      kicked: false,
      deviceId: 'dev-1',
      loggedIn: true,
    });
    const conversations = createConversationStore();
    const messages = createMessageStore();
    const typing = createTypingStore();
    const api = new ChatApi({ conn, auth, conversations, messages, typing });
    return { conn, api, conversations };
  };

  it('createGroup forwards ttl seconds and the roster carries the expiry', async () => {
    const h = makeGroupHarness();
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.CREATE_GROUP_REQ) return { code: 0, groupId: 'guild-ttl' };
      if (msgId === MsgID.GET_USER_GROUPS_REQ) {
        return { code: 0, groups: [{ groupId: 'guild-ttl', groupName: 'Raid', expiresAt: 1792032098833 }] };
      }
      return { code: 0 };
    });
    expect(await h.api.createGroup('Raid', '', 3600)).toBe('guild-ttl');
    const create = h.conn.requests.find((r) => r.msgId === MsgID.CREATE_GROUP_REQ);
    expect(create?.req).toMatchObject({ ttlSeconds: 3600 });
    expect(
      h.conversations.get().conversations.find((c) => c.key === 'g:guild-ttl')?.expiresAt,
    ).toBe(1792032098833);
  });

  it('refreshGroups prunes group conversations missing from the roster', async () => {
    const h = makeGroupHarness();
    h.conn.setResponder(async (msgId) => {
      if (msgId === MsgID.GET_USER_GROUPS_REQ) return { code: 0, groups: [] };
      return { code: 0 };
    });
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
});
