import { useState } from 'react';
import {
  Badge,
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
import { isLeaderOf } from '../state/party_store';
import { zh } from '../i18n/zh';

/**
 * Party management: create/join state, member roster with ready flags,
 * leader-only kick/transfer, invite by user id, and the incoming invite
 * queue. Sync is snapshot-driven — the api applies PARTY_STATE_CHANGED, this
 * component only reads the store and issues requests.
 */
export default function PartyDialog({ open, onClose }: { open: boolean; onClose: () => void }) {
  const { partyApi, partyState, auth } = useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const { party, invites } = useStoreValue(partyState);
  const [inviteId, setInviteId] = useState('');
  const [maxMembers, setMaxMembers] = useState('');
  const [error, setError] = useState<string | null>(null);
  const leader = party !== null && isLeaderOf({ party, invites }, selfId);

  const create = async (): Promise<void> => {
    if (!partyApi) return;
    const parsed = parseInt(maxMembers, 10);
    const code = await partyApi.createParty(Number.isFinite(parsed) && parsed > 0 ? parsed : 0);
    if (code !== 0) {
      setError(zh.party.createFailed);
    }
  };

  const invite = async (): Promise<void> => {
    if (!partyApi) return;
    const target = inviteId.trim();
    if (!target) return;
    const code = await partyApi.invite(target);
    if (code !== 0) {
      setError(zh.party.inviteFailed);
      return;
    }
    setInviteId('');
    setError(null);
  };

  const kick = async (target: string): Promise<void> => {
    if (!partyApi || !window.confirm(zh.party.kickConfirm(target))) return;
    await partyApi.kickMember(target);
  };

  const transfer = async (target: string): Promise<void> => {
    if (!partyApi || !window.confirm(zh.party.transferConfirm(target))) return;
    await partyApi.transferLeader(target);
  };

  const leave = async (): Promise<void> => {
    if (!partyApi || !window.confirm(zh.party.leaveConfirm)) return;
    await partyApi.leaveParty();
  };

  const disband = async (): Promise<void> => {
    if (!partyApi || !window.confirm(zh.party.disbandConfirm)) return;
    await partyApi.disbandParty();
  };

  const answerInvite = async (inviteId: string, accept: boolean): Promise<void> => {
    if (!partyApi) return;
    if (accept) {
      await partyApi.acceptInvite(inviteId);
    } else {
      await partyApi.declineInvite(inviteId);
    }
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="xs" fullWidth>
      <DialogTitle>{zh.party.title}</DialogTitle>
      <DialogContent>
        {party === null ? (
          <>
            <Typography variant="body2" color="text.secondary" sx={{ mb: 1 }}>
              {zh.party.noParty}
            </Typography>
            <Stack direction="row" spacing={1}>
              <TextField
                size="small"
                fullWidth
                label={zh.party.createLabel}
                value={maxMembers}
                onChange={(e) => setMaxMembers(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && void create()}
                data-testid="party-max-members"
              />
              <Button variant="contained" onClick={() => void create()}>
                {zh.party.create}
              </Button>
            </Stack>
          </>
        ) : (
          <>
            <Typography variant="caption" color="text.secondary" display="block" sx={{ mb: 1 }}>
              {zh.party.members(party.members.length, party.maxMembers)}
            </Typography>
            <Stack direction="row" spacing={1} sx={{ mb: 1 }}>
              <TextField
                size="small"
                fullWidth
                label={zh.party.inviteLabel}
                value={inviteId}
                onChange={(e) => {
                  setInviteId(e.target.value);
                  setError(null);
                }}
                onKeyDown={(e) => e.key === 'Enter' && void invite()}
                error={error !== null}
                helperText={error ?? undefined}
                data-testid="party-invite-input"
              />
              <Button variant="contained" onClick={() => void invite()} disabled={inviteId.trim() === ''}>
                {zh.party.invite}
              </Button>
            </Stack>
            <List dense data-testid="party-member-list">
              {party.members.map((member) => {
                const self = member.userId === selfId;
                return (
                  <ListItem
                    key={member.userId}
                    secondaryAction={
                      <>
                        {self ? (
                          <Button
                            size="small"
                            onClick={() => void partyApi?.setReady(!member.ready)}
                            data-testid="party-ready-toggle"
                          >
                            {member.ready ? zh.party.notReady : zh.party.ready}
                          </Button>
                        ) : (
                          leader && (
                            <>
                              <Button size="small" onClick={() => void kick(member.userId)}>
                                {zh.party.kick}
                              </Button>
                              <Button size="small" onClick={() => void transfer(member.userId)}>
                                {zh.party.transferLeader}
                              </Button>
                            </>
                          )
                        )}
                      </>
                    }
                  >
                    <ListItemText
                      primary={
                        member.userId === party.leaderId
                          ? `${member.userId} (${zh.party.leaderTag})`
                          : member.userId
                      }
                      secondary={member.ready ? zh.party.readyTag : undefined}
                    />
                  </ListItem>
                );
              })}
            </List>
          </>
        )}
        {invites.length > 0 && (
          <List dense data-testid="party-invite-list" sx={{ mt: 1 }}>
            {invites.map((invite) => (
              <ListItem
                key={invite.inviteId}
                secondaryAction={
                  <>
                    <Button
                      size="small"
                      variant="contained"
                      onClick={() => void answerInvite(invite.inviteId, true)}
                      data-testid={`party-accept-${invite.fromUserId}`}
                    >
                      {zh.social.accept}
                    </Button>
                    <Button
                      size="small"
                      onClick={() => void answerInvite(invite.inviteId, false)}
                    >
                      {zh.social.decline}
                    </Button>
                  </>
                }
              >
                <ListItemText primary={zh.party.inviteFrom(invite.fromUserId)} />
              </ListItem>
            ))}
          </List>
        )}
      </DialogContent>
      <DialogActions>
        {party !== null && (
          <Button color="error" onClick={() => void leave()} data-testid="party-leave">
            {zh.party.leave}
          </Button>
        )}
        {leader && (
          <Button color="error" onClick={() => void disband()} data-testid="party-disband">
            {zh.party.disband}
          </Button>
        )}
        <Button onClick={onClose}>{zh.chat.cancel}</Button>
      </DialogActions>
    </Dialog>
  );
}

/** Entry button with the pending-invite badge (hidden when party is down). */
export function PartyButton({ onClick }: { onClick: () => void }) {
  const { partyState } = useServices();
  const { invites } = useStoreValue(partyState);
  return (
    <Badge badgeContent={invites.length} color="error" data-testid="party-invite-badge">
      <Button variant="text" fullWidth onClick={onClick}>
        {zh.party.title}
      </Button>
    </Badge>
  );
}
