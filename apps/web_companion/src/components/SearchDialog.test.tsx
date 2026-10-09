import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { ChannelType } from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import { MsgID } from '@chirp/proto/gateway';
import { renderLoggedIn } from '../test-utils';
import { zh } from '../i18n/zh';
import SearchDialog from './SearchDialog';

const match = (
  over: Partial<{ id: string; channelType: number; channelId: string; content: string }>,
) => ({
  messageId: over.id ?? 'm1',
  channelId: over.channelId ?? 'user_a|user_b',
  channelType: over.channelType ?? ChannelType.PRIVATE,
  senderId: 'user_b',
  senderKind: 0,
  msgType: 0,
  timestamp: 1727400000000,
  content: over.content ?? 'boss is up',
});

describe('SearchDialog', () => {
  it('starts with the prompt copy and a disabled action', async () => {
    await renderLoggedIn(<SearchDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />);
    expect(screen.getByTestId('search-prompt')).toBeTruthy();
    const run = screen.getByTestId('search-run') as HTMLButtonElement;
    expect(run.disabled).toBe(true);
  });

  it('searches and renders matches; a private hit opens its channel', async () => {
    const onOpenChannel = vi.fn();
    const onClose = vi.fn();
    const requests: unknown[] = [];
    await renderLoggedIn(
      <SearchDialog open onClose={onClose} onOpenChannel={onOpenChannel} />,
      {
        responder: async (msgId, req) => {
          if (msgId === MsgID.SEARCH_MESSAGE_REQ) {
            requests.push(req);
            return {
              code: 0,
              matches: [
                match({}),
                match({ id: 'm2', channelType: ChannelType.GUILD, channelId: 'guild-1', content: 'guild boss' }),
              ],
              hasMore: false,
            };
          }
          return { code: 0 };
        },
      },
    );
    fireEvent.change(screen.getByLabelText(zh.search.placeholder), { target: { value: 'boss' } });
    fireEvent.click(screen.getByTestId('search-run'));
    await waitFor(() => screen.getByTestId('search-results'));
    expect(screen.getByText('boss is up')).toBeTruthy();
    expect(screen.getByText('guild boss')).toBeTruthy();
    expect(screen.getByTestId('search-results').textContent).toContain(
      zh.search.groupChannel('guild-1'),
    );

    // 群命中本地无会话 → 行存在但禁用;私聊命中可点,跳 p:user_a|user_b。
    const guildRow = screen.getByTestId('search-match-m2') as HTMLButtonElement;
    expect(guildRow.getAttribute('aria-disabled')).toBe('true');
    fireEvent.click(screen.getByTestId('search-match-m1'));
    expect(onOpenChannel).toHaveBeenCalledWith('p:user_a|user_b');
    expect(onClose).toHaveBeenCalled();
    expect(requests).toHaveLength(1);
  });

  it('loads more with the cursor of the last match', async () => {
    await renderLoggedIn(
      <SearchDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />,
      {
        responder: async (msgId, req) => {
          if (msgId !== MsgID.SEARCH_MESSAGE_REQ) return { code: 0 };
          const r = req as { beforeTimestamp?: number; beforeMessageId?: string };
          if (r.beforeMessageId === 'm1') {
            return { code: 0, matches: [match({ id: 'm2' })], hasMore: false };
          }
          return { code: 0, matches: [match({})], hasMore: true };
        },
      },
    );
    fireEvent.change(screen.getByLabelText(zh.search.placeholder), { target: { value: 'boss' } });
    fireEvent.click(screen.getByTestId('search-run'));
    await waitFor(() => screen.getByTestId('search-more'));
    fireEvent.click(screen.getByTestId('search-more'));
    await waitFor(() => screen.getByTestId('search-match-m2'));
    expect(screen.getByTestId('search-match-m1')).toBeTruthy();
  });

  it('shows the unavailable copy on SERVER_UNAVAILABLE', async () => {
    await renderLoggedIn(
      <SearchDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />,
      {
        responder: async (msgId) =>
          msgId === MsgID.SEARCH_MESSAGE_REQ
            ? { code: ErrorCode.SERVER_UNAVAILABLE, matches: [], hasMore: false }
            : { code: 0 },
      },
    );
    fireEvent.change(screen.getByLabelText(zh.search.placeholder), { target: { value: 'boss' } });
    fireEvent.click(screen.getByTestId('search-run'));
    await waitFor(() => screen.getByTestId('search-error'));
    expect(screen.getByTestId('search-error').textContent).toBe(zh.search.unavailable);
  });

  it('shows the empty copy when nothing matched', async () => {
    await renderLoggedIn(
      <SearchDialog open onClose={vi.fn()} onOpenChannel={vi.fn()} />,
      {
        responder: async (msgId) =>
          msgId === MsgID.SEARCH_MESSAGE_REQ
            ? { code: 0, matches: [], hasMore: false }
            : { code: 0 },
      },
    );
    fireEvent.change(screen.getByLabelText(zh.search.placeholder), { target: { value: 'boss' } });
    fireEvent.click(screen.getByTestId('search-run'));
    await waitFor(() => screen.getByTestId('search-empty'));
    expect(screen.getByTestId('search-empty').textContent).toBe(zh.search.empty);
  });
});
