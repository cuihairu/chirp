import { useState } from 'react';
import {
  Box,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  List,
  ListItemButton,
  ListItemText,
  TextField,
  Typography,
} from '@mui/material';
import { ChannelType, type SearchMessageMatch } from '@chirp/proto/chat';
import { ErrorCode } from '@chirp/proto/common';
import type { ChatApi } from '../api/chat_api';
import { privateKey, type Conversation } from '../state/models';
import { upsertConversation, type ConversationState } from '../state/conversation_store';
import { useStoreValue, type Store } from '../state/store';

/** 消息搜索(SEARCH_MESSAGE_REQ/RESP 2248/2249)：服务端 FTS 检索，关键词必填，
 *  分页游标（本页最后一条的 timestamp+message_id）由「加载更多」携带。私聊
 *  结果直接补会话并跳转；群结果仅当本地已有该群会话时可跳；桌面无会话面的
 *  频道（世界/系统等）只读展示。与 web_companion SearchDialog 同一交互契约
 *  （高亮客户端做，首版从简）。 */
export default function SearchDialog({
  api,
  conversations,
  selfId,
  open,
  onClose,
  onOpenChannel,
}: {
  api: ChatApi;
  conversations: Store<ConversationState>;
  selfId: string;
  open: boolean;
  onClose: () => void;
  onOpenChannel: (key: string) => void;
}) {
  const local = useStoreValue(conversations);
  const [keyword, setKeyword] = useState('');
  const [matches, setMatches] = useState<SearchMessageMatch[]>([]);
  const [hasMore, setHasMore] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [searched, setSearched] = useState(false);

  const canSearch = keyword.trim() !== '';

  const runSearch = async (page: boolean): Promise<void> => {
    if (!canSearch) return;
    const last = matches[matches.length - 1];
    const resp = await api.searchMessages(keyword.trim(), page && last
      ? { beforeTimestamp: last.timestamp, beforeMessageId: last.messageId }
      : {});
    if (resp.code !== ErrorCode.OK) {
      setError(resp.code === ErrorCode.SERVER_UNAVAILABLE ? '搜索服务未连接' : '搜索失败');
      setHasMore(false);
      return;
    }
    setError(null);
    setSearched(true);
    setMatches(page ? [...matches, ...resp.matches] : resp.matches);
    setHasMore(resp.hasMore);
  };

  /** 私聊命中 → 本地补会话并跳转；群命中 → 本地有会话才跳；其余不跳。 */
  const openMatch = (match: SearchMessageMatch): void => {
    if (match.channelType === ChannelType.PRIVATE && selfId !== '') {
      const peer = match.channelId.split('|').find((id) => id !== selfId);
      if (!peer) return;
      upsertConversation(conversations, {
        kind: 'private',
        key: privateKey(selfId, peer),
        channelId: match.channelId,
        peerId: peer,
        title: peer,
        unreadLocal: 0,
      });
      onOpenChannel(privateKey(selfId, peer));
      onClose();
    }
  };

  const channelLabel = (match: SearchMessageMatch): string => {
    if (match.channelType === ChannelType.PRIVATE) {
      const peer = match.channelId.split('|').find((id) => id !== selfId) ?? match.channelId;
      return `私聊 · ${peer}`;
    }
    if (match.channelType === ChannelType.GUILD) return `群聊 · ${match.channelId}`;
    return `频道 · ${match.channelId}`;
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="sm" fullWidth>
      <DialogTitle>消息搜索</DialogTitle>
      <DialogContent>
        <Box sx={{ display: 'flex', gap: 1, alignItems: 'center' }}>
          <TextField
            autoFocus
            fullWidth
            size="small"
            margin="dense"
            label="关键词"
            value={keyword}
            onChange={(e) => setKeyword(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && void runSearch(false)}
            data-testid="search-input"
          />
          <Button
            variant="contained"
            onClick={() => void runSearch(false)}
            disabled={!canSearch}
            data-testid="search-run"
          >
            搜索
          </Button>
        </Box>
        {error !== null && (
          <Typography variant="caption" color="error" data-testid="search-error">
            {error}
          </Typography>
        )}
        {!searched && error === null && (
          <Typography variant="body2" color="text.secondary" data-testid="search-prompt">
            输入关键词检索历史消息。
          </Typography>
        )}
        {searched && matches.length === 0 && error === null && (
          <Typography variant="body2" color="text.secondary" data-testid="search-empty">
            没有匹配的消息。
          </Typography>
        )}
        {matches.length > 0 && (
          <List dense data-testid="search-results">
            {matches.map((match) => {
              const openable =
                match.channelType === ChannelType.PRIVATE ||
                local.conversations.some((c: Conversation) => c.channelId === match.channelId);
              return (
                <ListItemButton
                  key={match.messageId}
                  disabled={!openable}
                  onClick={() => openMatch(match)}
                  data-testid={`search-match-${match.messageId}`}
                >
                  <ListItemText
                    primary={match.content}
                    secondary={`${channelLabel(match)} · ${match.senderId} · ${new Date(match.timestamp).toLocaleString()}`}
                  />
                </ListItemButton>
              );
            })}
          </List>
        )}
      </DialogContent>
      <DialogActions>
        {hasMore && (
          <Button onClick={() => void runSearch(true)} data-testid="search-more">
            加载更多
          </Button>
        )}
        <Button onClick={onClose}>取消</Button>
      </DialogActions>
    </Dialog>
  );
}
