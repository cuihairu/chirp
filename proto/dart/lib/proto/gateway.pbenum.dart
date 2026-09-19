//
//  Generated code. Do not modify.
//  source: proto/gateway.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

/// Message ID definition for dispatching
class MsgID extends $pb.ProtobufEnum {
  static const MsgID UNKNOWN = MsgID._(0, _omitEnumNames ? '' : 'UNKNOWN');
  static const MsgID HEARTBEAT_PING = MsgID._(1001, _omitEnumNames ? '' : 'HEARTBEAT_PING');
  static const MsgID HEARTBEAT_PONG = MsgID._(1002, _omitEnumNames ? '' : 'HEARTBEAT_PONG');
  static const MsgID LOGIN_REQ = MsgID._(1003, _omitEnumNames ? '' : 'LOGIN_REQ');
  static const MsgID LOGIN_RESP = MsgID._(1004, _omitEnumNames ? '' : 'LOGIN_RESP');
  static const MsgID KICK_NOTIFY = MsgID._(1005, _omitEnumNames ? '' : 'KICK_NOTIFY');
  static const MsgID LOGOUT_REQ = MsgID._(1006, _omitEnumNames ? '' : 'LOGOUT_REQ');
  static const MsgID LOGOUT_RESP = MsgID._(1007, _omitEnumNames ? '' : 'LOGOUT_RESP');
  static const MsgID REGISTER_REQ = MsgID._(1008, _omitEnumNames ? '' : 'REGISTER_REQ');
  static const MsgID REGISTER_RESP = MsgID._(1009, _omitEnumNames ? '' : 'REGISTER_RESP');
  static const MsgID PASSWORD_LOGIN_REQ = MsgID._(1010, _omitEnumNames ? '' : 'PASSWORD_LOGIN_REQ');
  static const MsgID PASSWORD_LOGIN_RESP = MsgID._(1011, _omitEnumNames ? '' : 'PASSWORD_LOGIN_RESP');
  static const MsgID REFRESH_TOKEN_REQ = MsgID._(1012, _omitEnumNames ? '' : 'REFRESH_TOKEN_REQ');
  static const MsgID REFRESH_TOKEN_RESP = MsgID._(1013, _omitEnumNames ? '' : 'REFRESH_TOKEN_RESP');
  static const MsgID GET_SESSIONS_REQ = MsgID._(1014, _omitEnumNames ? '' : 'GET_SESSIONS_REQ');
  static const MsgID GET_SESSIONS_RESP = MsgID._(1015, _omitEnumNames ? '' : 'GET_SESSIONS_RESP');
  static const MsgID REVOKE_SESSION_REQ = MsgID._(1016, _omitEnumNames ? '' : 'REVOKE_SESSION_REQ');
  static const MsgID REVOKE_SESSION_RESP = MsgID._(1017, _omitEnumNames ? '' : 'REVOKE_SESSION_RESP');
  static const MsgID CHANGE_PASSWORD_REQ = MsgID._(1018, _omitEnumNames ? '' : 'CHANGE_PASSWORD_REQ');
  static const MsgID CHANGE_PASSWORD_RESP = MsgID._(1019, _omitEnumNames ? '' : 'CHANGE_PASSWORD_RESP');
  static const MsgID SEND_MESSAGE_REQ = MsgID._(2001, _omitEnumNames ? '' : 'SEND_MESSAGE_REQ');
  static const MsgID SEND_MESSAGE_RESP = MsgID._(2002, _omitEnumNames ? '' : 'SEND_MESSAGE_RESP');
  static const MsgID GET_HISTORY_REQ = MsgID._(2003, _omitEnumNames ? '' : 'GET_HISTORY_REQ');
  static const MsgID GET_HISTORY_RESP = MsgID._(2004, _omitEnumNames ? '' : 'GET_HISTORY_RESP');
  static const MsgID CHAT_MESSAGE_NOTIFY = MsgID._(2005, _omitEnumNames ? '' : 'CHAT_MESSAGE_NOTIFY');
  static const MsgID CREATE_GROUP_REQ = MsgID._(2101, _omitEnumNames ? '' : 'CREATE_GROUP_REQ');
  static const MsgID CREATE_GROUP_RESP = MsgID._(2102, _omitEnumNames ? '' : 'CREATE_GROUP_RESP');
  static const MsgID JOIN_GROUP_REQ = MsgID._(2103, _omitEnumNames ? '' : 'JOIN_GROUP_REQ');
  static const MsgID JOIN_GROUP_RESP = MsgID._(2104, _omitEnumNames ? '' : 'JOIN_GROUP_RESP');
  static const MsgID LEAVE_GROUP_REQ = MsgID._(2105, _omitEnumNames ? '' : 'LEAVE_GROUP_REQ');
  static const MsgID LEAVE_GROUP_RESP = MsgID._(2106, _omitEnumNames ? '' : 'LEAVE_GROUP_RESP');
  static const MsgID KICK_MEMBER_REQ = MsgID._(2107, _omitEnumNames ? '' : 'KICK_MEMBER_REQ');
  static const MsgID KICK_MEMBER_RESP = MsgID._(2108, _omitEnumNames ? '' : 'KICK_MEMBER_RESP');
  static const MsgID GET_GROUP_INFO_REQ = MsgID._(2109, _omitEnumNames ? '' : 'GET_GROUP_INFO_REQ');
  static const MsgID GET_GROUP_INFO_RESP = MsgID._(2110, _omitEnumNames ? '' : 'GET_GROUP_INFO_RESP');
  static const MsgID GET_GROUP_MEMBERS_REQ = MsgID._(2111, _omitEnumNames ? '' : 'GET_GROUP_MEMBERS_REQ');
  static const MsgID GET_GROUP_MEMBERS_RESP = MsgID._(2112, _omitEnumNames ? '' : 'GET_GROUP_MEMBERS_RESP');
  static const MsgID GET_USER_GROUPS_REQ = MsgID._(2113, _omitEnumNames ? '' : 'GET_USER_GROUPS_REQ');
  static const MsgID GET_USER_GROUPS_RESP = MsgID._(2114, _omitEnumNames ? '' : 'GET_USER_GROUPS_RESP');
  static const MsgID INVITE_TO_GROUP_REQ = MsgID._(2115, _omitEnumNames ? '' : 'INVITE_TO_GROUP_REQ');
  static const MsgID INVITE_TO_GROUP_RESP = MsgID._(2116, _omitEnumNames ? '' : 'INVITE_TO_GROUP_RESP');
  static const MsgID GROUP_CREATED_NOTIFY = MsgID._(2117, _omitEnumNames ? '' : 'GROUP_CREATED_NOTIFY');
  static const MsgID GROUP_MEMBER_JOINED_NOTIFY = MsgID._(2118, _omitEnumNames ? '' : 'GROUP_MEMBER_JOINED_NOTIFY');
  static const MsgID GROUP_MEMBER_LEFT_NOTIFY = MsgID._(2119, _omitEnumNames ? '' : 'GROUP_MEMBER_LEFT_NOTIFY');
  static const MsgID GROUP_MEMBER_KICKED_NOTIFY = MsgID._(2120, _omitEnumNames ? '' : 'GROUP_MEMBER_KICKED_NOTIFY');
  static const MsgID GROUP_UPDATED_NOTIFY = MsgID._(2121, _omitEnumNames ? '' : 'GROUP_UPDATED_NOTIFY');
  static const MsgID MARK_READ_REQ = MsgID._(2201, _omitEnumNames ? '' : 'MARK_READ_REQ');
  static const MsgID MARK_READ_RESP = MsgID._(2202, _omitEnumNames ? '' : 'MARK_READ_RESP');
  static const MsgID GET_READ_RECEIPTS_REQ = MsgID._(2203, _omitEnumNames ? '' : 'GET_READ_RECEIPTS_REQ');
  static const MsgID GET_READ_RECEIPTS_RESP = MsgID._(2204, _omitEnumNames ? '' : 'GET_READ_RECEIPTS_RESP');
  static const MsgID GET_UNREAD_COUNT_REQ = MsgID._(2205, _omitEnumNames ? '' : 'GET_UNREAD_COUNT_REQ');
  static const MsgID GET_UNREAD_COUNT_RESP = MsgID._(2206, _omitEnumNames ? '' : 'GET_UNREAD_COUNT_RESP');
  static const MsgID MESSAGE_READ_NOTIFY = MsgID._(2207, _omitEnumNames ? '' : 'MESSAGE_READ_NOTIFY');
  static const MsgID TYPING_INDICATOR_NOTIFY = MsgID._(2208, _omitEnumNames ? '' : 'TYPING_INDICATOR_NOTIFY');
  static const MsgID MESSAGE_ACK = MsgID._(2209, _omitEnumNames ? '' : 'MESSAGE_ACK');
  static const MsgID MESSAGE_NACK = MsgID._(2210, _omitEnumNames ? '' : 'MESSAGE_NACK');
  static const MsgID TRACK_MESSAGE_REQ = MsgID._(2211, _omitEnumNames ? '' : 'TRACK_MESSAGE_REQ');
  static const MsgID TRACK_MESSAGE_RESP = MsgID._(2212, _omitEnumNames ? '' : 'TRACK_MESSAGE_RESP');
  static const MsgID GET_HISTORY_V2_REQ = MsgID._(2213, _omitEnumNames ? '' : 'GET_HISTORY_V2_REQ');
  static const MsgID GET_HISTORY_V2_RESP = MsgID._(2214, _omitEnumNames ? '' : 'GET_HISTORY_V2_RESP');
  static const MsgID ADD_REACTION_REQ = MsgID._(2215, _omitEnumNames ? '' : 'ADD_REACTION_REQ');
  static const MsgID ADD_REACTION_RESP = MsgID._(2216, _omitEnumNames ? '' : 'ADD_REACTION_RESP');
  static const MsgID REMOVE_REACTION_REQ = MsgID._(2217, _omitEnumNames ? '' : 'REMOVE_REACTION_REQ');
  static const MsgID REMOVE_REACTION_RESP = MsgID._(2218, _omitEnumNames ? '' : 'REMOVE_REACTION_RESP');
  static const MsgID GET_REACTIONS_REQ = MsgID._(2219, _omitEnumNames ? '' : 'GET_REACTIONS_REQ');
  static const MsgID GET_REACTIONS_RESP = MsgID._(2220, _omitEnumNames ? '' : 'GET_REACTIONS_RESP');
  static const MsgID REACTION_ADDED_NOTIFY = MsgID._(2221, _omitEnumNames ? '' : 'REACTION_ADDED_NOTIFY');
  static const MsgID REACTION_REMOVED_NOTIFY = MsgID._(2222, _omitEnumNames ? '' : 'REACTION_REMOVED_NOTIFY');
  static const MsgID GET_TYPING_USERS_REQ = MsgID._(2223, _omitEnumNames ? '' : 'GET_TYPING_USERS_REQ');
  static const MsgID GET_TYPING_USERS_RESP = MsgID._(2224, _omitEnumNames ? '' : 'GET_TYPING_USERS_RESP');
  static const MsgID EDIT_MESSAGE_REQ = MsgID._(2225, _omitEnumNames ? '' : 'EDIT_MESSAGE_REQ');
  static const MsgID EDIT_MESSAGE_RESP = MsgID._(2226, _omitEnumNames ? '' : 'EDIT_MESSAGE_RESP');
  static const MsgID DELETE_MESSAGE_REQ = MsgID._(2227, _omitEnumNames ? '' : 'DELETE_MESSAGE_REQ');
  static const MsgID DELETE_MESSAGE_RESP = MsgID._(2228, _omitEnumNames ? '' : 'DELETE_MESSAGE_RESP');
  static const MsgID BULK_DELETE_REQ = MsgID._(2229, _omitEnumNames ? '' : 'BULK_DELETE_REQ');
  static const MsgID BULK_DELETE_RESP = MsgID._(2230, _omitEnumNames ? '' : 'BULK_DELETE_RESP');
  static const MsgID MESSAGE_EDITED_NOTIFY = MsgID._(2231, _omitEnumNames ? '' : 'MESSAGE_EDITED_NOTIFY');
  static const MsgID MESSAGE_DELETED_NOTIFY = MsgID._(2232, _omitEnumNames ? '' : 'MESSAGE_DELETED_NOTIFY');
  static const MsgID GET_MENTION_SUGGESTIONS_REQ = MsgID._(2233, _omitEnumNames ? '' : 'GET_MENTION_SUGGESTIONS_REQ');
  static const MsgID GET_MENTION_SUGGESTIONS_RESP = MsgID._(2234, _omitEnumNames ? '' : 'GET_MENTION_SUGGESTIONS_RESP');
  static const MsgID ADD_FRIEND_REQ = MsgID._(3001, _omitEnumNames ? '' : 'ADD_FRIEND_REQ');
  static const MsgID ADD_FRIEND_RESP = MsgID._(3002, _omitEnumNames ? '' : 'ADD_FRIEND_RESP');
  static const MsgID FRIEND_REQUEST_ACTION_REQ = MsgID._(3003, _omitEnumNames ? '' : 'FRIEND_REQUEST_ACTION_REQ');
  static const MsgID FRIEND_REQUEST_ACTION_RESP = MsgID._(3004, _omitEnumNames ? '' : 'FRIEND_REQUEST_ACTION_RESP');
  static const MsgID REMOVE_FRIEND_REQ = MsgID._(3005, _omitEnumNames ? '' : 'REMOVE_FRIEND_REQ');
  static const MsgID REMOVE_FRIEND_RESP = MsgID._(3006, _omitEnumNames ? '' : 'REMOVE_FRIEND_RESP');
  static const MsgID GET_FRIEND_LIST_REQ = MsgID._(3007, _omitEnumNames ? '' : 'GET_FRIEND_LIST_REQ');
  static const MsgID GET_FRIEND_LIST_RESP = MsgID._(3008, _omitEnumNames ? '' : 'GET_FRIEND_LIST_RESP');
  static const MsgID GET_PENDING_REQUESTS_REQ = MsgID._(3009, _omitEnumNames ? '' : 'GET_PENDING_REQUESTS_REQ');
  static const MsgID GET_PENDING_REQUESTS_RESP = MsgID._(3010, _omitEnumNames ? '' : 'GET_PENDING_REQUESTS_RESP');
  static const MsgID BLOCK_USER_REQ = MsgID._(3011, _omitEnumNames ? '' : 'BLOCK_USER_REQ');
  static const MsgID BLOCK_USER_RESP = MsgID._(3012, _omitEnumNames ? '' : 'BLOCK_USER_RESP');
  static const MsgID UNBLOCK_USER_REQ = MsgID._(3013, _omitEnumNames ? '' : 'UNBLOCK_USER_REQ');
  static const MsgID UNBLOCK_USER_RESP = MsgID._(3014, _omitEnumNames ? '' : 'UNBLOCK_USER_RESP');
  static const MsgID GET_BLOCKED_LIST_REQ = MsgID._(3015, _omitEnumNames ? '' : 'GET_BLOCKED_LIST_REQ');
  static const MsgID GET_BLOCKED_LIST_RESP = MsgID._(3016, _omitEnumNames ? '' : 'GET_BLOCKED_LIST_RESP');
  static const MsgID SET_PRESENCE_REQ = MsgID._(3017, _omitEnumNames ? '' : 'SET_PRESENCE_REQ');
  static const MsgID SET_PRESENCE_RESP = MsgID._(3018, _omitEnumNames ? '' : 'SET_PRESENCE_RESP');
  static const MsgID GET_PRESENCE_REQ = MsgID._(3019, _omitEnumNames ? '' : 'GET_PRESENCE_REQ');
  static const MsgID GET_PRESENCE_RESP = MsgID._(3020, _omitEnumNames ? '' : 'GET_PRESENCE_RESP');
  static const MsgID PRESENCE_NOTIFY = MsgID._(3021, _omitEnumNames ? '' : 'PRESENCE_NOTIFY');
  static const MsgID FRIEND_REQUEST_NOTIFY = MsgID._(3022, _omitEnumNames ? '' : 'FRIEND_REQUEST_NOTIFY');
  static const MsgID FRIEND_ACCEPTED_NOTIFY = MsgID._(3023, _omitEnumNames ? '' : 'FRIEND_ACCEPTED_NOTIFY');
  static const MsgID FRIEND_REMOVED_NOTIFY = MsgID._(3024, _omitEnumNames ? '' : 'FRIEND_REMOVED_NOTIFY');
  static const MsgID CREATE_ROOM_REQ = MsgID._(4001, _omitEnumNames ? '' : 'CREATE_ROOM_REQ');
  static const MsgID CREATE_ROOM_RESP = MsgID._(4002, _omitEnumNames ? '' : 'CREATE_ROOM_RESP');
  static const MsgID JOIN_ROOM_REQ = MsgID._(4003, _omitEnumNames ? '' : 'JOIN_ROOM_REQ');
  static const MsgID JOIN_ROOM_RESP = MsgID._(4004, _omitEnumNames ? '' : 'JOIN_ROOM_RESP');
  static const MsgID LEAVE_ROOM_REQ = MsgID._(4005, _omitEnumNames ? '' : 'LEAVE_ROOM_REQ');
  static const MsgID LEAVE_ROOM_RESP = MsgID._(4006, _omitEnumNames ? '' : 'LEAVE_ROOM_RESP');
  static const MsgID ICE_CANDIDATE_MSG = MsgID._(4007, _omitEnumNames ? '' : 'ICE_CANDIDATE_MSG');
  static const MsgID SDP_OFFER_MSG = MsgID._(4008, _omitEnumNames ? '' : 'SDP_OFFER_MSG');
  static const MsgID SDP_ANSWER_MSG = MsgID._(4009, _omitEnumNames ? '' : 'SDP_ANSWER_MSG');
  static const MsgID GET_ROOM_INFO_REQ = MsgID._(4010, _omitEnumNames ? '' : 'GET_ROOM_INFO_REQ');
  static const MsgID GET_ROOM_INFO_RESP = MsgID._(4011, _omitEnumNames ? '' : 'GET_ROOM_INFO_RESP');
  static const MsgID GET_USER_ROOM_REQ = MsgID._(4012, _omitEnumNames ? '' : 'GET_USER_ROOM_REQ');
  static const MsgID GET_USER_ROOM_RESP = MsgID._(4013, _omitEnumNames ? '' : 'GET_USER_ROOM_RESP');
  static const MsgID SET_MUTE_REQ = MsgID._(4014, _omitEnumNames ? '' : 'SET_MUTE_REQ');
  static const MsgID SET_MUTE_RESP = MsgID._(4015, _omitEnumNames ? '' : 'SET_MUTE_RESP');
  static const MsgID SET_DEAFEN_REQ = MsgID._(4016, _omitEnumNames ? '' : 'SET_DEAFEN_REQ');
  static const MsgID SET_DEAFEN_RESP = MsgID._(4017, _omitEnumNames ? '' : 'SET_DEAFEN_RESP');
  static const MsgID PARTICIPANT_JOINED_NOTIFY = MsgID._(4018, _omitEnumNames ? '' : 'PARTICIPANT_JOINED_NOTIFY');
  static const MsgID PARTICIPANT_LEFT_NOTIFY = MsgID._(4019, _omitEnumNames ? '' : 'PARTICIPANT_LEFT_NOTIFY');
  static const MsgID PARTICIPANT_STATE_CHANGED_NOTIFY = MsgID._(4020, _omitEnumNames ? '' : 'PARTICIPANT_STATE_CHANGED_NOTIFY');
  static const MsgID SPEAKING_NOTIFY = MsgID._(4021, _omitEnumNames ? '' : 'SPEAKING_NOTIFY');
  static const MsgID SERVER_AUTH_REQ = MsgID._(5001, _omitEnumNames ? '' : 'SERVER_AUTH_REQ');
  static const MsgID SERVER_AUTH_RESP = MsgID._(5002, _omitEnumNames ? '' : 'SERVER_AUTH_RESP');
  static const MsgID SERVER_HEARTBEAT_PING = MsgID._(5003, _omitEnumNames ? '' : 'SERVER_HEARTBEAT_PING');
  static const MsgID SERVER_HEARTBEAT_PONG = MsgID._(5004, _omitEnumNames ? '' : 'SERVER_HEARTBEAT_PONG');
  static const MsgID INJECT_MESSAGE_REQ = MsgID._(5005, _omitEnumNames ? '' : 'INJECT_MESSAGE_REQ');
  static const MsgID INJECT_MESSAGE_RESP = MsgID._(5006, _omitEnumNames ? '' : 'INJECT_MESSAGE_RESP');
  static const MsgID INJECT_MESSAGE_NOTIFY = MsgID._(5007, _omitEnumNames ? '' : 'INJECT_MESSAGE_NOTIFY');
  static const MsgID EVENT_PUBLISH_REQ = MsgID._(5008, _omitEnumNames ? '' : 'EVENT_PUBLISH_REQ');
  static const MsgID EVENT_PUBLISH_RESP = MsgID._(5009, _omitEnumNames ? '' : 'EVENT_PUBLISH_RESP');
  static const MsgID EVENT_DELIVER_NOTIFY = MsgID._(5010, _omitEnumNames ? '' : 'EVENT_DELIVER_NOTIFY');
  static const MsgID EVENT_ACK_REQ = MsgID._(5011, _omitEnumNames ? '' : 'EVENT_ACK_REQ');
  static const MsgID EVENT_ACK_RESP = MsgID._(5012, _omitEnumNames ? '' : 'EVENT_ACK_RESP');
  static const MsgID REGISTER_DEVICE_REQ = MsgID._(6001, _omitEnumNames ? '' : 'REGISTER_DEVICE_REQ');
  static const MsgID REGISTER_DEVICE_RESP = MsgID._(6002, _omitEnumNames ? '' : 'REGISTER_DEVICE_RESP');
  static const MsgID UNREGISTER_DEVICE_REQ = MsgID._(6003, _omitEnumNames ? '' : 'UNREGISTER_DEVICE_REQ');
  static const MsgID UNREGISTER_DEVICE_RESP = MsgID._(6004, _omitEnumNames ? '' : 'UNREGISTER_DEVICE_RESP');
  static const MsgID UPDATE_DEVICE_TOKEN_REQ = MsgID._(6005, _omitEnumNames ? '' : 'UPDATE_DEVICE_TOKEN_REQ');
  static const MsgID UPDATE_DEVICE_TOKEN_RESP = MsgID._(6006, _omitEnumNames ? '' : 'UPDATE_DEVICE_TOKEN_RESP');
  static const MsgID GET_USER_DEVICES_REQ = MsgID._(6007, _omitEnumNames ? '' : 'GET_USER_DEVICES_REQ');
  static const MsgID GET_USER_DEVICES_RESP = MsgID._(6008, _omitEnumNames ? '' : 'GET_USER_DEVICES_RESP');
  static const MsgID PUSH_NOTIFICATION_REQ = MsgID._(6009, _omitEnumNames ? '' : 'PUSH_NOTIFICATION_REQ');
  static const MsgID PUSH_NOTIFICATION_RESP = MsgID._(6010, _omitEnumNames ? '' : 'PUSH_NOTIFICATION_RESP');
  static const MsgID CREATE_PARTY_REQ = MsgID._(7001, _omitEnumNames ? '' : 'CREATE_PARTY_REQ');
  static const MsgID CREATE_PARTY_RESP = MsgID._(7002, _omitEnumNames ? '' : 'CREATE_PARTY_RESP');
  static const MsgID DISBAND_PARTY_REQ = MsgID._(7003, _omitEnumNames ? '' : 'DISBAND_PARTY_REQ');
  static const MsgID DISBAND_PARTY_RESP = MsgID._(7004, _omitEnumNames ? '' : 'DISBAND_PARTY_RESP');
  static const MsgID INVITE_TO_PARTY_REQ = MsgID._(7005, _omitEnumNames ? '' : 'INVITE_TO_PARTY_REQ');
  static const MsgID INVITE_TO_PARTY_RESP = MsgID._(7006, _omitEnumNames ? '' : 'INVITE_TO_PARTY_RESP');
  static const MsgID INVITE_NOTIFY = MsgID._(7007, _omitEnumNames ? '' : 'INVITE_NOTIFY');
  static const MsgID ACCEPT_INVITE_REQ = MsgID._(7008, _omitEnumNames ? '' : 'ACCEPT_INVITE_REQ');
  static const MsgID ACCEPT_INVITE_RESP = MsgID._(7009, _omitEnumNames ? '' : 'ACCEPT_INVITE_RESP');
  static const MsgID DECLINE_INVITE_REQ = MsgID._(7010, _omitEnumNames ? '' : 'DECLINE_INVITE_REQ');
  static const MsgID DECLINE_INVITE_RESP = MsgID._(7011, _omitEnumNames ? '' : 'DECLINE_INVITE_RESP');
  static const MsgID INVITE_RESULT_NOTIFY = MsgID._(7012, _omitEnumNames ? '' : 'INVITE_RESULT_NOTIFY');
  static const MsgID LEAVE_PARTY_REQ = MsgID._(7013, _omitEnumNames ? '' : 'LEAVE_PARTY_REQ');
  static const MsgID LEAVE_PARTY_RESP = MsgID._(7014, _omitEnumNames ? '' : 'LEAVE_PARTY_RESP');
  static const MsgID KICK_PARTY_MEMBER_REQ = MsgID._(7015, _omitEnumNames ? '' : 'KICK_PARTY_MEMBER_REQ');
  static const MsgID KICK_PARTY_MEMBER_RESP = MsgID._(7016, _omitEnumNames ? '' : 'KICK_PARTY_MEMBER_RESP');
  static const MsgID TRANSFER_LEADER_REQ = MsgID._(7017, _omitEnumNames ? '' : 'TRANSFER_LEADER_REQ');
  static const MsgID TRANSFER_LEADER_RESP = MsgID._(7018, _omitEnumNames ? '' : 'TRANSFER_LEADER_RESP');
  static const MsgID SET_READY_REQ = MsgID._(7019, _omitEnumNames ? '' : 'SET_READY_REQ');
  static const MsgID SET_READY_RESP = MsgID._(7020, _omitEnumNames ? '' : 'SET_READY_RESP');
  static const MsgID PARTY_JOINED_NOTIFY = MsgID._(7021, _omitEnumNames ? '' : 'PARTY_JOINED_NOTIFY');
  static const MsgID PARTY_LEFT_NOTIFY = MsgID._(7022, _omitEnumNames ? '' : 'PARTY_LEFT_NOTIFY');
  static const MsgID PARTY_KICKED_NOTIFY = MsgID._(7023, _omitEnumNames ? '' : 'PARTY_KICKED_NOTIFY');
  static const MsgID PARTY_STATE_CHANGED_NOTIFY = MsgID._(7024, _omitEnumNames ? '' : 'PARTY_STATE_CHANGED_NOTIFY');
  static const MsgID PARTY_DISBANDED_NOTIFY = MsgID._(7025, _omitEnumNames ? '' : 'PARTY_DISBANDED_NOTIFY');
  static const MsgID GET_MY_PARTY_REQ = MsgID._(7026, _omitEnumNames ? '' : 'GET_MY_PARTY_REQ');
  static const MsgID GET_MY_PARTY_RESP = MsgID._(7027, _omitEnumNames ? '' : 'GET_MY_PARTY_RESP');

