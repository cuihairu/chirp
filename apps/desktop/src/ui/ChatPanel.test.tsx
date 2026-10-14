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