import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/react';
import MessageBubble from './MessageBubble';
import type { ChatMessageView } from '../state/models';
import { zh } from '../i18n/zh';

const message = (over: Partial<ChatMessageView>): ChatMessageView => ({
  messageId: 'm1',
  senderId: 'user_b',
  channelKey: 'p:user_a|user_b',
  channelType: 0,
  channelId: 'user_a|user_b',
  content: 'hello',
  timestamp: Date.UTC(2026, 0, 1, 12, 30),
  pending: false,
  ...over,
});

describe('MessageBubble', () => {
  it('shows a failed status for own failed sends', () => {
    render(<MessageBubble message={message({ senderId: 'user_a', failed: true })} selfId="user_a" />);
    expect(screen.getByText(/发送失败/)).toBeTruthy();
  });

  it('shows the queued note for offline deliveries', () => {
    render(
      <MessageBubble message={message({ senderId: 'user_a', queuedOffline: true })} selfId="user_a" />,
    );
    expect(screen.getByText(/对方离线,已排队/)).toBeTruthy();
  });

  it('renders a deleted remote message as withdrawn', () => {
    render(<MessageBubble message={message({ deleted: true, content: '' })} selfId="user_a" />);
    expect(screen.getByText(zh.chat.deleted)).toBeTruthy();
    expect(screen.queryByText('hello')).toBeNull();
  });

  it('keeps pending own messages visually unconfirmed', () => {
    render(<MessageBubble message={message({ senderId: 'user_a', pending: true })} selfId="user_a" />);
    expect(screen.getByText(/发送中/)).toBeTruthy();
    expect(screen.getByText('hello')).toBeTruthy();
  });
});
