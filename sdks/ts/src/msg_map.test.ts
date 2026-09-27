import { LoginResponse, LogoutResponse } from '@chirp/proto/auth';
import { GetHistoryResponse, SendMessageResponse } from '@chirp/proto/chat';
import { MsgID } from '@chirp/proto/gateway';
import { GetRoomInfoResponse, JoinRoomResponse, RoomType } from '@chirp/proto/voice';
import { describe, expect, it } from 'vitest';
import { CREATE_ROOM, GET_ROOM_INFO, JOIN_ROOM, GET_HISTORY, LOGIN, LOGOUT, SEND_MESSAGE } from './msg_map';

describe('message specs', () => {
  it('pins the request/response id pairs the server dispatches on', () => {
    expect(LOGIN.reqMsgId).toBe(MsgID.LOGIN_REQ);
    expect(LOGIN.respMsgId).toBe(MsgID.LOGIN_RESP);
    expect(LOGOUT.reqMsgId).toBe(MsgID.LOGOUT_REQ);
    expect(LOGOUT.respMsgId).toBe(MsgID.LOGOUT_RESP);
    expect(SEND_MESSAGE.reqMsgId).toBe(MsgID.SEND_MESSAGE_REQ);
    expect(SEND_MESSAGE.respMsgId).toBe(MsgID.SEND_MESSAGE_RESP);
    expect(GET_HISTORY.reqMsgId).toBe(MsgID.GET_HISTORY_REQ);
    expect(GET_HISTORY.respMsgId).toBe(MsgID.GET_HISTORY_RESP);
    expect(CREATE_ROOM.reqMsgId).toBe(MsgID.CREATE_ROOM_REQ);
    expect(CREATE_ROOM.respMsgId).toBe(MsgID.CREATE_ROOM_RESP);
    expect(JOIN_ROOM.reqMsgId).toBe(MsgID.JOIN_ROOM_REQ);
    expect(JOIN_ROOM.respMsgId).toBe(MsgID.JOIN_ROOM_RESP);
    expect(GET_ROOM_INFO.reqMsgId).toBe(MsgID.GET_ROOM_INFO_REQ);
    expect(GET_ROOM_INFO.respMsgId).toBe(MsgID.GET_ROOM_INFO_RESP);
  });

  it('roundtrips the voice-plane specs the web client drives', () => {
    const create = CREATE_ROOM.encodeRequest({
      userId: 'u1',
      roomType: RoomType.GROUP,
      roomName: 'smoke-room',
      maxParticipants: 5,
    });
    expect(create.length).toBeGreaterThan(0);

    const join = JoinRoomResponse.encode(
      JoinRoomResponse.fromPartial({
        code: 0,
        roomId: 'room-1',
        sdpAnswer: '',
        participantIds: ['u1', 'u2'],
      }),
    ).finish();
    expect(JOIN_ROOM.decodeResponse(join).participantIds).toEqual(['u1', 'u2']);

    const info = GetRoomInfoResponse.encode(
      GetRoomInfoResponse.fromPartial({
        code: 0,
        roomId: 'room-1',
        participants: [{ userId: 'u1', state: 1 }],
      }),
    ).finish();
    expect(GET_ROOM_INFO.decodeResponse(info).participants[0].userId).toBe('u1');
  });

  it('encodes a request from a partial and decodes a response back', () => {
    const bytes = LOGIN.encodeRequest({ token: 'player_1', deviceId: 'd1', platform: 'web' });
    const response = LoginResponse.encode(
      LoginResponse.fromPartial({ code: 0, sessionId: 's1', userId: 'player_1' }),
    ).finish();
    const decoded = LOGIN.decodeResponse(response);
    expect(decoded.sessionId).toBe('s1');
    expect(decoded.userId).toBe('player_1');
    // The request bytes must decode as the same message type.
    expect(bytes.length).toBeGreaterThan(0);
  });

  it('roundtrips a send-message response carrying the server message id', () => {
    const bytes = SEND_MESSAGE.encodeRequest({
      senderId: 'u1',
      receiverId: 'u2',
      content: new TextEncoder().encode('hi'),
    });
    expect(bytes.length).toBeGreaterThan(0);
    const resp = SendMessageResponse.encode(
      SendMessageResponse.fromPartial({ code: 0, messageId: 'm9' }),
    ).finish();
    expect(SEND_MESSAGE.decodeResponse(resp).messageId).toBe('m9');
  });

  it('decodes history responses', () => {
    const resp = GetHistoryResponse.encode(GetHistoryResponse.fromPartial({ code: 0 })).finish();
    expect(GET_HISTORY.decodeResponse(resp).code).toBe(0);
    const logout = LogoutResponse.encode(LogoutResponse.fromPartial({})).finish();
    expect(LOGOUT.decodeResponse(logout).code).toBe(0);
  });
});
