import { useState } from 'react';
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
import { ParticipantState } from '@chirp/proto/voice';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { selfParticipantOf } from '../state/voice_store';
import { zh } from '../i18n/zh';

/**
 * Voice room management (protocol layer): create / join by room id, the
 * member roster with state tags, own mute / deafen toggles, and leave.
 * No media plane — the UI is a protocol-plane closed loop that holds the
 * roster/state surface the voice service offers without driving WebRTC.
 */
export default function VoiceDialog({ open, onClose }: { open: boolean; onClose: () => void }) {
  const { voiceApi, voiceState, auth } = useServices();
  const selfId = useStoreValue(auth).userId ?? '';
  const { room } = useStoreValue(voiceState);
  const [roomName, setRoomName] = useState('');
  const [joinId, setJoinId] = useState('');
  const [error, setError] = useState<string | null>(null);
  const self = selfParticipantOf({ room }, selfId);

  const create = async (): Promise<void> => {
    if (!voiceApi) return;
    const name = roomName.trim();
    const code = await voiceApi.createRoom(name, 0);
    if (code !== 0) setError(zh.voice.createFailed);
    else {
      setRoomName('');
      setError(null);
    }
  };

  const join = async (): Promise<void> => {
    if (!voiceApi) return;
    const roomId = joinId.trim();
    if (!roomId) return;
    const code = await voiceApi.joinRoom(roomId);
    if (code !== 0) {
      setError(zh.voice.joinFailed);
      return;
    }
    setJoinId('');
    setError(null);
  };

  const leave = async (): Promise<void> => {
    if (!voiceApi || !window.confirm(zh.voice.leaveConfirm)) return;
    await voiceApi.leaveRoom();
  };

  const toggleMute = (): void => {
    if (!voiceApi || !self) return;
    void voiceApi.setMute(!self.muted);
  };

  const toggleDeafen = (): void => {
    if (!voiceApi || !self) return;
    void voiceApi.setDeafen(!self.deafened);
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="xs" fullWidth>
      <DialogTitle>{zh.voice.title}</DialogTitle>
      <DialogContent>
        {room === null ? (
          <>
            <Typography variant="body2" color="text.secondary" sx={{ mb: 1 }}>
              {zh.voice.noRoom}
            </Typography>
            <Stack direction="row" spacing={1} sx={{ mb: 2 }}>
              <TextField
                size="small"
                fullWidth
                label={zh.voice.createLabel}
                value={roomName}
                onChange={(e) => setRoomName(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && void create()}
                data-testid="voice-room-name"
              />
              <Button variant="contained" onClick={() => void create()}>
                {zh.voice.create}
              </Button>
            </Stack>
            <Stack direction="row" spacing={1}>
              <TextField
                size="small"
                fullWidth
                label={zh.voice.joinLabel}
                value={joinId}
                onChange={(e) => {
                  setJoinId(e.target.value);
                  setError(null);
                }}
                onKeyDown={(e) => e.key === 'Enter' && void join()}
                error={error !== null}
                helperText={error ?? undefined}
                data-testid="voice-join-input"
              />
              <Button variant="outlined" onClick={() => void join()} disabled={joinId.trim() === ''}>
                {zh.voice.join}
              </Button>
            </Stack>
          </>
        ) : (
          <>
            <Typography variant="caption" color="text.secondary" display="block" sx={{ mb: 1 }}>
              {zh.voice.participants(room.participants.length, room.maxParticipants)}
            </Typography>
            {self && (
              <Stack direction="row" spacing={1} sx={{ mb: 1 }}>
                <Button size="small" onClick={toggleMute} data-testid="voice-mute-toggle">
                  {self.muted ? zh.voice.unmute : zh.voice.mute}
                </Button>
                <Button size="small" onClick={toggleDeafen} data-testid="voice-deafen-toggle">
                  {self.deafened ? zh.voice.undeafen : zh.voice.deafen}
                </Button>
              </Stack>
            )}
            <List dense data-testid="voice-member-list">
              {room.participants.map((participant) => (
                <ListItem key={participant.userId}>
                  <ListItemText
                    primary={
                      participant.userId === selfId
                        ? `${participant.userId} (${zh.voice.selfTag})`
                        : participant.userId
                    }
                    secondary={stateLabel(participant.state)}
                  />
                </ListItem>
              ))}
            </List>
          </>
        )}
      </DialogContent>
      <DialogActions>
        {room !== null && (
          <Button color="error" onClick={() => void leave()} data-testid="voice-leave">
            {zh.voice.leave}
          </Button>
        )}
        <Button onClick={onClose}>{zh.chat.cancel}</Button>
      </DialogActions>
    </Dialog>
  );
}

function stateLabel(state: ParticipantState): string {
  switch (state) {
    case ParticipantState.MUTED:
      return zh.voice.stateMuted;
    case ParticipantState.DEAFENED:
      return zh.voice.stateDeafened;
    default:
      return zh.voice.stateConnected;
  }
}

/** Entry button (hidden entirely when the voice plane is down). */
export function VoiceButton({ onClick }: { onClick: () => void }) {
  const { voiceState } = useServices();
  const { room } = useStoreValue(voiceState);
  return (
    <Button variant="text" fullWidth onClick={onClick} data-testid={room ? 'voice-button-active' : 'voice-button'}>
      {room ? zh.voice.titleActive : zh.voice.title}
    </Button>
  );
}