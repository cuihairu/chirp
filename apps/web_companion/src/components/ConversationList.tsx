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
import { privateKey, type Conversation } from '../state/models';
import { upsertConversation } from '../state/conversation_store';
import { zh } from '../i18n/zh';

/** Left pane: conversations, newest first, plus the start-private-chat entry. */
export default function ConversationList({
  activeKey,
  onOpen,
}: {
  activeKey?: string;
  onOpen: (key: string) => void;
}) {
  const { auth, conversations } = useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const { conversations: list } = useStoreValue(conversations);
  const [dialogOpen, setDialogOpen] = useState(false);
  const [peerId, setPeerId] = useState('');
  const [error, setError] = useState<string | null>(null);

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
      <Box sx={{ p: 1.5 }}>
        <Button variant="contained" fullWidth onClick={() => setDialogOpen(true)}>
          {zh.chat.newChat}
        </Button>
      </Box>
      <List dense sx={{ overflowY: 'auto', flex: 1 }}>
        {list.map((conversation: Conversation) => (
          <ListItemButton
            key={conversation.key}
            selected={conversation.key === activeKey}
            onClick={() => onOpen(conversation.key)}
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
          </ListItemButton>
        ))}
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
    </Box>
  );
}
