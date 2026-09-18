import { useEffect, useState } from 'react';
import {
  Badge,
  Box,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  List,
  ListItem,
  ListItemButton,
  ListItemText,
  TextField,
  Typography,
} from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { presenceFresh, presenceOf } from '../state/presence_store';
import { privateKey } from '../state/models';
import { upsertConversation } from '../state/conversation_store';
import { PresenceStatus } from '@chirp/proto/social';
import { zh } from '../i18n/zh';

/**
 * Friends panel: roster with live presence dots, incoming pending requests,
 * outgoing pending markers, and the add-by-user-id entry. Rendered only when
 * the social plane is configured; everything degrades to a plain close
 * button when the social socket is down.
 */
export default function FriendsDialog({
  open,
  onClose,
  onOpenChannel,
}: {
  open: boolean;
  onClose: () => void;
  onOpenChannel: (key: string) => void;
}) {
  const { socialApi, friends, presence, conversations, auth } = useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const friendState = useStoreValue(friends);
  const presenceState = useStoreValue(presence);
  const [addId, setAddId] = useState('');
  const [error, setError] = useState<string | null>(null);

  // Refresh presence for the whole roster whenever the panel opens.
  useEffect(() => {
    if (open && socialApi) void socialApi.pullPresence(friendState.friends);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  const messageFriend = (peer: string): void => {
    const key = privateKey(selfId, peer);
    upsertConversation(conversations, {
      kind: 'private',
      key,
      channelId: [selfId, peer].sort().join('|'),
      peerId: peer,
      title: peer,
      unreadLocal: 0,
    });
    onClose();
    onOpenChannel(key);
  };

  const add = async (): Promise<void> => {
    const target = addId.trim();
    if (!target || !socialApi) return;
    if (target === selfId) {
      setError(zh.social.addSelf);
      return;
    }
    const code = await socialApi.addFriend(target);
    if (code !== 0) {
      setError(zh.social.addFailed);
      return;
    }
    setAddId('');
    setError(null);
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="xs" fullWidth>
      <DialogTitle>{zh.social.title}</DialogTitle>
      <DialogContent>
        <Box sx={{ display: 'flex', gap: 1, mb: 1 }}>
          <TextField
            size="small"
            fullWidth
            label={zh.social.addLabel}
            value={addId}
            onChange={(e) => {
              setAddId(e.target.value);
              setError(null);
            }}
            onKeyDown={(e) => e.key === 'Enter' && void add()}
            error={error !== null}
            helperText={error ?? undefined}
            data-testid="friend-add-input"
          />
          <Button variant="contained" onClick={() => void add()} disabled={addId.trim() === ''}>
            {zh.social.add}
          </Button>
        </Box>

        <List dense data-testid="friend-requests">
          {friendState.pendingIn.map((request) => (
            <ListItem
              key={request.requestId}
              secondaryAction={
                <Box sx={{ display: 'flex', gap: 0.5 }}>
                  <Button
                    size="small"
                    variant="contained"
                    data-testid={`accept-${request.fromUserId}`}
                    onClick={() => void socialApi?.respondRequest(request.requestId, true)}
                  >
                    {zh.social.accept}
                  </Button>
                  <Button
                    size="small"
                    data-testid={`decline-${request.fromUserId}`}
                    onClick={() => void socialApi?.respondRequest(request.requestId, false)}
                  >
                    {zh.social.decline}
                  </Button>
                </Box>
              }
            >
              <ListItemText
                primary={zh.social.requestFrom(request.fromUserId)}
                primaryTypographyProps={{ variant: 'body2' }}
              />
            </ListItem>
          ))}
        </List>

        <List dense data-testid="friend-list">
          {friendState.friends.map((friend) => {
            const entry = presenceOf(presenceState, friend);
            // A stale snapshot (missed disconnect notify) renders offline.
            const online =
              presenceFresh(presenceState, friend, Date.now()) &&
              entry?.status === PresenceStatus.ONLINE;
            return (
              <ListItem
                key={friend}
                disablePadding
                secondaryAction={
                  <Button size="small" onClick={() => messageFriend(friend)}>
                    {zh.social.message}
                  </Button>
                }
              >
                <ListItemButton onClick={() => messageFriend(friend)}>
                  <Badge
                    variant="dot"
                    color="success"
                    sx={{ mr: 1 }}
                    data-testid={`presence-${friend}`}
                    invisible={!online}
                  >
                    <ListItemText
                      primary={friend}
                      primaryTypographyProps={{ variant: 'body2' }}
                      secondary={online ? zh.social.online : undefined}
                    />
                  </Badge>
                </ListItemButton>
              </ListItem>
            );
          })}
          {friendState.friends.length === 0 && friendState.pendingIn.length === 0 && (
            <Typography variant="caption" color="text.secondary">
              {zh.social.empty}
            </Typography>
          )}
        </List>

        {friendState.pendingOut.length > 0 && (
          <Typography variant="caption" color="text.secondary">
            {zh.social.pendingOut(friendState.pendingOut.join(', '))}
          </Typography>
        )}
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>{zh.chat.cancel}</Button>
      </DialogActions>
    </Dialog>
  );
}
