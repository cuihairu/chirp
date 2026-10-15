import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { ChannelType, SenderKind } from '@chirp/proto/chat';
import type { ChatApi, ChannelRef } from '../api/chat_api';
import { appendMessage, createMessageStore } from '../state/message_store';
import { createTypingStore } from '../state/typing_store';
import type { ChatMessageView, Conversation } from '../state/models';
import ChatPanel from './ChatPanel';

const SELF = 'user_a';

const channel: ChannelRef = {
  key: 'p:user_a|user_b',
  kind: 'private',
  channelId: 'user_a|user_b',
  peerId: 'user_b',
};

const conversation: Conversation = {
  kind: 'private',
  key: channel.key,
  channelId: channel.channelId,
  peerId: 'user_b',
  title: 'user_b',
  unreadLocal: 0,
};

const message = (over: Partial<ChatMessageView>): ChatMessageView => ({
  messageId: 'm1',
  senderId: 'user_b',
  channelKey: channel.key,
  channelType: ChannelType.PRIVATE,
  channelId: channel.channelId,
  content: 'hi',
  timestamp: 1_000,
  pending: false,
  ...over,
});

/** Minimal api double: the panel only touches history/active-channel/alias here. */
const fakeApi = (): ChatApi =>
  ({
    loadHistory: vi.fn().mockResolvedValue(undefined),
    setActiveChannel: vi.fn(),
    markRead: vi.fn().mockResolvedValue(undefined),
    groupAliasOf: vi.fn().mockReturnValue(''),
    reportMessage: vi.fn().mockResolvedValue(undefined),
  }) as unknown as ChatApi;

const renderPanel = (list: ChatMessageView[]) => {
  const messages = createMessageStore();
  for (const m of list) appendMessage(messages, m);
  render(
    <ChatPanel
      api={fakeApi()}
      messages={messages}
      typing={createTypingStore()}
      conversation={conversation}
      selfId={SELF}
      channel={channel}
    />,
  );
};

describe('ChatPanel sender-kind bubble faces (P0 消息展示)', () => {
  it('paints system, npc and service senders with distinct faces', async () => {
    renderPanel([
      message({ messageId: 's1', senderKind: SenderKind.SENDER_SYSTEM, content: '系统消息' }),
      message({ messageId: 'n1', senderKind: SenderKind.SENDER_NPC, content: 'NPC 消息' }),
      message({ messageId: 'v1', senderKind: SenderKind.SENDER_SERVICE, content: '交易消息' }),
      message({ messageId: 'p1', senderKind: SenderKind.SENDER_USER, content: '玩家消息' }),
    ]);
    await waitFor(() => expect(screen.getByText('系统消息')).toBeTruthy());
    expect(screen.getByText('系统消息').closest('[data-sender-kind]')?.getAttribute('data-sender-kind')).toBe('system');
    expect(screen.getByText('NPC 消息').closest('[data-sender-kind]')?.getAttribute('data-sender-kind')).toBe('npc');
    expect(screen.getByText('交易消息').closest('[data-sender-kind]')?.getAttribute('data-sender-kind')).toBe('service');
    expect(screen.getByText('玩家消息').closest('[data-sender-kind]')?.getAttribute('data-sender-kind')).toBe('user');
  });

  it('falls back to the player face when sender_kind is absent', async () => {
    renderPanel([message({ messageId: 'p2', content: '旧消息' })]);
    await waitFor(() => expect(screen.getByText('旧消息')).toBeTruthy());
    expect(screen.getByText('旧消息').closest('[data-sender-kind]')?.getAttribute('data-sender-kind')).toBe('user');
  });

  it('falls back to the player face for an unknown sender_kind on the wire', async () => {
    // A value the generated enum does not know (forward-compat gap) must not
    // render an undefined bubble color.
    renderPanel([message({ messageId: 'x', senderKind: 99 as SenderKind, content: '未来消息' })]);
    await waitFor(() => expect(screen.getByText('未来消息')).toBeTruthy());
    expect(
      screen.getByText('未来消息').closest('[data-sender-kind]')?.getAttribute('data-sender-kind'),
    ).toBe('user');
  });

  it('keeps own messages on the primary face regardless of sender_kind', async () => {
    renderPanel([
      message({
        messageId: 'own',
        senderId: SELF,
        senderKind: SenderKind.SENDER_SERVICE,
        content: '我发的',
      }),
    ]);
    await waitFor(() => expect(screen.getByText('我发的')).toBeTruthy());
    expect(screen.getByText('我发的').closest('[data-sender-kind]')?.getAttribute('data-sender-kind')).toBe('mine');
  });
});

