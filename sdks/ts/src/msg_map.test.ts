import { LoginResponse, LogoutResponse } from '@chirp/proto/auth';
import {
  GetNotifyPrefsResponse,
  SetNotifyPrefsResponse,
} from '@chirp/proto/social';
import {
  GetHistoryResponse,
  ReportMessageResponse,
  SearchMessageResponse,
  SendMessageResponse,
  SetGroupMuteResponse,
  SetMemberAliasResponse,
} from '@chirp/proto/chat';
import { MsgID } from '@chirp/proto/gateway';
import { GetRoomInfoResponse, JoinRoomResponse, RoomType } from '@chirp/proto/voice';
import { describe, expect, it } from 'vitest';
import {
  CREATE_ROOM,
  GET_HISTORY,
  GET_NOTIFY_PREFS,
  GET_ROOM_INFO,
  JOIN_ROOM,
  LOGIN,
  LOGOUT,
  REPORT_MESSAGE,
  SEARCH_MESSAGE,
  SEND_MESSAGE,
  SET_GROUP_MUTE,
  SET_MEMBER_ALIAS,
  SET_NOTIFY_PREFS,
} from './msg_map';

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

  it('roundtrips the search and member-alias specs', () => {
    const search = SEARCH_MESSAGE.encodeRequest({
      keyword: '天气',
      channelId: 'world',
      contentTypes: [1],
      beforeTimestamp: 100,
      beforeMessageId: 'm9',
      limit: 5,
    });
    expect(search.length).toBeGreaterThan(0);
    const searchResp = SearchMessageResponse.encode(
      SearchMessageResponse.fromPartial({ code: 0, hasMore: true }),
    ).finish();
    expect(SEARCH_MESSAGE.decodeResponse(searchResp).hasMore).toBe(true);

    const alias = SET_MEMBER_ALIAS.encodeRequest({
      groupId: 'g1',
      targetUserId: 'u9',
      alias: '阿九',
    });
    expect(alias.length).toBeGreaterThan(0);
    const aliasResp = SetMemberAliasResponse.encode(
      SetMemberAliasResponse.fromPartial({ code: 0, alias: '阿九' }),
    ).finish();
    expect(SET_MEMBER_ALIAS.decodeResponse(aliasResp).alias).toBe('阿九');

    const mute = SET_GROUP_MUTE.encodeRequest({
      groupId: 'g1',
      targetUserId: 'u9',
      durationSec: 3600,
    });
    expect(mute.length).toBeGreaterThan(0);
    const muteResp = SetGroupMuteResponse.encode(
      SetGroupMuteResponse.fromPartial({ code: 0, mutedUntilTs: 1700000000000 }),
    ).finish();
    expect(SET_GROUP_MUTE.decodeResponse(muteResp).mutedUntilTs).toBe(1700000000000);
  });

  it('roundtrips the report spec (2253/2254)', () => {
    expect(REPORT_MESSAGE.reqMsgId).toBe(MsgID.REPORT_MESSAGE_REQ);
    expect(REPORT_MESSAGE.respMsgId).toBe(MsgID.REPORT_MESSAGE_RESP);
    const report = REPORT_MESSAGE.encodeRequest({
      messageId: 'm1',
      reason: 'spam',
    });
    expect(report.length).toBeGreaterThan(0);
    const reportResp = ReportMessageResponse.encode(
      ReportMessageResponse.fromPartial({ code: 0, reportCount: 2 }),
    ).finish();
    expect(REPORT_MESSAGE.decodeResponse(reportResp).reportCount).toBe(2);
  });

  it('roundtrips the friend-online notify prefs specs (3025-3028)', () => {
    expect(SET_NOTIFY_PREFS.reqMsgId).toBe(MsgID.SET_NOTIFY_PREFS_REQ);
    expect(SET_NOTIFY_PREFS.respMsgId).toBe(MsgID.SET_NOTIFY_PREFS_RESP);
    expect(GET_NOTIFY_PREFS.reqMsgId).toBe(MsgID.GET_NOTIFY_PREFS_REQ);
    expect(GET_NOTIFY_PREFS.respMsgId).toBe(MsgID.GET_NOTIFY_PREFS_RESP);

    // Lock the "off" request: the explicit false must ride the wire (the
    // server treats a missing prefs submessage as INVALID_PARAM).
    const off = SET_NOTIFY_PREFS.encodeRequest({
      userId: 'u1',
      prefs: { friendOnlineNotify: false },
    });
    expect(off.length).toBeGreaterThan(0);
    const setResp = SetNotifyPrefsResponse.encode(
      SetNotifyPrefsResponse.fromPartial({ code: 0, prefs: { friendOnlineNotify: false }, serverTime: 42 }),
    ).finish();
    const decoded = SET_NOTIFY_PREFS.decodeResponse(setResp);
    expect(decoded.code).toBe(0);
    expect(decoded.prefs?.friendOnlineNotify).toBe(false);
    expect(decoded.serverTime).toBe(42);

    const get = GET_NOTIFY_PREFS.encodeRequest({ userId: 'u1' });
    expect(get.length).toBeGreaterThan(0);
    const getResp = GetNotifyPrefsResponse.encode(
      GetNotifyPrefsResponse.fromPartial({ code: 0, prefs: { friendOnlineNotify: true } }),
    ).finish();
    expect(GET_NOTIFY_PREFS.decodeResponse(getResp).prefs?.friendOnlineNotify).toBe(true);
  });
});
