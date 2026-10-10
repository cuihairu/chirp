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
import { GroupMemberRole } from '@chirp/proto/chat';
import type { GroupMember } from '@chirp/proto/chat';
import { zh } from '../i18n/zh';

/** MODERATOR 及以上(含 ADMIN/OWNER)可执行群禁言;与 GroupManager 台账同口径。 */
function isModeratorPlus(role: GroupMemberRole | undefined): boolean {
  return role === GroupMemberRole.MODERATOR || role === GroupMemberRole.ADMIN
    || role === GroupMemberRole.OWNER;
}

/**
 * Group management: roster, direct "invite" (the server adds members without
 * a pending state), owner-only kick, moderator+ mute/unmute, and leaving.
 * Own leave / being kicked drops the conversation locally, so the parent
 * navigates away via onLeft.
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

  // 禁言入口按名册角色放行:名册未载入时回退 ownerId 判定(群主恒可)。
  const selfRole = members.find((m) => m.userId === selfId)?.role;
  const canMute = members.length > 0 ? isModeratorPlus(selfRole) : isOwner;

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

  /** target 未禁言时弹时长输入(分钟);已禁言则解禁(seconds=0)。 */
  const setMute = async (target: string, currentlyMutedUntil: number): Promise<void> => {
    let seconds = 0;
    if (currentlyMutedUntil === 0) {
      const input = window.prompt(zh.chat.mutePrompt(target));
      if (input === null) return;
      seconds = Math.max(0, Math.floor(Number(input) * 60) || 0);
      if (seconds === 0) return;
    }
    const resp = await api.setGroupMute(groupId, target, seconds);
    if (resp.code !== 0) {
      setError(zh.chat.muteFailed);
      return;
    }
    setError(null);
    await reload();
  };

  /** 群昵称(2122/2123):本人设自己;MODERATOR+ 设他人(权限口径与禁言一致)。
   *  预填当前值,空串提交 = 清除。 */
  const setAlias = async (target: string): Promise<void> => {
    const current = members.find((m) => m.userId === target)?.alias ?? '';
    const input = window.prompt(zh.chat.aliasPrompt(target), current);
    if (input === null) return;
    const resp = await api.setMemberAlias(groupId, target, input.trim());
    if (resp.code !== 0) {
      setError(zh.chat.aliasFailed);
      return;
    }
    setError(null);
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
          {members.map((member) => {
            const muted = member.mutedUntilTs > Date.now();
            const displayName = member.alias || member.username || member.userId;
            return (
              <ListItem
                key={member.userId}
                secondaryAction={
                  <>
                    {(member.userId === selfId || canMute) && (
                      <Button
                        size="small"
                        onClick={() => void setAlias(member.userId)}
                        data-testid={`alias-${member.userId}`}
                      >
                        {zh.chat.alias}
                      </Button>
                    )}
                    {isOwner && member.userId !== selfId && (
                      <Button
                        size="small"
                        onClick={() => void kick(member.userId)}
                        data-testid={`kick-${member.userId}`}
                      >
                        {zh.chat.kick}
                      </Button>
                    )}
                    {canMute && member.userId !== selfId && (
                      <Button
                        size="small"
                        onClick={() => void setMute(member.userId, muted ? member.mutedUntilTs : 0)}
                        data-testid={`mute-${member.userId}`}
                      >
                        {muted ? zh.chat.unmute : zh.chat.mute}
                      </Button>
                    )}
                  </>
                }
              >
                <ListItemText
                  primary={
                    (member.userId === ownerId
                      ? `${displayName} (${zh.chat.ownerTag})`
                      : displayName) + (muted ? ` (${zh.chat.mutedTag})` : '')
                  }
                  secondary={member.alias ? (member.username || member.userId) : undefined}
                />
              </ListItem>
            );
          })}
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
