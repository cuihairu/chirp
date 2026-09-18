import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
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

const noop = () => undefined;

describe('MessageBubble', () => {
  it('shows a failed status for own failed sends', () => {
    render(<MessageBubble message={message({ senderId: 'user_a', failed: true })} selfId="user_a" onToggleReaction={noop} />);
    expect(screen.getByText(/发送失败/)).toBeTruthy();
  });

  it('shows the queued note for offline deliveries', () => {
    render(
      <MessageBubble message={message({ senderId: 'user_a', queuedOffline: true })} selfId="user_a" onToggleReaction={noop} />,
    );
    expect(screen.getByText(/对方离线,已排队/)).toBeTruthy();
  });

  it('renders a deleted remote message as withdrawn', () => {
    render(<MessageBubble message={message({ deleted: true, content: '' })} selfId="user_a" onToggleReaction={noop} />);
    expect(screen.getByText(zh.chat.deleted)).toBeTruthy();
    expect(screen.queryByText('hello')).toBeNull();
  });

  it('keeps pending own messages visually unconfirmed', () => {
    render(<MessageBubble message={message({ senderId: 'user_a', pending: true })} selfId="user_a" onToggleReaction={noop} />);
    expect(screen.getByText(/发送中/)).toBeTruthy();
    expect(screen.getByText('hello')).toBeTruthy();
  });

  it('renders reaction chips with counts and toggles on click', () => {
    const toggle = vi.fn();
    render(
      <MessageBubble
        message={message({ reactions: { '👍': { emoji: '👍', count: 2, mine: true } } })}
        selfId="user_a"
        onToggleReaction={toggle}
      />,
    );
    const chip = screen.getByTestId('reaction-m1-👍');
    expect(chip.textContent).toContain('×2');
    fireEvent.click(chip);
    expect(toggle).toHaveBeenCalledWith(expect.objectContaining({ messageId: 'm1' }), '👍');
  });

  it('offers quick reactions on every live message', () => {
    const toggle = vi.fn();
    render(<MessageBubble message={message({})} selfId="user_a" onToggleReaction={toggle} />);
    fireEvent.click(screen.getByTestId('react-m1-❤️'));
    expect(toggle).toHaveBeenCalledWith(expect.objectContaining({ messageId: 'm1' }), '❤️');
  });

  it('shows edit and delete only on own messages with handlers', () => {
    const handlers = { onToggleReaction: noop, onEdit: noop, onDelete: noop };
    const { rerender } = render(
      <MessageBubble message={message({ senderId: 'user_a' })} selfId="user_a" {...handlers} />,
    );
    expect(screen.getByTestId('edit-m1')).toBeTruthy();
    expect(screen.getByTestId('delete-m1')).toBeTruthy();

    rerender(<MessageBubble message={message({ senderId: 'user_b' })} selfId="user_a" {...handlers} />);
    expect(screen.queryByTestId('edit-m1')).toBeNull();
    expect(screen.queryByTestId('delete-m1')).toBeNull();
  });

  it('hides reactions and actions on deleted messages', () => {
    render(
      <MessageBubble
        message={message({ deleted: true, content: '', reactions: { '👍': { emoji: '👍', count: 1, mine: false } } })}
        selfId="user_a"
        onToggleReaction={noop}
        onEdit={noop}
        onDelete={noop}
      />,
    );
    expect(screen.queryByTestId('reaction-m1-👍')).toBeNull();
    expect(screen.queryByTestId('react-m1-❤️')).toBeNull();
    expect(screen.queryByTestId('edit-m1')).toBeNull();
  });

  it('marks own messages read once the peer cursor reaches them', () => {
    const { rerender } = render(
      <MessageBubble message={message({ senderId: 'user_a' })} selfId="user_a" peerReadMessageId="m2" onToggleReaction={noop} />,
    );
    expect(screen.getByText(new RegExp(zh.chat.read))).toBeTruthy();

    // Cursor still behind this message (string compare on msg_<ms>_<n> ids).
    rerender(
      <MessageBubble message={message({ senderId: 'user_a' })} selfId="user_a" peerReadMessageId="m0" onToggleReaction={noop} />,
    );
    expect(screen.queryByText(new RegExp(zh.chat.read))).toBeNull();
  });
});
