import { describe, expect, it } from 'vitest';
import { MsgID, Packet } from '@chirp/proto/gateway';
import { LoginRequest, LoginResponse } from '@chirp/proto/auth';
import { ChatMessage, ChannelType, MsgType, SendMessageRequest } from '@chirp/proto/chat';

// The generated code in proto/ts is the contract with the C++ backend. These
// tests pin the wire values the app depends on, so a proto regeneration that
// changes them fails here instead of at runtime against a real server.
describe('generated protobuf wire contract', () => {
  it('keeps the MsgID values the protocol layer dispatches on', () => {
    expect(MsgID.HEARTBEAT_PING).toBe(1001);
    expect(MsgID.LOGIN_REQ).toBe(1003);
    expect(MsgID.KICK_NOTIFY).toBe(1005);
    expect(MsgID.SEND_MESSAGE_REQ).toBe(2001);
    expect(MsgID.TYPING_INDICATOR_NOTIFY).toBe(2208);
    expect(MsgID.MESSAGE_ACK).toBe(2209);
    expect(MsgID.INVITE_TO_GROUP_REQ).toBe(2115);
    expect(MsgID.GET_UNREAD_COUNT_REQ).toBe(2205);
    expect(MsgID.ADD_FRIEND_REQ).toBe(3001);
    expect(MsgID.SET_PRESENCE_REQ).toBe(3017);
    expect(MsgID.CREATE_ROOM_REQ).toBe(4001);
  });

  it('roundtrips the Packet envelope with a number int64 sequence', () => {
    const body = SendMessageRequest.encode(
      SendMessageRequest.fromPartial({
        senderId: 'user_1',
        receiverId: 'user_2',
        channelType: ChannelType.PRIVATE,
        msgType: MsgType.TEXT,
        content: new TextEncoder().encode('ping'),
        clientTimestamp: 1726632000000,
      }),
    ).finish();
    const packet = Packet.encode(
      Packet.fromPartial({ msgId: MsgID.SEND_MESSAGE_REQ, sequence: 42, body }),
    ).finish();
    const decoded = Packet.decode(packet);
    expect(decoded.msgId).toBe(MsgID.SEND_MESSAGE_REQ);
    expect(decoded.sequence).toBe(42);
    const sent = SendMessageRequest.decode(decoded.body);
    expect(sent.senderId).toBe('user_1');
    expect(sent.receiverId).toBe('user_2');
    expect(new TextDecoder().decode(sent.content)).toBe('ping');
    expect(sent.clientTimestamp).toBe(1726632000000);
  });

  it('roundtrips login bodies with camelCase fields', () => {
    const req = LoginRequest.encode(
      LoginRequest.fromPartial({
        token: 'player_1',
        deviceId: 'dev-web-1',
        platform: 'web',
        supportsMessageAck: true,
      }),
    ).finish();
    const parsed = LoginRequest.decode(req);
    expect(parsed.token).toBe('player_1');
    expect(parsed.deviceId).toBe('dev-web-1');
    expect(parsed.platform).toBe('web');
    expect(parsed.supportsMessageAck).toBe(true);

    const resp = LoginResponse.encode(
      LoginResponse.fromPartial({ code: 0, sessionId: 'sess_1', userId: 'player_1', kickPrevious: true }),
    ).finish();
    const parsedResp = LoginResponse.decode(resp);
    expect(parsedResp.sessionId).toBe('sess_1');
    expect(parsedResp.kickPrevious).toBe(true);
  });

  it('roundtrips a chat message payload', () => {
    const msg = ChatMessage.encode(
      ChatMessage.fromPartial({
        messageId: 'm1',
        senderId: 'user_1',
        receiverId: 'user_2',
        channelType: ChannelType.PRIVATE,
        msgType: MsgType.TEXT,
        content: new TextEncoder().encode('hello'),
        timestamp: 1726632000123,
      }),
    ).finish();
    const parsed = ChatMessage.decode(msg);
    expect(parsed.messageId).toBe('m1');
    expect(parsed.channelType).toBe(ChannelType.PRIVATE);
    expect(parsed.timestamp).toBe(1726632000123);
  });
});