describe('ChatPanel item link cards (P0 ITEM_LINK)', () => {
  it('renders the item card with quality face and count, and opens the detail dialog', async () => {
    renderPanel([
      message({
        messageId: 'it1',
        content: '出了把橙武',
        item: {
          itemId: 'item-9001',
          itemName: '烈焰之刃',
          quality: 4,
          iconUrl: '',
          count: 2,
          attrs: { 攻击力: '120' },
        },
      }),
    ]);
    await waitFor(() => expect(screen.getByText('出了把橙武')).toBeTruthy());
    const card = screen.getByTestId('item-it1');
    expect(card.getAttribute('data-quality')).toBe('4');
    expect(card.textContent).toContain('烈焰之刃');
    expect(card.textContent).toContain('×2');
    expect(card.textContent).toContain('item-9001');

    fireEvent.click(card);
    const dialog = screen.getByRole('dialog');
    expect(dialog.textContent).toContain('烈焰之刃');
    expect(dialog.textContent).toContain('数量 ×2');
    expect(dialog.textContent).toContain('攻击力: 120');
  });

  it('renders no item card for plain text messages', async () => {
    renderPanel([message({ messageId: 't1', content: '纯文本' })]);
    await waitFor(() => expect(screen.getByText('纯文本')).toBeTruthy());
    expect(screen.queryByTestId('item-t1')).toBeNull();
  });
});

describe('ChatPanel achievement cards (P1 ACHIEVEMENT)', () => {
  it('renders the achievement card with the rarity face and description', async () => {
    renderPanel([
      message({
        messageId: 'ac1',
        content: '解锁了新成就',
        achievement: {
          achievementId: 'achievement-7001',
          achievementName: '全服首杀·烈焰巨龙',
          description: '首次击杀世界 BOSS 烈焰巨龙',
          iconUrl: '',
          rarity: 3,
        },
      }),
    ]);
    await waitFor(() => expect(screen.getByText('解锁了新成就')).toBeTruthy());
    const card = screen.getByTestId('achievement-ac1');
    expect(card.getAttribute('data-rarity')).toBe('3');
    expect(card.textContent).toContain('全服首杀·烈焰巨龙');
    expect(card.textContent).toContain('稀有度 史诗');
    expect(card.textContent).toContain('首次击杀世界 BOSS 烈焰巨龙');
    expect(card.textContent).toContain('achievement-7001');
  });

  it('renders no achievement card for plain text messages', async () => {
    renderPanel([message({ messageId: 't1', content: '纯文本' })]);
    await waitFor(() => expect(screen.getByText('纯文本')).toBeTruthy());
    expect(screen.queryByTestId('achievement-t1')).toBeNull();
  });
});

describe('ChatPanel trade cards (P1 TRADE_STATUS)', () => {
  it('renders the trade card with the status badge, amount and item summary', async () => {
    renderPanel([
      message({
        messageId: 'tr1',
        content: '交易已成交',
        trade: {
          tradeId: 'trade-9001',
          status: 'COMPLETED',
          amount: 1500,
          itemName: '烈焰剑',
          itemCount: 2,
        },
      }),
    ]);
    await waitFor(() => expect(screen.getByText('交易已成交')).toBeTruthy());
    const card = screen.getByTestId('trade-tr1');
    expect(card.getAttribute('data-trade-status')).toBe('COMPLETED');
    expect(card.textContent).toContain('交易状态 · COMPLETED');
    expect(card.textContent).toContain('烈焰剑 ×2');
    expect(card.textContent).toContain('金额 1500');
    expect(card.textContent).toContain('trade-9001');
  });

  it('renders no trade card for plain text messages', async () => {
    renderPanel([message({ messageId: 't2', content: '纯文本' })]);
    await waitFor(() => expect(screen.getByText('纯文本')).toBeTruthy());
    expect(screen.queryByTestId('trade-t2')).toBeNull();
  });
});

