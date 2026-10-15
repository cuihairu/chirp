import { useState } from 'react';
import {
  Badge,
  Box,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  List,
  ListItemButton,
  ListItemText,
  MenuItem,
  TextField,
  Typography,
} from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { privateKey, groupKey, type Conversation } from '../state/models';
import { upsertConversation } from '../state/conversation_store';
import { presenceFresh, presenceOf } from '../state/presence_store';
import { useNow } from '../state/useNow';
import { PresenceStatus } from '@chirp/proto/social';
import FriendsDialog from './FriendsDialog';
import PartyDialog, { PartyButton } from './PartyDialog';
import VoiceDialog, { VoiceButton } from './VoiceDialog';
import DevicesDialog, { DevicesButton } from './DevicesDialog';
import OnlineDevicesDialog, { OnlineDevicesButton } from './OnlineDevicesDialog';
import SearchDialog from './SearchDialog';
import { zh } from '../i18n/zh';

/** 建群对话框的有效期档位：秒数 → 文案。0 = 永久群。 */
export const GROUP_TTL_OPTIONS: Array<[number, string]> = [
  [0, '永久'],
  [3600, '1 小时'],
  [86400, '1 天'],
  [604800, '7 天'],
];

/** 临时频道剩余时长（15s 时钟粒度：分钟/小时/天三档）。到点返回 null——
 *  名册刷新后该群从列表消失，不留"已过期"残影。 */
export const groupExpiryLabel = (expiresAt: number, now: number): string | null => {
  const minutes = Math.ceil((expiresAt - now) / 60_000);
  if (minutes <= 0) return null;
  if (minutes < 60) return `剩 ${minutes} 分`;
  const hours = Math.ceil(minutes / 60);
  if (hours < 48) return `剩 ${hours} 小时`;
  return `剩 ${Math.ceil(hours / 24)} 天`;
};

