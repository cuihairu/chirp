import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { SenderKind } from '@chirp/proto/chat';
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

  it('shows the mute hint for own MUTED rejections', () => {
    render(
      <MessageBubble
        message={message({ senderId: 'user_a', failed: true, failureReason: 'muted' })}
        selfId="user_a"
        onToggleReaction={noop}
      />,
    );
    expect(screen.getByText(/禁言中,消息未发送/)).toBeTruthy();
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

  it('renders the coordinate card and copies the coordinates on click', () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.assign(navigator, { clipboard: { writeText } });
    render(
      <MessageBubble
        message={message({
          content: '矿点',
          coord: { mapId: 'field-7', x: 120, y: 88, z: 3, label: 'BOSS 刷新点' },
        })}
        selfId="user_a"
        onToggleReaction={noop}
      />,
    );
    const card = screen.getByTestId('coord-m1');
    expect(card.textContent).toContain('BOSS 刷新点');
    expect(card.textContent).toContain('field-7');
    expect(card.textContent).toContain('120, 88, 3');
    fireEvent.click(card);
    expect(writeText).toHaveBeenCalledWith('120, 88, 3');
  });

  it('omits the zero height from the coordinate card text', () => {
    render(
      <MessageBubble
        message={message({ coord: { mapId: 'map-1', x: 4, y: 9, z: 0, label: '' } })}
        selfId="user_a"
        onToggleReaction={noop}
      />,
    );
    const card = screen.getByTestId('coord-m1');
    expect(card.textContent).toContain('4, 9');
    expect(card.textContent).not.toContain('4, 9, 0');
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

  it('renders the quoted parent summary when resolved', () => {
    render(
      <MessageBubble
        message={message({ replyToMessageId: 'm0' })}
        selfId="user_a"
        quoted={{ senderName: 'user_b', content: 'earlier line' }}
        onToggleReaction={noop}
      />,
    );
    expect(screen.getByTestId('quoted-m1').textContent).toContain(zh.chat.reply);
    expect(screen.getByTestId('quoted-m1').textContent).toContain('user_b');
    expect(screen.getByTestId('quoted-m1').textContent).toContain('earlier line');
  });

  it('shows the unavailable placeholder when the quoted parent is missing', () => {
    render(
      <MessageBubble
        message={message({ replyToMessageId: 'm-gone' })}
        selfId="user_a"
        quoted={null}
        onToggleReaction={noop}
      />,
    );
    expect(screen.getByTestId('quoted-m1').textContent).toContain(zh.chat.quotedUnavailable);
  });

  it('renders no quote block without a reply id', () => {
    render(<MessageBubble message={message({})} selfId="user_a" onToggleReaction={noop} />);
    expect(screen.queryByTestId('quoted-m1')).toBeNull();
  });

  it('fires the reply action with the message', () => {
    const onReply = vi.fn();
    render(<MessageBubble message={message({})} selfId="user_a" onToggleReaction={noop} onReply={onReply} />);
    fireEvent.click(screen.getByTestId('reply-m1'));
    expect(onReply).toHaveBeenCalledWith(expect.objectContaining({ messageId: 'm1' }));
  });

  it('hides the reply action on deleted messages', () => {
    const onReply = vi.fn();
    render(
      <MessageBubble
        message={message({ deleted: true, content: '' })}
        selfId="user_a"
        onToggleReaction={noop}
        onReply={onReply}
      />,
    );
    expect(screen.queryByTestId('reply-m1')).toBeNull();
  });

  it('fires the report action on a peer message', () => {
    const onReport = vi.fn();
    render(<MessageBubble message={message({})} selfId="user_a" onToggleReaction={noop} onReport={onReport} />);
    fireEvent.click(screen.getByTestId('report-m1'));
    expect(onReport).toHaveBeenCalledWith(expect.objectContaining({ messageId: 'm1' }));
  });

  it('never offers the report action on own messages', () => {
    render(
      <MessageBubble
        message={message({ senderId: 'user_a' })}
        selfId="user_a"
        onToggleReaction={noop}
        onReport={noop}
      />,
    );
    expect(screen.queryByTestId('report-m1')).toBeNull();
  });

  it('hides the report action on deleted messages', () => {
    const onReport = vi.fn();
    render(
      <MessageBubble
        message={message({ deleted: true, content: '' })}
        selfId="user_a"
        onToggleReaction={noop}
        onReport={onReport}
      />,
    );
    expect(screen.queryByTestId('report-m1')).toBeNull();
  });

  it('renders no report action without a handler', () => {
    render(<MessageBubble message={message({})} selfId="user_a" onToggleReaction={noop} />);
    expect(screen.queryByTestId('report-m1')).toBeNull();
  });

  it('tags peer bubbles with the sender_kind face (P0)', () => {
    const cases: Array<[SenderKind, string]> = [
      [SenderKind.SENDER_USER, 'user'],
      [SenderKind.SENDER_SYSTEM, 'system'],
      [SenderKind.SENDER_NPC, 'npc'],
      [SenderKind.SENDER_SERVICE, 'service'],
    ];
    for (const [kind, tag] of cases) {
      const { container, unmount } = render(
        <MessageBubble message={message({ senderKind: kind })} selfId="user_a" onToggleReaction={noop} />,
      );
      expect(container.querySelector(`[data-sender-kind="${tag}"]`)).toBeTruthy();
      unmount();
    }
  });

  it('keeps own bubbles on the primary face even with a system sender_kind', () => {
    const { container } = render(
      <MessageBubble
        message={message({ senderId: 'user_a', senderKind: SenderKind.SENDER_SYSTEM })}
        selfId="user_a"
        onToggleReaction={noop}
      />,
    );
    expect(container.querySelector('[data-sender-kind="mine"]')).toBeTruthy();
  });

  it('falls back to the player face for an unknown sender_kind on the wire', () => {
    // A value the generated enum does not know (forward-compat gap) must not
    // render an undefined bubble color.
    const { container } = render(
      <MessageBubble
        message={message({ senderKind: 99 as SenderKind })}
        selfId="user_a"
        onToggleReaction={noop}
      />,
    );
    expect(container.querySelector('[data-sender-kind="user"]')).toBeTruthy();
  });
});
