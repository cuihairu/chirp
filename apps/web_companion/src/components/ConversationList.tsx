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
  TextField,
  Typography,
} from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { privateKey, groupKey, type Conversation } from '../state/models';
import { upsertConversation } from '../state/conversation_store';
import { presenceFresh, presenceOf } from '../state/presence_store';
import { PresenceStatus } from '@chirp/proto/social';
import FriendsDialog from './FriendsDialog';
import PartyDialog, { PartyButton } from './PartyDialog';
import DevicesDialog, { DevicesButton } from './DevicesDialog';
import { zh } from '../i18n/zh';

/** Left pane: conversations, newest first, plus the start-private-chat entry. */
export default function ConversationList({
  activeKey,
  onOpen,
}: {
  activeKey?: string;
  onOpen: (key: string) => void;
}) {
  const { api, socialApi, partyApi, deviceApi, auth, conversations, presence } = useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const { conversations: list } = useStoreValue(conversations);
  const presenceState = useStoreValue(presence);
  const [dialogOpen, setDialogOpen] = useState(false);
  const [peerId, setPeerId] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [groupOpen, setGroupOpen] = useState(false);
  const [groupName, setGroupName] = useState('');
  const [friendsOpen, setFriendsOpen] = useState(false);
  const [partyOpen, setPartyOpen] = useState(false);
  const [devicesOpen, setDevicesOpen] = useState(false);

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
    const groupId = await api.createGroup(name);
    setGroupOpen(false);
    setGroupName('');
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
        {socialApi && (
          <Button variant="text" fullWidth onClick={() => setFriendsOpen(true)}>
            {zh.social.title}
          </Button>
        )}
        {partyApi && <PartyButton onClick={() => setPartyOpen(true)} />}
        {deviceApi && <DevicesButton onClick={() => setDevicesOpen(true)} />}
      </Box>
      <List dense sx={{ overflowY: 'auto', flex: 1 }}>
        {list.map((conversation: Conversation) => {
          const online =
            conversation.kind === 'private' &&
            presenceFresh(presenceState, conversation.peerId, Date.now()) &&
            presenceOf(presenceState, conversation.peerId)?.status === PresenceStatus.ONLINE;
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
                    <Typography variant="caption" noWrap sx={{ maxWidth: 160 }} display="block">
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
      {deviceApi && <DevicesDialog open={devicesOpen} onClose={() => setDevicesOpen(false)} />}
    </Box>
  );
}
