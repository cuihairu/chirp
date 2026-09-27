import { describe, expect, it, vi } from 'vitest';
import { fireEvent, screen, waitFor } from '@testing-library/react';
import { MsgID } from '@chirp/proto/gateway';
import { ParticipantState } from '@chirp/proto/voice';
import { renderLoggedIn } from '../test-utils';
import { applyRoomSnapshot } from '../state/voice_store';
import { FakeChatConnection } from '../state/test_helpers';
import { zh } from '../i18n/zh';
import VoiceDialog, { VoiceButton } from './VoiceDialog';

const roster = () => ({
  code: 0,
  roomId: 'room-1',
  roomName: 'smoke-room',
  roomType: 1,
  maxParticipants: 5,
  participants: [
    { userId: 'user_a', state: ParticipantState.CONNECTED, muted: false, deafened: false },
    { userId: 'user_b', state: ParticipantState.CONNECTED, muted: false, deafened: false },
  ],
});

describe('VoiceDialog', () => {
  it('offers create/join when roomless and creates a room on demand', async () => {
    const voice = new FakeChatConnection();
    voice.setResponder(async (msgId, req) => {
      if (msgId === MsgID.CREATE_ROOM_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', roomType: 1, roomName: 'crew' });
        return { code: 0, roomId: 'room-1' };
      }
      if (msgId === MsgID.GET_ROOM_INFO_REQ) return roster();
      return { code: 0 };
    });
    await renderLoggedIn(<VoiceDialog open onClose={vi.fn()} />, { voiceConn: voice });
    expect(screen.getByText(zh.voice.noRoom)).toBeTruthy();

    fireEvent.change(screen.getByTestId('voice-room-name').querySelector('input')!, {
      target: { value: 'crew' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.voice.create }));

    await waitFor(() => expect(screen.getByText(zh.voice.participants(2, 5))).toBeTruthy());
    expect(screen.getByText('user_b')).toBeTruthy();
  });

  it('joins by room id and leaves on confirm', async () => {
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    const voice = new FakeChatConnection();
    voice.setResponder(async (msgId) => {
      if (msgId === MsgID.JOIN_ROOM_REQ) return { code: 0, roomId: 'room-1' };
      if (msgId === MsgID.GET_ROOM_INFO_REQ) return roster();
      return { code: 0 };
    });
    await renderLoggedIn(<VoiceDialog open onClose={vi.fn()} />, { voiceConn: voice });
    fireEvent.change(screen.getByTestId('voice-join-input').querySelector('input')!, {
      target: { value: 'room-1' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.voice.join }));
    await waitFor(() => expect(screen.getByText(zh.voice.participants(2, 5))).toBeTruthy());

    fireEvent.click(screen.getByText(zh.voice.leave));
    await waitFor(() => expect(screen.getByText(zh.voice.noRoom)).toBeTruthy());
  });

  it('shows join failure copy when joining a missing room', async () => {
    const voice = new FakeChatConnection();
    voice.setResponder(async () => ({ code: 21 }));
    await renderLoggedIn(<VoiceDialog open onClose={vi.fn()} />, { voiceConn: voice });
    fireEvent.change(screen.getByTestId('voice-join-input').querySelector('input')!, {
      target: { value: 'no-such-room' },
    });
    fireEvent.click(screen.getByRole('button', { name: zh.voice.join }));
    await waitFor(() => expect(screen.getByText(zh.voice.joinFailed)).toBeTruthy());
  });

  it('renders the roster with self tag and toggles mute with a confirm', async () => {
    const voice = new FakeChatConnection();
    voice.setResponder(async (msgId, req) => {
      if (msgId === MsgID.SET_MUTE_REQ) {
        expect(req).toMatchObject({ userId: 'user_a', roomId: 'room-1', muted: true });
        return { code: 0 };
      }
      return { code: 0 };
    });
    await renderLoggedIn(<VoiceDialog open onClose={vi.fn()} />, {
      voiceConn: voice,
      prepare: ({ services }) => {
        applyRoomSnapshot(services.voiceState, {
          roomId: 'room-1',
          roomName: 'smoke-room',
          maxParticipants: 5,
          participants: [
            { userId: 'user_a', state: ParticipantState.CONNECTED, muted: false, deafened: false },
            { userId: 'user_b', state: ParticipantState.MUTED, muted: true },
          ],
        });
      },
    });

    expect(screen.getByText(`user_a (${zh.voice.selfTag})`)).toBeTruthy();
    expect(screen.getByText(zh.voice.stateMuted)).toBeTruthy();

    fireEvent.click(screen.getByTestId('voice-mute-toggle'));
    await waitFor(() =>
      expect(voice.requests.some((r) => r.msgId === MsgID.SET_MUTE_REQ)).toBe(true),
    );
  });

  it('VoiceButton reflects an active room membership', async () => {
    await renderLoggedIn(<VoiceButton onClick={vi.fn()} />, {
      prepare: ({ services }) => {
        applyRoomSnapshot(services.voiceState, {
          roomId: 'room-1',
          roomName: 'smoke-room',
          maxParticipants: 5,
          participants: [],
        });
      },
    });
    expect(screen.getByTestId('voice-button-active')).toBeTruthy();
  });
});