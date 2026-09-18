import { useEffect, useRef, useState } from 'react';
import { Box, Button, CircularProgress, Dialog, DialogActions, DialogContent, DialogTitle, TextField, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { channelRefOf } from '../api/chat_api';
import { useStoreValue } from '../state/store';
import { readCursorOf } from '../state/message_store';
import { clearUnread } from '../state/conversation_store';
import { typingUsersOf } from '../state/typing_store';
import type { ChatMessageView } from '../state/models';
import MessageBubble from './MessageBubble';
import MessageInput from './MessageInput';
import { zh } from '../i18n/zh';

const TYPING_START_INTERVAL_MS = 3000;
const TYPING_IDLE_MS = 5000;
const TYPING_TICK_MS = 1000;

/**
 * The right pane: one open channel. Owning the open-channel lifecycle here
 * (active key → unread suppression → receipts → typing) keeps the stores dumb.
 */
export default function ChatWindow({ channelKey }: { channelKey: string }) {
  const { api, auth, conversations, messages, typing } = useServices();
  const { userId } = useStoreValue(auth);
  const messageState = useStoreValue(messages);
  const conversationState = useStoreValue(conversations);
  const typingState = useStoreValue(typing);
  const bottomRef = useRef<HTMLDivElement | null>(null);
  const historyLoadedFor = useRef<string | null>(null);
  const typingRef = useRef({ lastStart: 0, stopTimer: 0 as ReturnType<typeof setTimeout> | 0 });

  // Re-render once a second so expired typing entries drop off.
  const [now, setNow] = useState(Date.now());
  const [editing, setEditing] = useState<ChatMessageView | null>(null);
  const [editText, setEditText] = useState('');

  const selfId = userId ?? '';
  const channel = channelRefOf(channelKey, selfId);
  const list = messageState.byChannel[channelKey] ?? [];
  const hasMore = messageState.hasMore[channelKey] ?? false;
  const loading = messageState.loadingHistory[channelKey] ?? false;
  const conversation = conversationState.conversations.find((c) => c.key === channelKey);
  const title = conversation?.title ?? channel.peerId;
  const newest = list[list.length - 1];
  const typists = selfId ? typingUsersOf(typingState, channelKey, now) : [];

  // Channel opened: suppress unread locally and page history in (once per
  // channel mount; paging older history goes through the button).
  useEffect(() => {
    if (!selfId) return;
    api.setActiveChannel(channelKey);
    clearUnread(conversations, channelKey);
    clearTypingTimer(typingRef);
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

  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now()), TYPING_TICK_MS);
    return () => {
      clearInterval(timer);
      clearTypingTimer(typingRef);
    };
  }, []);

  // Keep the newest message on screen.
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ block: 'end' });
  }, [list.length]);

  const loadEarlier = (): void => {
    const oldest = list[0];
    void api.loadHistory(channel, oldest ? oldest.timestamp : undefined);
  };

  const send = (text: string): void => {
    clearTypingTimer(typingRef);
    void api.sendTyping(channel, false);
    void api.sendMessage(channel, text);
  };

  // Throttled typing sender: at most one start per 3s (the server cools
  // duplicates down anyway), plus a stop 5s after the last keystroke.
  const onTyping = (): void => {
    const state = typingRef.current;
    const at = Date.now();
    if (at - state.lastStart >= TYPING_START_INTERVAL_MS) {
      state.lastStart = at;
      void api.sendTyping(channel, true);
    }
    if (state.stopTimer) clearTimeout(state.stopTimer);
    state.stopTimer = setTimeout(() => {
      state.stopTimer = 0;
      void api.sendTyping(channel, false);
    }, TYPING_IDLE_MS);
  };

  const toggleReaction = (message: ChatMessageView, emoji: string): void => {
    const mine = message.reactions?.[emoji]?.mine ?? false;
    if (mine) void api.removeReaction(message.messageId, emoji);
    else void api.addReaction(message.messageId, emoji);
  };

  const openEdit = (message: ChatMessageView): void => {
    setEditing(message);
    setEditText(message.content);
  };

  const saveEdit = (): void => {
    if (editing && editText.trim()) {
      void api.editMessage(editing.messageId, editText.trim());
    }
    setEditing(null);
  };

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', flex: 1, minWidth: 0, height: '100%' }}>
      <Box sx={{ px: 2, py: 1.5, borderBottom: 1, borderColor: 'divider' }}>
        <Typography variant="subtitle1" noWrap>
          {title}
        </Typography>
        {typists.length > 0 && (
          <Typography variant="caption" color="text.secondary" data-testid="typing-row">
            {zh.chat.typing(typists.join(', '))}
          </Typography>
        )}
      </Box>
      <Box sx={{ flex: 1, overflowY: 'auto', px: 2, py: 1 }} data-testid="message-list">
        {hasMore && (
          <Button size="small" onClick={loadEarlier} disabled={loading} sx={{ mb: 1 }}>
            {loading ? zh.chat.loading : zh.chat.loadEarlier}
          </Button>
        )}
        {list.map((m) => (
          <MessageBubble
            key={m.messageId}
            message={m}
            selfId={selfId}
            peerReadMessageId={
              channel.kind === 'private'
                ? readCursorOf(messageState, channelKey, channel.peerId)
                : undefined
            }
            onToggleReaction={toggleReaction}
            onEdit={openEdit}
            onDelete={(message) => {
              if (window.confirm(zh.chat.deleteConfirm)) void api.deleteMessage(message.messageId);
            }}
          />
        ))}
        <div ref={bottomRef} />
      </Box>
      <Box sx={{ p: 1.5, borderTop: 1, borderColor: 'divider' }}>
        {loading && !list.length ? (
          <CircularProgress size={20} />
        ) : (
          <MessageInput onSend={send} onTyping={onTyping} disabled={selfId === ''} />
        )}
      </Box>

      <Dialog open={editing !== null} onClose={() => setEditing(null)} maxWidth="xs" fullWidth>
        <DialogTitle>{zh.chat.editTitle}</DialogTitle>
        <DialogContent>
          <TextField
            autoFocus
            fullWidth
            multiline
            margin="dense"
            value={editText}
            onChange={(e) => setEditText(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && !e.shiftKey && saveEdit()}
          />
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setEditing(null)}>{zh.chat.cancel}</Button>
          <Button variant="contained" onClick={saveEdit} disabled={editText.trim() === ''}>
            {zh.chat.save}
          </Button>
        </DialogActions>
      </Dialog>
    </Box>
  );
}

function clearTypingTimer(ref: React.RefObject<{ lastStart: number; stopTimer: ReturnType<typeof setTimeout> | 0 }>): void {
  if (!ref.current) return;
  if (ref.current.stopTimer) {
    clearTimeout(ref.current.stopTimer);
    ref.current.stopTimer = 0;
  }
  ref.current.lastStart = 0;
}