  static const $core.List<MsgID> values = <MsgID> [
    UNKNOWN,
    HEARTBEAT_PING,
    HEARTBEAT_PONG,
    LOGIN_REQ,
    LOGIN_RESP,
    KICK_NOTIFY,
    LOGOUT_REQ,
    LOGOUT_RESP,
    REGISTER_REQ,
    REGISTER_RESP,
    PASSWORD_LOGIN_REQ,
    PASSWORD_LOGIN_RESP,
    REFRESH_TOKEN_REQ,
    REFRESH_TOKEN_RESP,
    GET_SESSIONS_REQ,
    GET_SESSIONS_RESP,
    REVOKE_SESSION_REQ,
    REVOKE_SESSION_RESP,
    CHANGE_PASSWORD_REQ,
    CHANGE_PASSWORD_RESP,
    SEND_MESSAGE_REQ,
    SEND_MESSAGE_RESP,
    GET_HISTORY_REQ,
    GET_HISTORY_RESP,
    CHAT_MESSAGE_NOTIFY,
    CREATE_GROUP_REQ,
    CREATE_GROUP_RESP,
    JOIN_GROUP_REQ,
    JOIN_GROUP_RESP,
    LEAVE_GROUP_REQ,
    LEAVE_GROUP_RESP,
    KICK_MEMBER_REQ,
    KICK_MEMBER_RESP,
    GET_GROUP_INFO_REQ,
    GET_GROUP_INFO_RESP,
    GET_GROUP_MEMBERS_REQ,
    GET_GROUP_MEMBERS_RESP,
    GET_USER_GROUPS_REQ,
    GET_USER_GROUPS_RESP,
    INVITE_TO_GROUP_REQ,
    INVITE_TO_GROUP_RESP,
    GROUP_CREATED_NOTIFY,
    GROUP_MEMBER_JOINED_NOTIFY,
    GROUP_MEMBER_LEFT_NOTIFY,
    GROUP_MEMBER_KICKED_NOTIFY,
    GROUP_UPDATED_NOTIFY,
    MARK_READ_REQ,
    MARK_READ_RESP,
    GET_READ_RECEIPTS_REQ,
    GET_READ_RECEIPTS_RESP,
    GET_UNREAD_COUNT_REQ,
    GET_UNREAD_COUNT_RESP,
    MESSAGE_READ_NOTIFY,
    TYPING_INDICATOR_NOTIFY,
    MESSAGE_ACK,
    MESSAGE_NACK,
    TRACK_MESSAGE_REQ,
    TRACK_MESSAGE_RESP,
    GET_HISTORY_V2_REQ,
    GET_HISTORY_V2_RESP,
    ADD_REACTION_REQ,
    ADD_REACTION_RESP,
    REMOVE_REACTION_REQ,
    REMOVE_REACTION_RESP,
    GET_REACTIONS_REQ,
    GET_REACTIONS_RESP,
    REACTION_ADDED_NOTIFY,
    REACTION_REMOVED_NOTIFY,
    GET_TYPING_USERS_REQ,
    GET_TYPING_USERS_RESP,
    EDIT_MESSAGE_REQ,
    EDIT_MESSAGE_RESP,
    DELETE_MESSAGE_REQ,
    DELETE_MESSAGE_RESP,
    BULK_DELETE_REQ,
    BULK_DELETE_RESP,
    MESSAGE_EDITED_NOTIFY,
    MESSAGE_DELETED_NOTIFY,
    GET_MENTION_SUGGESTIONS_REQ,
    GET_MENTION_SUGGESTIONS_RESP,
    ADD_FRIEND_REQ,
    ADD_FRIEND_RESP,
    FRIEND_REQUEST_ACTION_REQ,
    FRIEND_REQUEST_ACTION_RESP,
    REMOVE_FRIEND_REQ,
    REMOVE_FRIEND_RESP,
    GET_FRIEND_LIST_REQ,
    GET_FRIEND_LIST_RESP,
    GET_PENDING_REQUESTS_REQ,
    GET_PENDING_REQUESTS_RESP,
    BLOCK_USER_REQ,
    BLOCK_USER_RESP,
    UNBLOCK_USER_REQ,
    UNBLOCK_USER_RESP,
    GET_BLOCKED_LIST_REQ,
    GET_BLOCKED_LIST_RESP,
    SET_PRESENCE_REQ,
    SET_PRESENCE_RESP,
    GET_PRESENCE_REQ,
    GET_PRESENCE_RESP,
    PRESENCE_NOTIFY,
    FRIEND_REQUEST_NOTIFY,
    FRIEND_ACCEPTED_NOTIFY,
    FRIEND_REMOVED_NOTIFY,
    CREATE_ROOM_REQ,
    CREATE_ROOM_RESP,
    JOIN_ROOM_REQ,
    JOIN_ROOM_RESP,
    LEAVE_ROOM_REQ,
    LEAVE_ROOM_RESP,
    ICE_CANDIDATE_MSG,
    SDP_OFFER_MSG,
    SDP_ANSWER_MSG,
    GET_ROOM_INFO_REQ,
    GET_ROOM_INFO_RESP,
    GET_USER_ROOM_REQ,
    GET_USER_ROOM_RESP,
    SET_MUTE_REQ,
    SET_MUTE_RESP,
    SET_DEAFEN_REQ,
    SET_DEAFEN_RESP,
    PARTICIPANT_JOINED_NOTIFY,
    PARTICIPANT_LEFT_NOTIFY,
    PARTICIPANT_STATE_CHANGED_NOTIFY,
    SPEAKING_NOTIFY,
    SERVER_AUTH_REQ,
    SERVER_AUTH_RESP,
    SERVER_HEARTBEAT_PING,
    SERVER_HEARTBEAT_PONG,
    INJECT_MESSAGE_REQ,
    INJECT_MESSAGE_RESP,
    INJECT_MESSAGE_NOTIFY,
    EVENT_PUBLISH_REQ,
    EVENT_PUBLISH_RESP,
    EVENT_DELIVER_NOTIFY,
    EVENT_ACK_REQ,
    EVENT_ACK_RESP,
    REGISTER_DEVICE_REQ,
    REGISTER_DEVICE_RESP,
    UNREGISTER_DEVICE_REQ,
    UNREGISTER_DEVICE_RESP,
    UPDATE_DEVICE_TOKEN_REQ,
    UPDATE_DEVICE_TOKEN_RESP,
    GET_USER_DEVICES_REQ,
    GET_USER_DEVICES_RESP,
    PUSH_NOTIFICATION_REQ,
    PUSH_NOTIFICATION_RESP,
    CREATE_PARTY_REQ,
    CREATE_PARTY_RESP,
    DISBAND_PARTY_REQ,
    DISBAND_PARTY_RESP,
    INVITE_TO_PARTY_REQ,
    INVITE_TO_PARTY_RESP,
    INVITE_NOTIFY,
    ACCEPT_INVITE_REQ,
    ACCEPT_INVITE_RESP,
    DECLINE_INVITE_REQ,
    DECLINE_INVITE_RESP,
    INVITE_RESULT_NOTIFY,
    LEAVE_PARTY_REQ,
    LEAVE_PARTY_RESP,
    KICK_PARTY_MEMBER_REQ,
    KICK_PARTY_MEMBER_RESP,
    TRANSFER_LEADER_REQ,
    TRANSFER_LEADER_RESP,
    SET_READY_REQ,
    SET_READY_RESP,
    PARTY_JOINED_NOTIFY,
    PARTY_LEFT_NOTIFY,
    PARTY_KICKED_NOTIFY,
    PARTY_STATE_CHANGED_NOTIFY,
    PARTY_DISBANDED_NOTIFY,
    GET_MY_PARTY_REQ,
    GET_MY_PARTY_RESP,
  ];

  static final $core.Map<$core.int, MsgID> _byValue = $pb.ProtobufEnum.initByValue(values);
  static MsgID? valueOf($core.int value) => _byValue[value];

  const MsgID._($core.int v, $core.String n) : super(v, n);
}


const _omitEnumNames = $core.bool.fromEnvironment('protobuf.omit_enum_names');
