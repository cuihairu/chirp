import ArrowUpwardIcon from '@mui/icons-material/ArrowUpward';
import SendIcon from '@mui/icons-material/Send';
import {
  Box,
  Button,
  Chip,
  CircularProgress,
  Divider,
  IconButton,
  InputBase,
  Paper,
  Stack,
  Tooltip,
  Typography,
} from '@mui/material';
import { useEffect, useRef, useState } from 'react';
import type { KeyboardEvent as ReactKeyboardEvent } from 'react';
import type { ChatApi, ChannelRef } from '../api/chat_api';
import type { Conversation } from '../state/models';
import type { MessageState } from '../state/message_store';
import { useStoreValue, type Store } from '../state/store';
import { TYPING_TTL_MS, type TypingState } from '../state/typing_store';

const fmtTime = (ts: number): string => {
  const d = new Date(ts);
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  return `${hh}:${mm}`;
};

/** 单条消息行：发送者 + 时间 + 正文 + 本地状态标记。 */
function MessageRow(props: {
  mine: boolean;
  senderId: string;
  content: string;
  timestamp: number;
  pending: boolean;
  failed?: boolean;
  queuedOffline?: boolean;
  edited?: boolean;
  readByPeer: boolean;
}) {
  return (
    <Box sx={{ display: 'flex', justifyContent: props.mine ? 'flex-end' : 'flex-start', mb: 1 }}>
      <Box sx={{ maxWidth: '72%' }}>
        {!props.mine ? (
          <Typography variant="caption" color="text.secondary">
            {props.senderId}
          </Typography>
        ) : null}
        <Paper
          elevation={0}
          sx={{
            px: 1.5,
            py: 1,
            bgcolor: props.mine ? 'primary.main' : 'background.paper',
            color: props.mine ? '#08130f' : 'text.primary',
            opacity: props.pending ? 0.6 : 1,
            border: props.failed ? '1px solid #e57373' : 'none',
            mt: props.mine ? 0 : 0.25,
          }}
        >
          <Typography variant="body2" sx={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>
            {props.content}
          </Typography>
        </Paper>
        <Stack
          direction="row"
          spacing={0.5}
          sx={{ justifyContent: props.mine ? 'flex-end' : 'flex-start', mt: 0.25 }}
        >
          <Typography variant="caption" color="text.disabled">
            {fmtTime(props.timestamp)}
            {props.edited ? ' · 已编辑' : ''}
          </Typography>
          {props.pending ? (
            <Typography variant="caption" color="text.disabled">
              发送中
            </Typography>
          ) : null}
          {props.failed ? (
            <Typography variant="caption" color="error">
              发送失败
            </Typography>
          ) : null}
          {props.queuedOffline ? (
            <Typography variant="caption" color="text.secondary">
              对方离线，已入离线队列
            </Typography>
          ) : null}
          {props.mine && props.readByPeer && !props.pending && !props.failed ? (
            <Typography variant="caption" color="text.disabled">
              已读
            </Typography>
          ) : null}
        </Stack>
      </Box>
    </Box>
  );
}

/**
 * 聊天主面板：历史（分页加载）+ 实时收发 + 已读回执 + 正在输入 +
 * 离线队列提示。stores 经 services 注入（与 web_companion 同构）。
 */
export default function ChatPanel(props: {
  api: ChatApi;
  messages: Store<MessageState>;
  typing: Store<TypingState>;
  conversation: Conversation;
  selfId: string;
  channel: ChannelRef;
  membersHint?: string[];
}) {
  const { api, conversation, selfId, channel } = props;
  const messages = useStoreValue(props.messages);
  const typing = useStoreValue(props.typing);
  const list = messages.byChannel[conversation.key] ?? [];
  const hasMore = messages.hasMore[conversation.key] ?? false;
  const loading = messages.loadingHistory[conversation.key] ?? false;
  const [draft, setDraft] = useState('');
  const [error, setError] = useState<string | null>(null);
  const bottomRef = useRef<HTMLDivElement | null>(null);
  const cursors = messages.readCursors[conversation.key] ?? {};

  // 打开会话即拉历史并标读（要带最新消息 id，对端的「已读」才能对上号）；
  // 离开会话时清 activeChannel（免得本会话外的消息再涨本地未读红点）。
  useEffect(() => {
    setError(null);
    void api.loadHistory(channel).then(() => {
      const fresh = props.messages.get().byChannel[conversation.key] ?? [];
      const latest = fresh[fresh.length - 1];
      if (latest) void api.markRead(channel, latest.messageId);
    });
    api.setActiveChannel(conversation.key);
    return () => api.setActiveChannel(null);
  }, [api, channel, conversation.key]);

  // 会话保持打开期间，每条新到的消息都即时标读。
  useEffect(() => {
    const newest = list[list.length - 1];
    if (!newest || newest.senderId === selfId) return;
    void api.markRead(channel, newest.messageId);
  }, [api, channel, selfId, list]);

  // 新消息自动滚底（v1 从简：总是滚）。
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ block: 'end' });
  }, [list.length]);

  const send = async (): Promise<void> => {
    const text = draft.trim();
    if (!text) return;
    setDraft('');
    setError(null);
    try {
      await api.sendMessage(channel, text);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const onDraftKeyDown = (event: ReactKeyboardEvent<HTMLInputElement | HTMLTextAreaElement>): void => {
    if (event.key === 'Enter' && !event.shiftKey && !event.nativeEvent.isComposing) {
      event.preventDefault();
      void send();
    }
  };

  const lastMessage = list[list.length - 1];

  // 已读回执：对方已读游标追平最新一条时，我发的行标「已读」（与
  // web_companion 同一语义；群聊暂无成员游标，不显示）。
  const peerRead =
    conversation.kind === 'private' && list.length > 0
      ? cursors[conversation.peerId] === lastMessage?.messageId
      : false;

  // 正在输入：TTL 内的用户，排除自己。
  const now = Date.now();
  const typingUsers = Object.entries(typing.byChannel[conversation.key] ?? {})
    .filter(([uid, at]) => uid !== selfId && now - at < TYPING_TTL_MS)
    .map(([uid]) => uid);

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', height: '100%', minWidth: 0 }}>
      <Box
        sx={{
          px: 2,
          py: 1.5,
          bgcolor: 'background.paper',
          borderBottom: '1px solid',
          borderColor: 'divider',
        }}
      >
        <Stack direction="row" alignItems="center" spacing={1}>
          <Typography variant="subtitle1" fontWeight={600} noWrap>
            {conversation.title}
          </Typography>
          <Chip
            size="small"
            label={conversation.kind === 'private' ? '私聊' : '群聊'}
            variant="outlined"
          />
          {props.membersHint ? (
            <Typography variant="caption" color="text.secondary" noWrap>
              {props.membersHint.length} 名成员
            </Typography>
          ) : null}
        </Stack>
      </Box>

      <Box sx={{ flex: 1, overflowY: 'auto', px: 2, py: 1.5 }}>
        {hasMore ? (
          <Box sx={{ textAlign: 'center', mb: 1 }}>
            <Button
              size="small"
              startIcon={
                loading ? <CircularProgress size={12} /> : <ArrowUpwardIcon fontSize="small" />
              }
              onClick={() => void api.loadHistory(channel, list[0]?.timestamp)}
              disabled={loading}
            >
              加载更早消息
            </Button>
          </Box>
        ) : null}
        {list.map((m) => (
          <MessageRow
            key={m.messageId}
            mine={m.senderId === selfId}
            senderId={m.senderId}
            content={m.content}
            timestamp={m.timestamp}
            pending={m.pending}
            failed={m.failed}
            queuedOffline={m.queuedOffline}
            edited={m.edited}
            readByPeer={peerRead}
          />
        ))}
        <div ref={bottomRef} />
      </Box>

      {typingUsers.length > 0 ? (
        <Typography variant="caption" color="text.secondary" sx={{ px: 2, pb: 0.5 }}>
          {typingUsers.join('、')} 正在输入…
        </Typography>
      ) : null}

      {error ? (
        <Typography variant="caption" color="error" sx={{ px: 2, pb: 0.5 }}>
          {error}
        </Typography>
      ) : null}

      <Divider />
      <Box sx={{ p: 1.5, bgcolor: 'background.paper' }}>
        <Stack direction="row" spacing={1} alignItems="flex-end">
          <InputBase
            fullWidth
            multiline
            maxRows={5}
            placeholder={`发消息给 ${conversation.title}（Enter 发送，Shift+Enter 换行）`}
            value={draft}
            onChange={(e) => {
              setDraft(e.target.value);
              api.sendTyping(channel, e.target.value.length > 0);
            }}
            onKeyDown={onDraftKeyDown}
            sx={{ bgcolor: 'background.default', borderRadius: 2, px: 1.5, py: 0.75 }}
          />
          <Tooltip title="发送">
            <span>
              <IconButton color="primary" onClick={() => void send()} disabled={!draft.trim()}>
                <SendIcon />
              </IconButton>
            </span>
          </Tooltip>
        </Stack>
      </Box>
    </Box>
  );
}