describe('ChatPanel recruit cards (P1 GUILD/PARTY_RECRUIT)', () => {
  it('renders the guild recruit card with level, headcount and blurb', async () => {
    renderPanel([
      message({
        messageId: 'gu1',
        content: '招募成员',
        guildRecruit: {
          guildId: 'guild-3001',
          guildName: '烈焰军团',
          level: 7,
          memberCount: 48,
          maxMembers: 60,
          description: '周三晚开荒',
        },
      }),
    ]);
    await waitFor(() => expect(screen.getByText('招募成员')).toBeTruthy());
    const card = screen.getByTestId('guild-gu1');
    expect(card.textContent).toContain('公会招募');
    expect(card.textContent).toContain('烈焰军团');
    expect(card.textContent).toContain('等级 7');
    expect(card.textContent).toContain('人数 48/60');
    expect(card.textContent).toContain('周三晚开荒');
    expect(card.textContent).toContain('guild-3001');
  });

  it('renders the party recruit card with headcount need and leader', async () => {
    renderPanel([
      message({
        messageId: 'pa1',
        content: '缺 2 人',
        partyRecruit: {
          partyId: 'party-4001',
          dungeonName: '熔岩之心',
          needed: 5,
          current: 3,
          leaderName: 'alice',
        },
      }),
    ]);
    await waitFor(() => expect(screen.getByText('缺 2 人')).toBeTruthy());
    const card = screen.getByTestId('party-pa1');
    expect(card.textContent).toContain('组队招募');
    expect(card.textContent).toContain('熔岩之心');
    expect(card.textContent).toContain('缺 2 人');
    expect(card.textContent).toContain('3/5');
    expect(card.textContent).toContain('队长 alice');
    expect(card.textContent).toContain('party-4001');
  });
});

describe('ChatPanel emoji codes (P1 表情/贴图)', () => {
  it('expands emoji codes in the message text and keeps unknown codes literal', async () => {
    renderPanel([message({ messageId: 'e1', content: 'hi [emoji:fire] [emoji:zzz]' })]);
    await waitFor(() => expect(screen.getByTestId('msg-text-e1')).toBeTruthy());
    expect(screen.getByTestId('emoji-e1-fire').textContent).toBe('🔥');
    expect(screen.getByTestId('msg-text-e1').textContent).toContain('[emoji:zzz]');
  });

  it('inserts the picked emoji code into the draft', async () => {
    renderPanel([message({ messageId: 'm1', content: 'hi' })]);
    await waitFor(() => expect(screen.getByText('hi')).toBeTruthy());
    fireEvent.click(screen.getByTestId('emoji-picker-toggle'));
    fireEvent.click(screen.getByTestId('emoji-pick-smile'));
    expect((screen.getByRole('textbox') as HTMLTextAreaElement).value).toContain('[emoji:smile]');
  });
});

describe('ChatPanel rich text (P1 富文本)', () => {
  it('renders bold and italic markers as strong/em and keeps the rest literal', async () => {
    renderPanel([message({ messageId: 'rt1', content: '上分**一波推**了*真的* 2 * 3' })]);
    await waitFor(() => expect(screen.getByTestId('msg-text-rt1')).toBeTruthy());
    const text = screen.getByTestId('msg-text-rt1');
    expect(text.querySelector('strong')?.textContent).toBe('一波推');
    expect(text.querySelector('em')?.textContent).toBe('真的');
    expect(text.textContent).toBe('上分一波推了真的 2 * 3');
  });

  it('carries the run style into an emoji inside the styled span', async () => {
    renderPanel([message({ messageId: 'rt2', content: '**火[emoji:fire]**' })]);
    await waitFor(() => expect(screen.getByTestId('msg-text-rt2')).toBeTruthy());
    const bold = screen.getByTestId('msg-text-rt2').querySelector('strong');
    expect(bold?.textContent).toBe('火🔥');
  });
});