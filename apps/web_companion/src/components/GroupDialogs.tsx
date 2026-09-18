import { useCallback, useEffect, useState } from 'react';
import {
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  List,
  ListItem,
  ListItemText,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { ErrorCode } from '@chirp/proto/common';
import type { GroupMember } from '@chirp/proto/chat';
import { zh } from '../i18n/zh';

/**
 * Group management: roster, direct "invite" (the server adds members without
 * a pending state), owner-only kick, and leaving. Own leave / being kicked
 * drops the conversation locally, so the parent navigates away via onLeft.
 */
export default function GroupSettingsDialog({
  groupId,
  title,
  ownerId,
  open,
  onClose,
  onLeft,
}: {
  groupId: string;
  title: string;
  /** Group owner user id; undefined when the roster has not loaded yet. */
  ownerId?: string;
  open: boolean;
  onClose: () => void;
  onLeft: () => void;
}) {
  const { api, auth } = useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const isOwner = ownerId === selfId;
  const [members, setMembers] = useState<GroupMember[]>([]);
  const [inviteId, setInviteId] = useState('');
  const [error, setError] = useState<string | null>(null);

  const reload = useCallback(async (): Promise<void> => {
    setMembers(await api.loadGroupMembers(groupId));
  }, [api, groupId]);

  useEffect(() => {
    if (open) void reload();
  }, [open, reload]);

  const invite = async (): Promise<void> => {
    const target = inviteId.trim();
    if (!target) return;
    const code = await api.inviteToGroup(groupId, target);
    if (code === ErrorCode.SESSION_EXPIRED) return;
    if (code !== 0) {
      setError(zh.chat.inviteFailed);
      return;
    }
    setInviteId('');
    setError(null);
    await reload();
  };

  const kick = async (target: string): Promise<void> => {
    const code = await api.kickMember(groupId, target);
    if (code !== 0) return;
    await reload();
  };

  const leave = async (): Promise<void> => {
    if (!window.confirm(zh.chat.leaveConfirm)) return;
    await api.leaveGroup(groupId);
    onLeft();
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="xs" fullWidth>
      <DialogTitle>{zh.chat.groupSettings(title)}</DialogTitle>
      <DialogContent>
        <Stack direction="row" spacing={1} sx={{ mb: 1 }}>
          <TextField
            size="small"
            fullWidth
            label={zh.chat.inviteLabel}
            value={inviteId}
            onChange={(e) => {
              setInviteId(e.target.value);
              setError(null);
            }}
            onKeyDown={(e) => e.key === 'Enter' && void invite()}
            error={error !== null}
            helperText={error ?? undefined}
            data-testid="invite-input"
          />
          <Button variant="contained" onClick={() => void invite()} disabled={inviteId.trim() === ''}>
            {zh.chat.invite}
          </Button>
        </Stack>
        <List dense data-testid="member-list">
          {members.map((member) => (
            <ListItem
              key={member.userId}
              secondaryAction={
                isOwner && member.userId !== selfId && (
                  <Button
                    size="small"
                    onClick={() => void kick(member.userId)}
                    data-testid={`kick-${member.userId}`}
                  >
                    {zh.chat.kick}
                  </Button>
                )
              }
            >
              <ListItemText
                primary={
                  member.userId === ownerId
                    ? `${member.username || member.userId} (${zh.chat.ownerTag})`
                    : member.username || member.userId
                }
              />
            </ListItem>
          ))}
          {members.length === 0 && (
            <Typography variant="caption" color="text.secondary">
              {zh.chat.loading}
            </Typography>
          )}
        </List>
      </DialogContent>
      <DialogActions>
        <Button color="error" onClick={() => void leave()} data-testid="leave-group">
          {zh.chat.leave}
        </Button>
        <Button onClick={onClose}>{zh.chat.cancel}</Button>
      </DialogActions>
    </Dialog>
  );
}
