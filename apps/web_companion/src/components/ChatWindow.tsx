import { useEffect, useRef } from 'react';
import { Box, Button, CircularProgress, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { channelRefOf } from '../api/chat_api';
import { useStoreValue } from '../state/store';
import { clearUnread } from '../state/conversation_store';
import MessageBubble from './MessageBubble';
import MessageInput from './MessageInput';
import { zh } from '../i18n/zh';

/**
 * The right pane: one open channel. Owning the open-channel lifecycle here
 * (active key → unread suppression → receipts) keeps the stores dumb.
 */
export default function ChatWindow({ channelKey }: { channelKey: string }) {
  const { api, auth, conversations, messages } = useServices();
  const { userId } = useStoreValue(auth);
  const messageState = useStoreValue(messages);
  const conversationState = useStoreValue(conversations);
  const bottomRef = useRef<HTMLDivElement | null>(null);
  const historyLoadedFor = useRef<string | null>(null);

  const selfId = userId ?? '';
  const channel = channelRefOf(channelKey, selfId);
  const list = messageState.byChannel[channelKey] ?? [];
  const hasMore = messageState.hasMore[channelKey] ?? false;
  const loading = messageState.loadingHistory[channelKey] ?? false;
  const conversation = conversationState.conversations.find((c) => c.key === channelKey);
  const title = conversation?.title ?? channel.peerId;
  const newest = list[list.length - 1];

  // Channel opened: suppress unread locally and page history in (once per
  // channel mount; paging older history goes through the button).
  useEffect(() => {
    if (!selfId) return;
    api.setActiveChannel(channelKey);
    clearUnread(conversations, channelKey);
    if (historyLoadedFor.current !== channelKey) {
      historyLoadedFor.current = channelKey;
      void api.loadHistory(channel).then(() => {
        const fresh = messages.get().byChannel[channelKey] ?? [];
        const latest = fresh[fresh.length - 1];
        if (latest) void api.markRead(channel, latest.messageId);
      });
    }
    return () => api.setActiveChannel(null);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [channelKey, selfId]);

  // While the channel stays open, every incoming message counts as read.
  useEffect(() => {
    if (!selfId || !newest || newest.senderId === selfId) return;
    void api.markRead(channel, newest.messageId);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selfId, newest?.messageId]);

  // Keep the newest message on screen.
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ block: 'end' });
  }, [list.length]);

  const loadEarlier = (): void => {
    const oldest = list[0];
    void api.loadHistory(channel, oldest ? oldest.timestamp : undefined);
  };

  const send = (text: string): void => {
    void api.sendMessage(channel, text);
  };

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', flex: 1, minWidth: 0, height: '100%' }}>
      <Box sx={{ px: 2, py: 1.5, borderBottom: 1, borderColor: 'divider' }}>
        <Typography variant="subtitle1" noWrap>
          {title}
        </Typography>
      </Box>
      <Box sx={{ flex: 1, overflowY: 'auto', px: 2, py: 1 }} data-testid="message-list">
        {hasMore && (
          <Button size="small" onClick={loadEarlier} disabled={loading} sx={{ mb: 1 }}>
            {loading ? zh.chat.loading : zh.chat.loadEarlier}
          </Button>
        )}
        {list.map((m) => (
          <MessageBubble key={m.messageId} message={m} selfId={selfId} />
        ))}
        <div ref={bottomRef} />
      </Box>
      <Box sx={{ p: 1.5, borderTop: 1, borderColor: 'divider' }}>
        {loading && !list.length ? (
          <CircularProgress size={20} />
        ) : (
          <MessageInput onSend={send} disabled={selfId === ''} />
        )}
      </Box>
    </Box>
  );
}
