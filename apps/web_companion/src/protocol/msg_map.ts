import * as Auth from '@chirp/proto/auth';
import * as Chat from '@chirp/proto/chat';
import { MsgID } from '@chirp/proto/gateway';

/**
 * A typed request/response pair. On this protocol a response is correlated by
 * Packet.sequence, not by msgId — the server answers with the RESP msgId and
 * echoes the request's sequence. The spec keeps both ids and the codec in one
 * place so call sites never touch raw msgIds.
 */
export interface MessageSpec<Req, Resp> {
  readonly reqMsgId: MsgID;
  readonly respMsgId: MsgID;
  /** Accepts a partial; unset fields fall back to proto defaults via fromPartial. */
  encodeRequest(message: Partial<Req>): Uint8Array;
  decodeResponse(bytes: Uint8Array): Resp;
}

interface ProtoMessage<T> {
  encode(message: T, writer?: unknown): { finish(): Uint8Array };
  decode(input: Uint8Array, length?: number): T;
  fromPartial(partial: Partial<T>): T;
}

function defineSpec<Req, Resp>(
  reqMsgId: MsgID,
  respMsgId: MsgID,
  Req: ProtoMessage<Req>,
  Resp: ProtoMessage<Resp>,
): MessageSpec<Req, Resp> {
  return {
    reqMsgId,
    respMsgId,
    encodeRequest: (message) => Req.encode(Req.fromPartial(message)).finish(),
    decodeResponse: (bytes) => Resp.decode(bytes),
  };
}

// Core pair used by the connection layer itself.
export const LOGIN = defineSpec(
  MsgID.LOGIN_REQ,
  MsgID.LOGIN_RESP,
  Auth.LoginRequest,
  Auth.LoginResponse,
);
export const LOGOUT = defineSpec(
  MsgID.LOGOUT_REQ,
  MsgID.LOGOUT_RESP,
  Auth.LogoutRequest,
  Auth.LogoutResponse,
);

// Chat (direct chat WS entry; the full table grows in the api layer).
export const SEND_MESSAGE = defineSpec(
  MsgID.SEND_MESSAGE_REQ,
  MsgID.SEND_MESSAGE_RESP,
  Chat.SendMessageRequest,
  Chat.SendMessageResponse,
);
export const GET_HISTORY = defineSpec(
  MsgID.GET_HISTORY_REQ,
  MsgID.GET_HISTORY_RESP,
  Chat.GetHistoryRequest,
  Chat.GetHistoryResponse,
);
export const MARK_READ = defineSpec(
  MsgID.MARK_READ_REQ,
  MsgID.MARK_READ_RESP,
  Chat.MarkReadRequest,
  Chat.MarkReadResponse,
);
export const GET_UNREAD_COUNT = defineSpec(
  MsgID.GET_UNREAD_COUNT_REQ,
  MsgID.GET_UNREAD_COUNT_RESP,
  Chat.GetUnreadCountRequest,
  Chat.GetUnreadCountResponse,
);
export const GET_USER_GROUPS = defineSpec(
  MsgID.GET_USER_GROUPS_REQ,
  MsgID.GET_USER_GROUPS_RESP,
  Chat.GetUserGroupsRequest,
  Chat.GetUserGroupsResponse,
);
