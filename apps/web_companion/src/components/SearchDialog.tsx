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
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { privateKey, type Conversation } from '../state/models';
import { upsertConversation } from '../state/conversation_store';
import { zh } from '../i18n/zh';

/** 消息搜索(SEARCH_MESSAGE_REQ/RESP 2248/2249):服务端 FTS 检索,关键词必填,
 *  分页游标(本页最后一条的 timestamp+message_id)由「加载更多」携带。私聊结果
 *  可直接跳转会话;群结果仅当本地已有该群会话时可跳;web 无会话面的频道
 *  (世界/系统等)只读展示。高亮不做——决策记录约定高亮由客户端做,首版从简。 */
export default function SearchDialog({
  open,
  onClose,
  onOpenChannel,
}: {
  open: boolean;
  onClose: () => void;
  onOpenChannel: (key: string) => void;
}) {
  const { api, auth, conversations } = useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const { conversations: local } = useStoreValue(conversations);
  const [keyword, setKeyword] = useState('');
  const [matches, setMatches] = useState<SearchMessageMatch[]>([]);
  const [hasMore, setHasMore] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [searched, setSearched] = useState(false);

  const canSearch = keyword.trim() !== '';

  const runSearch = async (page: boolean): Promise<void> => {
    if (!api || !canSearch) return;
    const last = matches[matches.length - 1];
    const resp = await api.searchMessages(keyword.trim(), page && last
      ? { beforeTimestamp: last.timestamp, beforeMessageId: last.messageId }
      : {});
    if (resp.code !== ErrorCode.OK) {
      setError(
        resp.code === ErrorCode.SERVER_UNAVAILABLE ? zh.search.unavailable : zh.search.failed,
      );
      setHasMore(false);
      return;
    }
    setError(null);
    setSearched(true);
    setMatches(page ? [...matches, ...resp.matches] : resp.matches);
    setHasMore(resp.hasMore);
  };

  /** 私聊命中 → 本地补会话并跳转;群命中 → 本地有会话才跳;其余不跳。 */
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
      return zh.search.privateChannel(peer);
    }
    if (match.channelType === ChannelType.GUILD) return zh.search.groupChannel(match.channelId);
    return zh.search.otherChannel(match.channelId);
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="sm" fullWidth>
      <DialogTitle>{zh.search.title}</DialogTitle>
      <DialogContent>
        <Box sx={{ display: 'flex', gap: 1, alignItems: 'center' }}>
          <TextField
            autoFocus
            fullWidth
            size="small"
            margin="dense"
            label={zh.search.placeholder}
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
            {zh.search.action}
          </Button>
        </Box>
        {error !== null && (
          <Typography variant="caption" color="error" data-testid="search-error">
            {error}
          </Typography>
        )}
        {!searched && error === null && (
          <Typography variant="body2" color="text.secondary" data-testid="search-prompt">
            {zh.search.prompt}
          </Typography>
        )}
        {searched && matches.length === 0 && error === null && (
          <Typography variant="body2" color="text.secondary" data-testid="search-empty">
            {zh.search.empty}
          </Typography>
        )}
        {matches.length > 0 && (
          <List dense data-testid="search-results">
            {matches.map((match) => (
              <MatchRow
                key={match.messageId}
                match={match}
                label={channelLabel(match)}
                openable={
                  match.channelType === ChannelType.PRIVATE ||
                  local.some((c: Conversation) => c.channelId === match.channelId)
                }
                onOpen={openMatch}
              />
            ))}
          </List>
        )}
      </DialogContent>
      <DialogActions>
        {hasMore && (
          <Button onClick={() => void runSearch(true)} data-testid="search-more">
            {zh.search.more}
          </Button>
        )}
        <Button onClick={onClose}>{zh.chat.cancel}</Button>
      </DialogActions>
    </Dialog>
  );
}

/** 私聊命中可点开;群命中本地有会话才可点开;其余频道只读。 */
function MatchRow({
  match,
  label,
  openable,
  onOpen,
}: {
  match: SearchMessageMatch;
  label: string;
  openable: boolean;
  onOpen: (match: SearchMessageMatch) => void;
}) {
  const row = (
    <ListItemText
      primary={match.content}
      secondary={`${label} · ${match.senderId} · ${new Date(match.timestamp).toLocaleString()}`}
    />
  );
  if (!openable) {
    return (
      <ListItemButton disabled data-testid={`search-match-${match.messageId}`}>
        {row}
      </ListItemButton>
    );
  }
  return (
    <ListItemButton onClick={() => onOpen(match)} data-testid={`search-match-${match.messageId}`}>
      {row}
    </ListItemButton>
  );
}