/** Left pane: conversations, newest first, plus the start-private-chat entry. */
export default function ConversationList({
  activeKey,
  onOpen,
}: {
  activeKey?: string;
  onOpen: (key: string) => void;
}) {
  const { api, socialApi, partyApi, voiceApi, deviceApi, auth, conversations, presence } =
    useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const { conversations: list } = useStoreValue(conversations);
  const presenceState = useStoreValue(presence);
  // 在线新鲜度按墙上时钟判 TTL 过期;时钟走状态(渲染期禁 Date.now()),
  // 15s 一跳,过期徽标下次跳变即灭。
  const now = useNow(15_000);
  const [dialogOpen, setDialogOpen] = useState(false);
  const [peerId, setPeerId] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [groupOpen, setGroupOpen] = useState(false);
  const [groupName, setGroupName] = useState('');
  const [groupTtl, setGroupTtl] = useState(0);
  const [friendsOpen, setFriendsOpen] = useState(false);
  const [partyOpen, setPartyOpen] = useState(false);
  const [voiceOpen, setVoiceOpen] = useState(false);
  const [devicesOpen, setDevicesOpen] = useState(false);
  const [onlineDevicesOpen, setOnlineDevicesOpen] = useState(false);
  const [searchOpen, setSearchOpen] = useState(false);

  const startChat = (): void => {
    const peer = peerId.trim();
    if (!peer || peer === selfId) {
      setError(zh.chat.startChatSelf);
      return;
    }
    // The server has no private-conversation listing; the local store is the
    // list. History still pages in from the server when the channel opens.
    upsertConversation(conversations, {
      kind: 'private',
      key: privateKey(selfId, peer),
      channelId: [selfId, peer].sort().join('|'),
      peerId: peer,
      title: peer,
      unreadLocal: 0,
    });
    setDialogOpen(false);
    setPeerId('');
    setError(null);
    onOpen(privateKey(selfId, peer));
  };

  const createGroup = async (): Promise<void> => {
    const name = groupName.trim();
    if (!name) return;
    const groupId = await api.createGroup(name, '', groupTtl);
    setGroupOpen(false);
    setGroupName('');
    setGroupTtl(0);
    if (groupId) onOpen(groupKey(groupId));
  };

  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
        borderRight: 1,
        borderColor: 'divider',
      }}
    >
      <Box sx={{ p: 1.5, display: 'flex', flexDirection: 'column', gap: 1 }}>
        <Button variant="contained" fullWidth onClick={() => setDialogOpen(true)}>
          {zh.chat.newChat}
        </Button>
        <Button variant="outlined" fullWidth onClick={() => setGroupOpen(true)}>
          {zh.chat.newGroup}
        </Button>
        <Button variant="text" fullWidth onClick={() => setSearchOpen(true)} data-testid="search-entry">
          {zh.search.title}
        </Button>
        {socialApi && (
          <Button variant="text" fullWidth onClick={() => setFriendsOpen(true)}>
            {zh.social.title}
          </Button>
        )}
        {partyApi && <PartyButton onClick={() => setPartyOpen(true)} />}
        {voiceApi && <VoiceButton onClick={() => setVoiceOpen(true)} />}
        {deviceApi && <DevicesButton onClick={() => setDevicesOpen(true)} />}
        <OnlineDevicesButton onClick={() => setOnlineDevicesOpen(true)} />
      </Box>
      <List dense sx={{ overflowY: 'auto', flex: 1 }}>
        {list.map((conversation: Conversation) => {
          const online =
            conversation.kind === 'private' &&
            presenceFresh(presenceState, conversation.peerId, now) &&
            presenceOf(presenceState, conversation.peerId)?.status === PresenceStatus.ONLINE;
          const expiry =
            conversation.kind === 'group' && conversation.expiresAt
              ? groupExpiryLabel(conversation.expiresAt, now)
              : null;
          return (
            <ListItemButton
              key={conversation.key}
              selected={conversation.key === activeKey}
              onClick={() => onOpen(conversation.key)}
            >
              <Badge
                variant="dot"
                color="success"
                sx={{ mr: 1 }}
                data-testid={
                  conversation.kind === 'private' ? `presence-${conversation.peerId}` : undefined
                }
                invisible={!online}
              >
                <ListItemText
                  primary={
                    <Badge badgeContent={conversation.unreadLocal} color="error">
                      <Typography variant="body2" noWrap sx={{ maxWidth: 160 }}>
                        {conversation.title}
                      </Typography>
                    </Badge>
                  }
                  secondary={
                    <Typography variant="caption" noWrap sx={{ maxWidth: 160, display: 'block' }}>
                      {expiry && (
                        <Box component="span" data-testid={`group-expiry-${conversation.channelId}`}>
                          {expiry} ·{' '}
                        </Box>
                      )}
                      {conversation.lastMessagePreview ?? ''}
                    </Typography>
                  }
                />
              </Badge>
            </ListItemButton>
          );
        })}
        {list.length === 0 && (
          <Typography variant="caption" sx={{ px: 2, py: 1 }} color="text.secondary">
            {zh.chat.emptyChannel}
          </Typography>
        )}
      </List>
      <Dialog open={dialogOpen} onClose={() => setDialogOpen(false)} maxWidth="xs" fullWidth>
        <DialogTitle>{zh.chat.newChatTitle}</DialogTitle>
        <DialogContent>
          <TextField
            autoFocus
            fullWidth
            margin="dense"
            label={zh.chat.newChatLabel}
            value={peerId}
            onChange={(e) => {
              setPeerId(e.target.value);
              setError(null);
            }}
            onKeyDown={(e) => e.key === 'Enter' && startChat()}
            error={error !== null}
            helperText={error ?? undefined}
          />
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setDialogOpen(false)}>{zh.chat.cancel}</Button>
          <Button variant="contained" onClick={startChat} disabled={peerId.trim() === ''}>
            {zh.chat.confirm}
          </Button>
        </DialogActions>
      </Dialog>
      <Dialog open={groupOpen} onClose={() => setGroupOpen(false)} maxWidth="xs" fullWidth>
        <DialogTitle>{zh.chat.newGroupTitle}</DialogTitle>
        <DialogContent>
          <TextField
            autoFocus
            fullWidth
            margin="dense"
            label={zh.chat.newGroupLabel}
            value={groupName}
            onChange={(e) => setGroupName(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && void createGroup()}
            data-testid="group-name-input"
          />
          <TextField
            select
            fullWidth
            margin="dense"
            label={zh.chat.groupTtlLabel}
            value={groupTtl}
            onChange={(e) => setGroupTtl(Number(e.target.value))}
            data-testid="group-ttl-select"
          >
            {GROUP_TTL_OPTIONS.map(([seconds, label]) => (
              <MenuItem key={seconds} value={seconds}>
                {label}
              </MenuItem>
            ))}
          </TextField>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setGroupOpen(false)}>{zh.chat.cancel}</Button>
          <Button
            variant="contained"
            onClick={() => void createGroup()}
            disabled={groupName.trim() === ''}
            data-testid="group-create-confirm"
          >
            {zh.chat.confirm}
          </Button>
        </DialogActions>
      </Dialog>
      {socialApi && (
        <FriendsDialog
          open={friendsOpen}
          onClose={() => setFriendsOpen(false)}
          onOpenChannel={onOpen}
        />
      )}
      {partyApi && <PartyDialog open={partyOpen} onClose={() => setPartyOpen(false)} />}
      {voiceApi && <VoiceDialog open={voiceOpen} onClose={() => setVoiceOpen(false)} />}
      {deviceApi && <DevicesDialog open={devicesOpen} onClose={() => setDevicesOpen(false)} />}
      <OnlineDevicesDialog open={onlineDevicesOpen} onClose={() => setOnlineDevicesOpen(false)} />
      {api && (
        <SearchDialog
          open={searchOpen}
          onClose={() => setSearchOpen(false)}
          onOpenChannel={onOpen}
        />
      )}
    </Box>
  );
}
