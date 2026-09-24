//
//  Generated code. Do not modify.
//  source: proto/chat.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use msgTypeDescriptor instead')
const MsgType$json = {
  '1': 'MsgType',
  '2': [
    {'1': 'TEXT', '2': 0},
    {'1': 'EMOJI', '2': 1},
    {'1': 'VOICE', '2': 2},
    {'1': 'IMAGE', '2': 3},
    {'1': 'ITEM_LINK', '2': 10},
    {'1': 'SKILL_LINK', '2': 11},
    {'1': 'ACHIEVEMENT', '2': 12},
    {'1': 'NPC_DIALOG', '2': 13},
    {'1': 'TRADE_STATUS', '2': 14},
    {'1': 'SYSTEM', '2': 99},
  ],
};

/// Descriptor for `MsgType`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List msgTypeDescriptor = $convert.base64Decode(
    'CgdNc2dUeXBlEggKBFRFWFQQABIJCgVFTU9KSRABEgkKBVZPSUNFEAISCQoFSU1BR0UQAxINCg'
    'lJVEVNX0xJTksQChIOCgpTS0lMTF9MSU5LEAsSDwoLQUNISUVWRU1FTlQQDBIOCgpOUENfRElB'
    'TE9HEA0SEAoMVFJBREVfU1RBVFVTEA4SCgoGU1lTVEVNEGM=');

@$core.Deprecated('Use channelTypeDescriptor instead')
const ChannelType$json = {
  '1': 'ChannelType',
  '2': [
    {'1': 'PRIVATE', '2': 0},
    {'1': 'TEAM', '2': 1},
    {'1': 'GUILD', '2': 2},
    {'1': 'WORLD', '2': 3},
    {'1': 'SYSTEM_CHANNEL', '2': 4},
    {'1': 'MARQUEE', '2': 5},
  ],
};

/// Descriptor for `ChannelType`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List channelTypeDescriptor = $convert.base64Decode(
    'CgtDaGFubmVsVHlwZRILCgdQUklWQVRFEAASCAoEVEVBTRABEgkKBUdVSUxEEAISCQoFV09STE'
    'QQAxISCg5TWVNURU1fQ0hBTk5FTBAEEgsKB01BUlFVRUUQBQ==');

@$core.Deprecated('Use priorityDescriptor instead')
const Priority$json = {
  '1': 'Priority',
  '2': [
    {'1': 'PRIORITY_LOW', '2': 0},
    {'1': 'PRIORITY_NORMAL', '2': 1},
    {'1': 'PRIORITY_HIGH', '2': 2},
    {'1': 'PRIORITY_URGENT', '2': 3},
  ],
};

/// Descriptor for `Priority`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List priorityDescriptor = $convert.base64Decode(
    'CghQcmlvcml0eRIQCgxQUklPUklUWV9MT1cQABITCg9QUklPUklUWV9OT1JNQUwQARIRCg1QUk'
    'lPUklUWV9ISUdIEAISEwoPUFJJT1JJVFlfVVJHRU5UEAM=');

@$core.Deprecated('Use senderKindDescriptor instead')
const SenderKind$json = {
  '1': 'SenderKind',
  '2': [
    {'1': 'SENDER_USER', '2': 0},
    {'1': 'SENDER_SYSTEM', '2': 1},
    {'1': 'SENDER_NPC', '2': 2},
    {'1': 'SENDER_SERVICE', '2': 3},
  ],
};

/// Descriptor for `SenderKind`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List senderKindDescriptor = $convert.base64Decode(
    'CgpTZW5kZXJLaW5kEg8KC1NFTkRFUl9VU0VSEAASEQoNU0VOREVSX1NZU1RFTRABEg4KClNFTk'
    'RFUl9OUEMQAhISCg5TRU5ERVJfU0VSVklDRRAD');

@$core.Deprecated('Use groupMemberRoleDescriptor instead')
const GroupMemberRole$json = {
  '1': 'GroupMemberRole',
  '2': [
    {'1': 'MEMBER', '2': 0},
    {'1': 'MODERATOR', '2': 1},
    {'1': 'ADMIN', '2': 2},
    {'1': 'OWNER', '2': 3},
  ],
};

/// Descriptor for `GroupMemberRole`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List groupMemberRoleDescriptor = $convert.base64Decode(
    'Cg9Hcm91cE1lbWJlclJvbGUSCgoGTUVNQkVSEAASDQoJTU9ERVJBVE9SEAESCQoFQURNSU4QAh'
    'IJCgVPV05FUhAD');

@$core.Deprecated('Use channelKindDescriptor instead')
const ChannelKind$json = {
  '1': 'ChannelKind',
  '2': [
    {'1': 'CHANNEL_KIND_TEXT', '2': 0},
    {'1': 'CHANNEL_KIND_VOICE', '2': 1},
    {'1': 'CHANNEL_KIND_ANNOUNCEMENT', '2': 2},
    {'1': 'CHANNEL_KIND_STAGE', '2': 3},
    {'1': 'CHANNEL_KIND_FORUM', '2': 4},
  ],
};

/// Descriptor for `ChannelKind`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List channelKindDescriptor = $convert.base64Decode(
    'CgtDaGFubmVsS2luZBIVChFDSEFOTkVMX0tJTkRfVEVYVBAAEhYKEkNIQU5ORUxfS0lORF9WT0'
    'lDRRABEh0KGUNIQU5ORUxfS0lORF9BTk5PVU5DRU1FTlQQAhIWChJDSEFOTkVMX0tJTkRfU1RB'
    'R0UQAxIWChJDSEFOTkVMX0tJTkRfRk9SVU0QBA==');

@$core.Deprecated('Use permissionTypeDescriptor instead')
const PermissionType$json = {
  '1': 'PermissionType',
  '2': [
    {'1': 'PERMISSION_TYPE_ROLE', '2': 0},
    {'1': 'PERMISSION_TYPE_USER', '2': 1},
  ],
};

/// Descriptor for `PermissionType`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List permissionTypeDescriptor = $convert.base64Decode(
    'Cg5QZXJtaXNzaW9uVHlwZRIYChRQRVJNSVNTSU9OX1RZUEVfUk9MRRAAEhgKFFBFUk1JU1NJT0'
    '5fVFlQRV9VU0VSEAE=');

@$core.Deprecated('Use permissionOverrideDescriptor instead')
const PermissionOverride$json = {
  '1': 'PermissionOverride',
  '2': [
    {'1': 'INHERIT', '2': 0},
    {'1': 'ALLOW', '2': 1},
    {'1': 'DENY', '2': 2},
  ],
};

/// Descriptor for `PermissionOverride`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List permissionOverrideDescriptor = $convert.base64Decode(
    'ChJQZXJtaXNzaW9uT3ZlcnJpZGUSCwoHSU5IRVJJVBAAEgkKBUFMTE9XEAESCAoEREVOWRAC');

@$core.Deprecated('Use mentionTypeDescriptor instead')
const MentionType$json = {
  '1': 'MentionType',
  '2': [
    {'1': 'MENTION_TYPE_USER', '2': 0},
    {'1': 'MENTION_TYPE_ROLE', '2': 1},
    {'1': 'MENTION_TYPE_CHANNEL', '2': 2},
    {'1': 'MENTION_TYPE_EVERYONE', '2': 3},
    {'1': 'MENTION_TYPE_HERE', '2': 4},
  ],
};

/// Descriptor for `MentionType`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List mentionTypeDescriptor = $convert.base64Decode(
    'CgtNZW50aW9uVHlwZRIVChFNRU5USU9OX1RZUEVfVVNFUhAAEhUKEU1FTlRJT05fVFlQRV9ST0'
    'xFEAESGAoUTUVOVElPTl9UWVBFX0NIQU5ORUwQAhIZChVNRU5USU9OX1RZUEVfRVZFUllPTkUQ'
    'AxIVChFNRU5USU9OX1RZUEVfSEVSRRAE');

@$core.Deprecated('Use sendMessageRequestDescriptor instead')
const SendMessageRequest$json = {
  '1': 'SendMessageRequest',
  '2': [
    {'1': 'sender_id', '3': 1, '4': 1, '5': 9, '10': 'senderId'},
    {'1': 'receiver_id', '3': 2, '4': 1, '5': 9, '10': 'receiverId'},
    {'1': 'channel_type', '3': 3, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'channel_id', '3': 4, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'msg_type', '3': 5, '4': 1, '5': 14, '6': '.chirp.chat.MsgType', '10': 'msgType'},
    {'1': 'content', '3': 6, '4': 1, '5': 12, '10': 'content'},
    {'1': 'client_timestamp', '3': 7, '4': 1, '5': 3, '10': 'clientTimestamp'},
    {'1': 'priority', '3': 8, '4': 1, '5': 14, '6': '.chirp.chat.Priority', '10': 'priority'},
    {'1': 'metadata', '3': 9, '4': 1, '5': 12, '10': 'metadata'},
    {'1': 'ttl_seconds', '3': 10, '4': 1, '5': 5, '10': 'ttlSeconds'},
    {'1': 'reply_to_message_id', '3': 11, '4': 1, '5': 9, '10': 'replyToMessageId'},
  ],
};

/// Descriptor for `SendMessageRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List sendMessageRequestDescriptor = $convert.base64Decode(
    'ChJTZW5kTWVzc2FnZVJlcXVlc3QSGwoJc2VuZGVyX2lkGAEgASgJUghzZW5kZXJJZBIfCgtyZW'
    'NlaXZlcl9pZBgCIAEoCVIKcmVjZWl2ZXJJZBI6CgxjaGFubmVsX3R5cGUYAyABKA4yFy5jaGly'
    'cC5jaGF0LkNoYW5uZWxUeXBlUgtjaGFubmVsVHlwZRIdCgpjaGFubmVsX2lkGAQgASgJUgljaG'
    'FubmVsSWQSLgoIbXNnX3R5cGUYBSABKA4yEy5jaGlycC5jaGF0Lk1zZ1R5cGVSB21zZ1R5cGUS'
    'GAoHY29udGVudBgGIAEoDFIHY29udGVudBIpChBjbGllbnRfdGltZXN0YW1wGAcgASgDUg9jbG'
    'llbnRUaW1lc3RhbXASMAoIcHJpb3JpdHkYCCABKA4yFC5jaGlycC5jaGF0LlByaW9yaXR5Ughw'
    'cmlvcml0eRIaCghtZXRhZGF0YRgJIAEoDFIIbWV0YWRhdGESHwoLdHRsX3NlY29uZHMYCiABKA'
    'VSCnR0bFNlY29uZHMSLQoTcmVwbHlfdG9fbWVzc2FnZV9pZBgLIAEoCVIQcmVwbHlUb01lc3Nh'
    'Z2VJZA==');

@$core.Deprecated('Use sendMessageResponseDescriptor instead')
const SendMessageResponse$json = {
  '1': 'SendMessageResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'message_id', '3': 2, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'server_timestamp', '3': 3, '4': 1, '5': 3, '10': 'serverTimestamp'},
  ],
};

/// Descriptor for `SendMessageResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List sendMessageResponseDescriptor = $convert.base64Decode(
    'ChNTZW5kTWVzc2FnZVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEh0KCm1lc3NhZ2VfaWQYAiABKAlSCW1lc3NhZ2VJZBIpChBzZXJ2ZXJfdGlt'
    'ZXN0YW1wGAMgASgDUg9zZXJ2ZXJUaW1lc3RhbXA=');

@$core.Deprecated('Use chatMessageDescriptor instead')
const ChatMessage$json = {
  '1': 'ChatMessage',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'sender_id', '3': 2, '4': 1, '5': 9, '10': 'senderId'},
    {'1': 'receiver_id', '3': 3, '4': 1, '5': 9, '10': 'receiverId'},
    {'1': 'channel_type', '3': 4, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'channel_id', '3': 5, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'msg_type', '3': 6, '4': 1, '5': 14, '6': '.chirp.chat.MsgType', '10': 'msgType'},
    {'1': 'content', '3': 7, '4': 1, '5': 12, '10': 'content'},
    {'1': 'timestamp', '3': 8, '4': 1, '5': 3, '10': 'timestamp'},
    {'1': 'priority', '3': 9, '4': 1, '5': 14, '6': '.chirp.chat.Priority', '10': 'priority'},
    {'1': 'metadata', '3': 10, '4': 1, '5': 12, '10': 'metadata'},
    {'1': 'ttl_seconds', '3': 11, '4': 1, '5': 5, '10': 'ttlSeconds'},
    {'1': 'sender_kind', '3': 12, '4': 1, '5': 14, '6': '.chirp.chat.SenderKind', '10': 'senderKind'},
    {'1': 'reply_to_message_id', '3': 13, '4': 1, '5': 9, '10': 'replyToMessageId'},
  ],
};

/// Descriptor for `ChatMessage`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List chatMessageDescriptor = $convert.base64Decode(
    'CgtDaGF0TWVzc2FnZRIdCgptZXNzYWdlX2lkGAEgASgJUgltZXNzYWdlSWQSGwoJc2VuZGVyX2'
    'lkGAIgASgJUghzZW5kZXJJZBIfCgtyZWNlaXZlcl9pZBgDIAEoCVIKcmVjZWl2ZXJJZBI6Cgxj'
    'aGFubmVsX3R5cGUYBCABKA4yFy5jaGlycC5jaGF0LkNoYW5uZWxUeXBlUgtjaGFubmVsVHlwZR'
    'IdCgpjaGFubmVsX2lkGAUgASgJUgljaGFubmVsSWQSLgoIbXNnX3R5cGUYBiABKA4yEy5jaGly'
    'cC5jaGF0Lk1zZ1R5cGVSB21zZ1R5cGUSGAoHY29udGVudBgHIAEoDFIHY29udGVudBIcCgl0aW'
    '1lc3RhbXAYCCABKANSCXRpbWVzdGFtcBIwCghwcmlvcml0eRgJIAEoDjIULmNoaXJwLmNoYXQu'
    'UHJpb3JpdHlSCHByaW9yaXR5EhoKCG1ldGFkYXRhGAogASgMUghtZXRhZGF0YRIfCgt0dGxfc2'
    'Vjb25kcxgLIAEoBVIKdHRsU2Vjb25kcxI3CgtzZW5kZXJfa2luZBgMIAEoDjIWLmNoaXJwLmNo'
    'YXQuU2VuZGVyS2luZFIKc2VuZGVyS2luZBItChNyZXBseV90b19tZXNzYWdlX2lkGA0gASgJUh'
    'ByZXBseVRvTWVzc2FnZUlk');

@$core.Deprecated('Use npcPlayerUtteranceDescriptor instead')
const NpcPlayerUtterance$json = {
  '1': 'NpcPlayerUtterance',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'sender_id', '3': 2, '4': 1, '5': 9, '10': 'senderId'},
    {'1': 'npc_id', '3': 3, '4': 1, '5': 9, '10': 'npcId'},
    {'1': 'content', '3': 4, '4': 1, '5': 12, '10': 'content'},
    {'1': 'timestamp', '3': 5, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `NpcPlayerUtterance`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List npcPlayerUtteranceDescriptor = $convert.base64Decode(
    'ChJOcGNQbGF5ZXJVdHRlcmFuY2USHQoKbWVzc2FnZV9pZBgBIAEoCVIJbWVzc2FnZUlkEhsKCX'
    'NlbmRlcl9pZBgCIAEoCVIIc2VuZGVySWQSFQoGbnBjX2lkGAMgASgJUgVucGNJZBIYCgdjb250'
    'ZW50GAQgASgMUgdjb250ZW50EhwKCXRpbWVzdGFtcBgFIAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use getHistoryRequestDescriptor instead')
const GetHistoryRequest$json = {
  '1': 'GetHistoryRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'channel_id', '3': 3, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'before_timestamp', '3': 4, '4': 1, '5': 3, '10': 'beforeTimestamp'},
    {'1': 'limit', '3': 5, '4': 1, '5': 5, '10': 'limit'},
  ],
};

/// Descriptor for `GetHistoryRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getHistoryRequestDescriptor = $convert.base64Decode(
    'ChFHZXRIaXN0b3J5UmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSOgoMY2hhbm5lbF'
    '90eXBlGAIgASgOMhcuY2hpcnAuY2hhdC5DaGFubmVsVHlwZVILY2hhbm5lbFR5cGUSHQoKY2hh'
    'bm5lbF9pZBgDIAEoCVIJY2hhbm5lbElkEikKEGJlZm9yZV90aW1lc3RhbXAYBCABKANSD2JlZm'
    '9yZVRpbWVzdGFtcBIUCgVsaW1pdBgFIAEoBVIFbGltaXQ=');

@$core.Deprecated('Use getHistoryResponseDescriptor instead')
const GetHistoryResponse$json = {
  '1': 'GetHistoryResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'messages', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.ChatMessage', '10': 'messages'},
    {'1': 'has_more', '3': 3, '4': 1, '5': 8, '10': 'hasMore'},
  ],
};

/// Descriptor for `GetHistoryResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getHistoryResponseDescriptor = $convert.base64Decode(
    'ChJHZXRIaXN0b3J5UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSMwoIbWVzc2FnZXMYAiADKAsyFy5jaGlycC5jaGF0LkNoYXRNZXNzYWdlUght'
    'ZXNzYWdlcxIZCghoYXNfbW9yZRgDIAEoCFIHaGFzTW9yZQ==');

@$core.Deprecated('Use createGroupRequestDescriptor instead')
const CreateGroupRequest$json = {
  '1': 'CreateGroupRequest',
  '2': [
    {'1': 'creator_id', '3': 1, '4': 1, '5': 9, '10': 'creatorId'},
    {'1': 'group_name', '3': 2, '4': 1, '5': 9, '10': 'groupName'},
    {'1': 'description', '3': 3, '4': 1, '5': 9, '10': 'description'},
    {'1': 'avatar_url', '3': 4, '4': 1, '5': 9, '10': 'avatarUrl'},
    {'1': 'max_members', '3': 5, '4': 1, '5': 5, '10': 'maxMembers'},
    {'1': 'initial_members', '3': 6, '4': 3, '5': 9, '10': 'initialMembers'},
  ],
};

/// Descriptor for `CreateGroupRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createGroupRequestDescriptor = $convert.base64Decode(
    'ChJDcmVhdGVHcm91cFJlcXVlc3QSHQoKY3JlYXRvcl9pZBgBIAEoCVIJY3JlYXRvcklkEh0KCm'
    'dyb3VwX25hbWUYAiABKAlSCWdyb3VwTmFtZRIgCgtkZXNjcmlwdGlvbhgDIAEoCVILZGVzY3Jp'
    'cHRpb24SHQoKYXZhdGFyX3VybBgEIAEoCVIJYXZhdGFyVXJsEh8KC21heF9tZW1iZXJzGAUgAS'
    'gFUgptYXhNZW1iZXJzEicKD2luaXRpYWxfbWVtYmVycxgGIAMoCVIOaW5pdGlhbE1lbWJlcnM=');

@$core.Deprecated('Use createGroupResponseDescriptor instead')
const CreateGroupResponse$json = {
  '1': 'CreateGroupResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `CreateGroupResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createGroupResponseDescriptor = $convert.base64Decode(
    'ChNDcmVhdGVHcm91cFJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEhkKCGdyb3VwX2lkGAIgASgJUgdncm91cElkEh8KC3NlcnZlcl90aW1lGAMg'
    'ASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use groupInfoDescriptor instead')
const GroupInfo$json = {
  '1': 'GroupInfo',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'group_name', '3': 2, '4': 1, '5': 9, '10': 'groupName'},
    {'1': 'description', '3': 3, '4': 1, '5': 9, '10': 'description'},
    {'1': 'avatar_url', '3': 4, '4': 1, '5': 9, '10': 'avatarUrl'},
    {'1': 'owner_id', '3': 5, '4': 1, '5': 9, '10': 'ownerId'},
    {'1': 'member_count', '3': 6, '4': 1, '5': 5, '10': 'memberCount'},
    {'1': 'max_members', '3': 7, '4': 1, '5': 5, '10': 'maxMembers'},
    {'1': 'created_at', '3': 8, '4': 1, '5': 3, '10': 'createdAt'},
    {'1': 'metadata', '3': 9, '4': 3, '5': 11, '6': '.chirp.chat.GroupInfo.MetadataEntry', '10': 'metadata'},
  ],
  '3': [GroupInfo_MetadataEntry$json],
};

@$core.Deprecated('Use groupInfoDescriptor instead')
const GroupInfo_MetadataEntry$json = {
  '1': 'MetadataEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `GroupInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List groupInfoDescriptor = $convert.base64Decode(
    'CglHcm91cEluZm8SGQoIZ3JvdXBfaWQYASABKAlSB2dyb3VwSWQSHQoKZ3JvdXBfbmFtZRgCIA'
    'EoCVIJZ3JvdXBOYW1lEiAKC2Rlc2NyaXB0aW9uGAMgASgJUgtkZXNjcmlwdGlvbhIdCgphdmF0'
    'YXJfdXJsGAQgASgJUglhdmF0YXJVcmwSGQoIb3duZXJfaWQYBSABKAlSB293bmVySWQSIQoMbW'
    'VtYmVyX2NvdW50GAYgASgFUgttZW1iZXJDb3VudBIfCgttYXhfbWVtYmVycxgHIAEoBVIKbWF4'
    'TWVtYmVycxIdCgpjcmVhdGVkX2F0GAggASgDUgljcmVhdGVkQXQSPwoIbWV0YWRhdGEYCSADKA'
    'syIy5jaGlycC5jaGF0Lkdyb3VwSW5mby5NZXRhZGF0YUVudHJ5UghtZXRhZGF0YRo7Cg1NZXRh'
    'ZGF0YUVudHJ5EhAKA2tleRgBIAEoCVIDa2V5EhQKBXZhbHVlGAIgASgJUgV2YWx1ZToCOAE=');

@$core.Deprecated('Use groupMemberDescriptor instead')
const GroupMember$json = {
  '1': 'GroupMember',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'username', '3': 2, '4': 1, '5': 9, '10': 'username'},
    {'1': 'avatar_url', '3': 3, '4': 1, '5': 9, '10': 'avatarUrl'},
    {'1': 'role', '3': 4, '4': 1, '5': 14, '6': '.chirp.chat.GroupMemberRole', '10': 'role'},
    {'1': 'joined_at', '3': 5, '4': 1, '5': 3, '10': 'joinedAt'},
    {'1': 'last_read_at', '3': 6, '4': 1, '5': 3, '10': 'lastReadAt'},
  ],
};

/// Descriptor for `GroupMember`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List groupMemberDescriptor = $convert.base64Decode(
    'CgtHcm91cE1lbWJlchIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGgoIdXNlcm5hbWUYAiABKA'
    'lSCHVzZXJuYW1lEh0KCmF2YXRhcl91cmwYAyABKAlSCWF2YXRhclVybBIvCgRyb2xlGAQgASgO'
    'MhsuY2hpcnAuY2hhdC5Hcm91cE1lbWJlclJvbGVSBHJvbGUSGwoJam9pbmVkX2F0GAUgASgDUg'
    'hqb2luZWRBdBIgCgxsYXN0X3JlYWRfYXQYBiABKANSCmxhc3RSZWFkQXQ=');

@$core.Deprecated('Use joinGroupRequestDescriptor instead')
const JoinGroupRequest$json = {
  '1': 'JoinGroupRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'invite_code', '3': 3, '4': 1, '5': 9, '10': 'inviteCode'},
  ],
};

/// Descriptor for `JoinGroupRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List joinGroupRequestDescriptor = $convert.base64Decode(
    'ChBKb2luR3JvdXBSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIZCghncm91cF9pZB'
    'gCIAEoCVIHZ3JvdXBJZBIfCgtpbnZpdGVfY29kZRgDIAEoCVIKaW52aXRlQ29kZQ==');

@$core.Deprecated('Use joinGroupResponseDescriptor instead')
const JoinGroupResponse$json = {
  '1': 'JoinGroupResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'group', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.GroupInfo', '10': 'group'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `JoinGroupResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List joinGroupResponseDescriptor = $convert.base64Decode(
    'ChFKb2luR3JvdXBSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm9yQ2'
    '9kZVIEY29kZRIrCgVncm91cBgCIAEoCzIVLmNoaXJwLmNoYXQuR3JvdXBJbmZvUgVncm91cBIf'
    'CgtzZXJ2ZXJfdGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use leaveGroupRequestDescriptor instead')
const LeaveGroupRequest$json = {
  '1': 'LeaveGroupRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
  ],
};

/// Descriptor for `LeaveGroupRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List leaveGroupRequestDescriptor = $convert.base64Decode(
    'ChFMZWF2ZUdyb3VwUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGQoIZ3JvdXBfaW'
    'QYAiABKAlSB2dyb3VwSWQ=');

@$core.Deprecated('Use leaveGroupResponseDescriptor instead')
const LeaveGroupResponse$json = {
  '1': 'LeaveGroupResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `LeaveGroupResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List leaveGroupResponseDescriptor = $convert.base64Decode(
    'ChJMZWF2ZUdyb3VwUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use kickMemberRequestDescriptor instead')
const KickMemberRequest$json = {
  '1': 'KickMemberRequest',
  '2': [
    {'1': 'requester_id', '3': 1, '4': 1, '5': 9, '10': 'requesterId'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'target_user_id', '3': 3, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `KickMemberRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List kickMemberRequestDescriptor = $convert.base64Decode(
    'ChFLaWNrTWVtYmVyUmVxdWVzdBIhCgxyZXF1ZXN0ZXJfaWQYASABKAlSC3JlcXVlc3RlcklkEh'
    'kKCGdyb3VwX2lkGAIgASgJUgdncm91cElkEiQKDnRhcmdldF91c2VyX2lkGAMgASgJUgx0YXJn'
    'ZXRVc2VySWQ=');

@$core.Deprecated('Use kickMemberResponseDescriptor instead')
const KickMemberResponse$json = {
  '1': 'KickMemberResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `KickMemberResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List kickMemberResponseDescriptor = $convert.base64Decode(
    'ChJLaWNrTWVtYmVyUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use getGroupInfoRequestDescriptor instead')
const GetGroupInfoRequest$json = {
  '1': 'GetGroupInfoRequest',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
  ],
};

/// Descriptor for `GetGroupInfoRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getGroupInfoRequestDescriptor = $convert.base64Decode(
    'ChNHZXRHcm91cEluZm9SZXF1ZXN0EhkKCGdyb3VwX2lkGAEgASgJUgdncm91cElk');

@$core.Deprecated('Use getGroupInfoResponseDescriptor instead')
const GetGroupInfoResponse$json = {
  '1': 'GetGroupInfoResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'group', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.GroupInfo', '10': 'group'},
  ],
};

/// Descriptor for `GetGroupInfoResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getGroupInfoResponseDescriptor = $convert.base64Decode(
    'ChRHZXRHcm91cEluZm9SZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIrCgVncm91cBgCIAEoCzIVLmNoaXJwLmNoYXQuR3JvdXBJbmZvUgVncm91'
    'cA==');

@$core.Deprecated('Use getGroupMembersRequestDescriptor instead')
const GetGroupMembersRequest$json = {
  '1': 'GetGroupMembersRequest',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'limit', '3': 2, '4': 1, '5': 5, '10': 'limit'},
    {'1': 'offset', '3': 3, '4': 1, '5': 5, '10': 'offset'},
  ],
};

/// Descriptor for `GetGroupMembersRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getGroupMembersRequestDescriptor = $convert.base64Decode(
    'ChZHZXRHcm91cE1lbWJlcnNSZXF1ZXN0EhkKCGdyb3VwX2lkGAEgASgJUgdncm91cElkEhQKBW'
    'xpbWl0GAIgASgFUgVsaW1pdBIWCgZvZmZzZXQYAyABKAVSBm9mZnNldA==');

@$core.Deprecated('Use getGroupMembersResponseDescriptor instead')
const GetGroupMembersResponse$json = {
  '1': 'GetGroupMembersResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'members', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.GroupMember', '10': 'members'},
    {'1': 'total_count', '3': 3, '4': 1, '5': 5, '10': 'totalCount'},
  ],
};

/// Descriptor for `GetGroupMembersResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getGroupMembersResponseDescriptor = $convert.base64Decode(
    'ChdHZXRHcm91cE1lbWJlcnNSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLk'
    'Vycm9yQ29kZVIEY29kZRIxCgdtZW1iZXJzGAIgAygLMhcuY2hpcnAuY2hhdC5Hcm91cE1lbWJl'
    'clIHbWVtYmVycxIfCgt0b3RhbF9jb3VudBgDIAEoBVIKdG90YWxDb3VudA==');

@$core.Deprecated('Use getUserGroupsRequestDescriptor instead')
const GetUserGroupsRequest$json = {
  '1': 'GetUserGroupsRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'limit', '3': 2, '4': 1, '5': 5, '10': 'limit'},
    {'1': 'offset', '3': 3, '4': 1, '5': 5, '10': 'offset'},
  ],
};

/// Descriptor for `GetUserGroupsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUserGroupsRequestDescriptor = $convert.base64Decode(
    'ChRHZXRVc2VyR3JvdXBzUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSFAoFbGltaX'
    'QYAiABKAVSBWxpbWl0EhYKBm9mZnNldBgDIAEoBVIGb2Zmc2V0');

@$core.Deprecated('Use getUserGroupsResponseDescriptor instead')
const GetUserGroupsResponse$json = {
  '1': 'GetUserGroupsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'groups', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.GroupInfo', '10': 'groups'},
    {'1': 'total_count', '3': 3, '4': 1, '5': 5, '10': 'totalCount'},
  ],
};

/// Descriptor for `GetUserGroupsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUserGroupsResponseDescriptor = $convert.base64Decode(
    'ChVHZXRVc2VyR3JvdXBzUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSLQoGZ3JvdXBzGAIgAygLMhUuY2hpcnAuY2hhdC5Hcm91cEluZm9SBmdy'
    'b3VwcxIfCgt0b3RhbF9jb3VudBgDIAEoBVIKdG90YWxDb3VudA==');

@$core.Deprecated('Use inviteToGroupRequestDescriptor instead')
const InviteToGroupRequest$json = {
  '1': 'InviteToGroupRequest',
  '2': [
    {'1': 'inviter_id', '3': 1, '4': 1, '5': 9, '10': 'inviterId'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'target_user_id', '3': 3, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `InviteToGroupRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List inviteToGroupRequestDescriptor = $convert.base64Decode(
    'ChRJbnZpdGVUb0dyb3VwUmVxdWVzdBIdCgppbnZpdGVyX2lkGAEgASgJUglpbnZpdGVySWQSGQ'
    'oIZ3JvdXBfaWQYAiABKAlSB2dyb3VwSWQSJAoOdGFyZ2V0X3VzZXJfaWQYAyABKAlSDHRhcmdl'
    'dFVzZXJJZA==');

@$core.Deprecated('Use inviteToGroupResponseDescriptor instead')
const InviteToGroupResponse$json = {
  '1': 'InviteToGroupResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `InviteToGroupResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List inviteToGroupResponseDescriptor = $convert.base64Decode(
    'ChVJbnZpdGVUb0dyb3VwUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use groupCreatedNotifyDescriptor instead')
const GroupCreatedNotify$json = {
  '1': 'GroupCreatedNotify',
  '2': [
    {'1': 'group', '3': 1, '4': 1, '5': 11, '6': '.chirp.chat.GroupInfo', '10': 'group'},
    {'1': 'timestamp', '3': 2, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `GroupCreatedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List groupCreatedNotifyDescriptor = $convert.base64Decode(
    'ChJHcm91cENyZWF0ZWROb3RpZnkSKwoFZ3JvdXAYASABKAsyFS5jaGlycC5jaGF0Lkdyb3VwSW'
    '5mb1IFZ3JvdXASHAoJdGltZXN0YW1wGAIgASgDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use groupMemberJoinedNotifyDescriptor instead')
const GroupMemberJoinedNotify$json = {
  '1': 'GroupMemberJoinedNotify',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'member', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.GroupMember', '10': 'member'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `GroupMemberJoinedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List groupMemberJoinedNotifyDescriptor = $convert.base64Decode(
    'ChdHcm91cE1lbWJlckpvaW5lZE5vdGlmeRIZCghncm91cF9pZBgBIAEoCVIHZ3JvdXBJZBIvCg'
    'ZtZW1iZXIYAiABKAsyFy5jaGlycC5jaGF0Lkdyb3VwTWVtYmVyUgZtZW1iZXISHAoJdGltZXN0'
    'YW1wGAMgASgDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use groupMemberLeftNotifyDescriptor instead')
const GroupMemberLeftNotify$json = {
  '1': 'GroupMemberLeftNotify',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `GroupMemberLeftNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List groupMemberLeftNotifyDescriptor = $convert.base64Decode(
    'ChVHcm91cE1lbWJlckxlZnROb3RpZnkSGQoIZ3JvdXBfaWQYASABKAlSB2dyb3VwSWQSFwoHdX'
    'Nlcl9pZBgCIAEoCVIGdXNlcklkEhwKCXRpbWVzdGFtcBgDIAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use groupMemberKickedNotifyDescriptor instead')
const GroupMemberKickedNotify$json = {
  '1': 'GroupMemberKickedNotify',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'kicked_by', '3': 3, '4': 1, '5': 9, '10': 'kickedBy'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `GroupMemberKickedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List groupMemberKickedNotifyDescriptor = $convert.base64Decode(
    'ChdHcm91cE1lbWJlcktpY2tlZE5vdGlmeRIZCghncm91cF9pZBgBIAEoCVIHZ3JvdXBJZBIXCg'
    'd1c2VyX2lkGAIgASgJUgZ1c2VySWQSGwoJa2lja2VkX2J5GAMgASgJUghraWNrZWRCeRIcCgl0'
    'aW1lc3RhbXAYBCABKANSCXRpbWVzdGFtcA==');

@$core.Deprecated('Use groupUpdatedNotifyDescriptor instead')
const GroupUpdatedNotify$json = {
  '1': 'GroupUpdatedNotify',
  '2': [
    {'1': 'group', '3': 1, '4': 1, '5': 11, '6': '.chirp.chat.GroupInfo', '10': 'group'},
    {'1': 'timestamp', '3': 2, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `GroupUpdatedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List groupUpdatedNotifyDescriptor = $convert.base64Decode(
    'ChJHcm91cFVwZGF0ZWROb3RpZnkSKwoFZ3JvdXAYASABKAsyFS5jaGlycC5jaGF0Lkdyb3VwSW'
    '5mb1IFZ3JvdXASHAoJdGltZXN0YW1wGAIgASgDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use markReadRequestDescriptor instead')
const MarkReadRequest$json = {
  '1': 'MarkReadRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'channel_type', '3': 3, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'message_id', '3': 4, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'read_timestamp', '3': 5, '4': 1, '5': 3, '10': 'readTimestamp'},
  ],
};

/// Descriptor for `MarkReadRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List markReadRequestDescriptor = $convert.base64Decode(
    'Cg9NYXJrUmVhZFJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEh0KCmNoYW5uZWxfaW'
    'QYAiABKAlSCWNoYW5uZWxJZBI6CgxjaGFubmVsX3R5cGUYAyABKA4yFy5jaGlycC5jaGF0LkNo'
    'YW5uZWxUeXBlUgtjaGFubmVsVHlwZRIdCgptZXNzYWdlX2lkGAQgASgJUgltZXNzYWdlSWQSJQ'
    'oOcmVhZF90aW1lc3RhbXAYBSABKANSDXJlYWRUaW1lc3RhbXA=');

@$core.Deprecated('Use markReadResponseDescriptor instead')
const MarkReadResponse$json = {
  '1': 'MarkReadResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `MarkReadResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List markReadResponseDescriptor = $convert.base64Decode(
    'ChBNYXJrUmVhZFJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3JDb2'
    'RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use readReceiptDescriptor instead')
const ReadReceipt$json = {
  '1': 'ReadReceipt',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'message_id', '3': 2, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'read_at', '3': 3, '4': 1, '5': 3, '10': 'readAt'},
  ],
};

/// Descriptor for `ReadReceipt`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List readReceiptDescriptor = $convert.base64Decode(
    'CgtSZWFkUmVjZWlwdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSHQoKbWVzc2FnZV9pZBgCIA'
    'EoCVIJbWVzc2FnZUlkEhcKB3JlYWRfYXQYAyABKANSBnJlYWRBdA==');

@$core.Deprecated('Use getReadReceiptsRequestDescriptor instead')
const GetReadReceiptsRequest$json = {
  '1': 'GetReadReceiptsRequest',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
  ],
};

/// Descriptor for `GetReadReceiptsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getReadReceiptsRequestDescriptor = $convert.base64Decode(
    'ChZHZXRSZWFkUmVjZWlwdHNSZXF1ZXN0Eh0KCm1lc3NhZ2VfaWQYASABKAlSCW1lc3NhZ2VJZA'
    '==');

@$core.Deprecated('Use getReadReceiptsResponseDescriptor instead')
const GetReadReceiptsResponse$json = {
  '1': 'GetReadReceiptsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'receipts', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.ReadReceipt', '10': 'receipts'},
  ],
};

/// Descriptor for `GetReadReceiptsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getReadReceiptsResponseDescriptor = $convert.base64Decode(
    'ChdHZXRSZWFkUmVjZWlwdHNSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLk'
    'Vycm9yQ29kZVIEY29kZRIzCghyZWNlaXB0cxgCIAMoCzIXLmNoaXJwLmNoYXQuUmVhZFJlY2Vp'
    'cHRSCHJlY2VpcHRz');

@$core.Deprecated('Use getUnreadCountRequestDescriptor instead')
const GetUnreadCountRequest$json = {
  '1': 'GetUnreadCountRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetUnreadCountRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUnreadCountRequestDescriptor = $convert.base64Decode(
    'ChVHZXRVbnJlYWRDb3VudFJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklk');

@$core.Deprecated('Use getUnreadCountResponseDescriptor instead')
const GetUnreadCountResponse$json = {
  '1': 'GetUnreadCountResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'total_unread', '3': 2, '4': 1, '5': 5, '10': 'totalUnread'},
    {'1': 'channels', '3': 3, '4': 3, '5': 11, '6': '.chirp.chat.GetUnreadCountResponse.ChannelUnread', '10': 'channels'},
  ],
  '3': [GetUnreadCountResponse_ChannelUnread$json],
};

@$core.Deprecated('Use getUnreadCountResponseDescriptor instead')
const GetUnreadCountResponse_ChannelUnread$json = {
  '1': 'ChannelUnread',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'count', '3': 3, '4': 1, '5': 5, '10': 'count'},
    {'1': 'last_message_id', '3': 4, '4': 1, '5': 9, '10': 'lastMessageId'},
  ],
};

/// Descriptor for `GetUnreadCountResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUnreadCountResponseDescriptor = $convert.base64Decode(
    'ChZHZXRVbnJlYWRDb3VudFJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEiEKDHRvdGFsX3VucmVhZBgCIAEoBVILdG90YWxVbnJlYWQSTAoIY2hh'
    'bm5lbHMYAyADKAsyMC5jaGlycC5jaGF0LkdldFVucmVhZENvdW50UmVzcG9uc2UuQ2hhbm5lbF'
    'VucmVhZFIIY2hhbm5lbHMaqAEKDUNoYW5uZWxVbnJlYWQSHQoKY2hhbm5lbF9pZBgBIAEoCVIJ'
    'Y2hhbm5lbElkEjoKDGNoYW5uZWxfdHlwZRgCIAEoDjIXLmNoaXJwLmNoYXQuQ2hhbm5lbFR5cG'
    'VSC2NoYW5uZWxUeXBlEhQKBWNvdW50GAMgASgFUgVjb3VudBImCg9sYXN0X21lc3NhZ2VfaWQY'
    'BCABKAlSDWxhc3RNZXNzYWdlSWQ=');

@$core.Deprecated('Use messageReadNotifyDescriptor instead')
const MessageReadNotify$json = {
  '1': 'MessageReadNotify',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'message_id', '3': 3, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'reader_user_id', '3': 4, '4': 1, '5': 9, '10': 'readerUserId'},
    {'1': 'read_at', '3': 5, '4': 1, '5': 3, '10': 'readAt'},
  ],
};

/// Descriptor for `MessageReadNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageReadNotifyDescriptor = $convert.base64Decode(
    'ChFNZXNzYWdlUmVhZE5vdGlmeRIdCgpjaGFubmVsX2lkGAEgASgJUgljaGFubmVsSWQSOgoMY2'
    'hhbm5lbF90eXBlGAIgASgOMhcuY2hpcnAuY2hhdC5DaGFubmVsVHlwZVILY2hhbm5lbFR5cGUS'
    'HQoKbWVzc2FnZV9pZBgDIAEoCVIJbWVzc2FnZUlkEiQKDnJlYWRlcl91c2VyX2lkGAQgASgJUg'
    'xyZWFkZXJVc2VySWQSFwoHcmVhZF9hdBgFIAEoA1IGcmVhZEF0');

@$core.Deprecated('Use typingIndicatorStateDescriptor instead')
const TypingIndicatorState$json = {
  '1': 'TypingIndicatorState',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'user_id', '3': 3, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'is_typing', '3': 4, '4': 1, '5': 8, '10': 'isTyping'},
    {'1': 'timestamp', '3': 5, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `TypingIndicatorState`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List typingIndicatorStateDescriptor = $convert.base64Decode(
    'ChRUeXBpbmdJbmRpY2F0b3JTdGF0ZRIdCgpjaGFubmVsX2lkGAEgASgJUgljaGFubmVsSWQSOg'
    'oMY2hhbm5lbF90eXBlGAIgASgOMhcuY2hpcnAuY2hhdC5DaGFubmVsVHlwZVILY2hhbm5lbFR5'
    'cGUSFwoHdXNlcl9pZBgDIAEoCVIGdXNlcklkEhsKCWlzX3R5cGluZxgEIAEoCFIIaXNUeXBpbm'
    'cSHAoJdGltZXN0YW1wGAUgASgDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use messageAckDescriptor instead')
const MessageAck$json = {
  '1': 'MessageAck',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'received_at', '3': 3, '4': 1, '5': 3, '10': 'receivedAt'},
  ],
};

/// Descriptor for `MessageAck`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageAckDescriptor = $convert.base64Decode(
    'CgpNZXNzYWdlQWNrEh0KCm1lc3NhZ2VfaWQYASABKAlSCW1lc3NhZ2VJZBIXCgd1c2VyX2lkGA'
    'IgASgJUgZ1c2VySWQSHwoLcmVjZWl2ZWRfYXQYAyABKANSCnJlY2VpdmVkQXQ=');

@$core.Deprecated('Use messageNackDescriptor instead')
const MessageNack$json = {
  '1': 'MessageNack',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'error_code', '3': 3, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'errorCode'},
    {'1': 'error_message', '3': 4, '4': 1, '5': 9, '10': 'errorMessage'},
    {'1': 'failed_at', '3': 5, '4': 1, '5': 3, '10': 'failedAt'},
  ],
};

/// Descriptor for `MessageNack`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageNackDescriptor = $convert.base64Decode(
    'CgtNZXNzYWdlTmFjaxIdCgptZXNzYWdlX2lkGAEgASgJUgltZXNzYWdlSWQSFwoHdXNlcl9pZB'
    'gCIAEoCVIGdXNlcklkEjYKCmVycm9yX2NvZGUYAyABKA4yFy5jaGlycC5jb21tb24uRXJyb3JD'
    'b2RlUgllcnJvckNvZGUSIwoNZXJyb3JfbWVzc2FnZRgEIAEoCVIMZXJyb3JNZXNzYWdlEhsKCW'
    'ZhaWxlZF9hdBgFIAEoA1IIZmFpbGVkQXQ=');

@$core.Deprecated('Use deliveryStatusDescriptor instead')
const DeliveryStatus$json = {
  '1': 'DeliveryStatus',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'status', '3': 3, '4': 1, '5': 14, '6': '.chirp.chat.DeliveryStatus.Status', '10': 'status'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
    {'1': 'retry_count', '3': 5, '4': 1, '5': 5, '10': 'retryCount'},
  ],
  '4': [DeliveryStatus_Status$json],
};

@$core.Deprecated('Use deliveryStatusDescriptor instead')
const DeliveryStatus_Status$json = {
  '1': 'Status',
  '2': [
    {'1': 'PENDING', '2': 0},
    {'1': 'DELIVERED', '2': 1},
    {'1': 'FAILED', '2': 2},
    {'1': 'ACKNOWLEDGED', '2': 3},
  ],
};

/// Descriptor for `DeliveryStatus`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deliveryStatusDescriptor = $convert.base64Decode(
    'Cg5EZWxpdmVyeVN0YXR1cxIdCgptZXNzYWdlX2lkGAEgASgJUgltZXNzYWdlSWQSFwoHdXNlcl'
    '9pZBgCIAEoCVIGdXNlcklkEjkKBnN0YXR1cxgDIAEoDjIhLmNoaXJwLmNoYXQuRGVsaXZlcnlT'
    'dGF0dXMuU3RhdHVzUgZzdGF0dXMSHAoJdGltZXN0YW1wGAQgASgDUgl0aW1lc3RhbXASHwoLcm'
    'V0cnlfY291bnQYBSABKAVSCnJldHJ5Q291bnQiQgoGU3RhdHVzEgsKB1BFTkRJTkcQABINCglE'
    'RUxJVkVSRUQQARIKCgZGQUlMRUQQAhIQCgxBQ0tOT1dMRURHRUQQAw==');

@$core.Deprecated('Use paginationTokenDescriptor instead')
const PaginationToken$json = {
  '1': 'PaginationToken',
  '2': [
    {'1': 'cursor', '3': 1, '4': 1, '5': 9, '10': 'cursor'},
    {'1': 'timestamp', '3': 2, '4': 1, '5': 3, '10': 'timestamp'},
    {'1': 'page_size', '3': 3, '4': 1, '5': 5, '10': 'pageSize'},
  ],
};

/// Descriptor for `PaginationToken`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List paginationTokenDescriptor = $convert.base64Decode(
    'Cg9QYWdpbmF0aW9uVG9rZW4SFgoGY3Vyc29yGAEgASgJUgZjdXJzb3ISHAoJdGltZXN0YW1wGA'
    'IgASgDUgl0aW1lc3RhbXASGwoJcGFnZV9zaXplGAMgASgFUghwYWdlU2l6ZQ==');

@$core.Deprecated('Use getHistoryRequestV2Descriptor instead')
const GetHistoryRequestV2$json = {
  '1': 'GetHistoryRequestV2',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'channel_id', '3': 3, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'pagination', '3': 4, '4': 1, '5': 11, '6': '.chirp.chat.PaginationToken', '10': 'pagination'},
    {'1': 'limit', '3': 5, '4': 1, '5': 5, '10': 'limit'},
    {'1': 'include_deleted', '3': 6, '4': 1, '5': 8, '10': 'includeDeleted'},
    {'1': 'since_timestamp', '3': 7, '4': 1, '5': 3, '10': 'sinceTimestamp'},
  ],
};

/// Descriptor for `GetHistoryRequestV2`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getHistoryRequestV2Descriptor = $convert.base64Decode(
    'ChNHZXRIaXN0b3J5UmVxdWVzdFYyEhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBI6CgxjaGFubm'
    'VsX3R5cGUYAiABKA4yFy5jaGlycC5jaGF0LkNoYW5uZWxUeXBlUgtjaGFubmVsVHlwZRIdCgpj'
    'aGFubmVsX2lkGAMgASgJUgljaGFubmVsSWQSOwoKcGFnaW5hdGlvbhgEIAEoCzIbLmNoaXJwLm'
    'NoYXQuUGFnaW5hdGlvblRva2VuUgpwYWdpbmF0aW9uEhQKBWxpbWl0GAUgASgFUgVsaW1pdBIn'
    'Cg9pbmNsdWRlX2RlbGV0ZWQYBiABKAhSDmluY2x1ZGVEZWxldGVkEicKD3NpbmNlX3RpbWVzdG'
    'FtcBgHIAEoA1IOc2luY2VUaW1lc3RhbXA=');

@$core.Deprecated('Use getHistoryResponseV2Descriptor instead')
const GetHistoryResponseV2$json = {
  '1': 'GetHistoryResponseV2',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'messages', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.ChatMessage', '10': 'messages'},
    {'1': 'next_page', '3': 3, '4': 1, '5': 11, '6': '.chirp.chat.PaginationToken', '10': 'nextPage'},
    {'1': 'has_more', '3': 4, '4': 1, '5': 8, '10': 'hasMore'},
    {'1': 'total_count', '3': 5, '4': 1, '5': 5, '10': 'totalCount'},
  ],
};

/// Descriptor for `GetHistoryResponseV2`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getHistoryResponseV2Descriptor = $convert.base64Decode(
    'ChRHZXRIaXN0b3J5UmVzcG9uc2VWMhIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIzCghtZXNzYWdlcxgCIAMoCzIXLmNoaXJwLmNoYXQuQ2hhdE1lc3NhZ2VS'
    'CG1lc3NhZ2VzEjgKCW5leHRfcGFnZRgDIAEoCzIbLmNoaXJwLmNoYXQuUGFnaW5hdGlvblRva2'
    'VuUghuZXh0UGFnZRIZCghoYXNfbW9yZRgEIAEoCFIHaGFzTW9yZRIfCgt0b3RhbF9jb3VudBgF'
    'IAEoBVIKdG90YWxDb3VudA==');

@$core.Deprecated('Use trackMessageRequestDescriptor instead')
const TrackMessageRequest$json = {
  '1': 'TrackMessageRequest',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'receiver_id', '3': 2, '4': 1, '5': 9, '10': 'receiverId'},
    {'1': 'expires_at', '3': 3, '4': 1, '5': 3, '10': 'expiresAt'},
  ],
};

/// Descriptor for `TrackMessageRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List trackMessageRequestDescriptor = $convert.base64Decode(
    'ChNUcmFja01lc3NhZ2VSZXF1ZXN0Eh0KCm1lc3NhZ2VfaWQYASABKAlSCW1lc3NhZ2VJZBIfCg'
    'tyZWNlaXZlcl9pZBgCIAEoCVIKcmVjZWl2ZXJJZBIdCgpleHBpcmVzX2F0GAMgASgDUglleHBp'
    'cmVzQXQ=');

@$core.Deprecated('Use trackMessageResponseDescriptor instead')
const TrackMessageResponse$json = {
  '1': 'TrackMessageResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'tracking_id', '3': 2, '4': 1, '5': 9, '10': 'trackingId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `TrackMessageResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List trackMessageResponseDescriptor = $convert.base64Decode(
    'ChRUcmFja01lc3NhZ2VSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIfCgt0cmFja2luZ19pZBgCIAEoCVIKdHJhY2tpbmdJZBIfCgtzZXJ2ZXJf'
    'dGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use channelPermissionsDescriptor instead')
const ChannelPermissions$json = {
  '1': 'ChannelPermissions',
  '2': [
    {'1': 'can_read', '3': 1, '4': 1, '5': 8, '10': 'canRead'},
    {'1': 'can_write', '3': 2, '4': 1, '5': 8, '10': 'canWrite'},
    {'1': 'can_speak', '3': 3, '4': 1, '5': 8, '10': 'canSpeak'},
    {'1': 'can_join', '3': 4, '4': 1, '5': 8, '10': 'canJoin'},
    {'1': 'can_manage', '3': 5, '4': 1, '5': 8, '10': 'canManage'},
  ],
};

/// Descriptor for `ChannelPermissions`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelPermissionsDescriptor = $convert.base64Decode(
    'ChJDaGFubmVsUGVybWlzc2lvbnMSGQoIY2FuX3JlYWQYASABKAhSB2NhblJlYWQSGwoJY2FuX3'
    'dyaXRlGAIgASgIUghjYW5Xcml0ZRIbCgljYW5fc3BlYWsYAyABKAhSCGNhblNwZWFrEhkKCGNh'
    'bl9qb2luGAQgASgIUgdjYW5Kb2luEh0KCmNhbl9tYW5hZ2UYBSABKAhSCWNhbk1hbmFnZQ==');

@$core.Deprecated('Use permissionOverrideEntryDescriptor instead')
const PermissionOverrideEntry$json = {
  '1': 'PermissionOverrideEntry',
  '2': [
    {'1': 'type', '3': 1, '4': 1, '5': 14, '6': '.chirp.chat.PermissionType', '10': 'type'},
    {'1': 'id', '3': 2, '4': 1, '5': 9, '10': 'id'},
    {'1': 'permissions', '3': 3, '4': 1, '5': 11, '6': '.chirp.chat.ChannelPermissions', '10': 'permissions'},
    {'1': 'allow', '3': 4, '4': 1, '5': 14, '6': '.chirp.chat.PermissionOverride', '10': 'allow'},
    {'1': 'deny', '3': 5, '4': 1, '5': 14, '6': '.chirp.chat.PermissionOverride', '10': 'deny'},
  ],
};

/// Descriptor for `PermissionOverrideEntry`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List permissionOverrideEntryDescriptor = $convert.base64Decode(
    'ChdQZXJtaXNzaW9uT3ZlcnJpZGVFbnRyeRIuCgR0eXBlGAEgASgOMhouY2hpcnAuY2hhdC5QZX'
    'JtaXNzaW9uVHlwZVIEdHlwZRIOCgJpZBgCIAEoCVICaWQSQAoLcGVybWlzc2lvbnMYAyABKAsy'
    'Hi5jaGlycC5jaGF0LkNoYW5uZWxQZXJtaXNzaW9uc1ILcGVybWlzc2lvbnMSNAoFYWxsb3cYBC'
    'ABKA4yHi5jaGlycC5jaGF0LlBlcm1pc3Npb25PdmVycmlkZVIFYWxsb3cSMgoEZGVueRgFIAEo'
    'DjIeLmNoaXJwLmNoYXQuUGVybWlzc2lvbk92ZXJyaWRlUgRkZW55');

@$core.Deprecated('Use channelCategoryDescriptor instead')
const ChannelCategory$json = {
  '1': 'ChannelCategory',
  '2': [
    {'1': 'category_id', '3': 1, '4': 1, '5': 9, '10': 'categoryId'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'name', '3': 3, '4': 1, '5': 9, '10': 'name'},
    {'1': 'position', '3': 4, '4': 1, '5': 5, '10': 'position'},
    {'1': 'is_collapsed', '3': 5, '4': 1, '5': 8, '10': 'isCollapsed'},
    {'1': 'created_at', '3': 6, '4': 1, '5': 3, '10': 'createdAt'},
  ],
};

/// Descriptor for `ChannelCategory`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelCategoryDescriptor = $convert.base64Decode(
    'Cg9DaGFubmVsQ2F0ZWdvcnkSHwoLY2F0ZWdvcnlfaWQYASABKAlSCmNhdGVnb3J5SWQSGQoIZ3'
    'JvdXBfaWQYAiABKAlSB2dyb3VwSWQSEgoEbmFtZRgDIAEoCVIEbmFtZRIaCghwb3NpdGlvbhgE'
    'IAEoBVIIcG9zaXRpb24SIQoMaXNfY29sbGFwc2VkGAUgASgIUgtpc0NvbGxhcHNlZBIdCgpjcm'
    'VhdGVkX2F0GAYgASgDUgljcmVhdGVkQXQ=');

@$core.Deprecated('Use channelDescriptor instead')
const Channel$json = {
  '1': 'Channel',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'category_id', '3': 3, '4': 1, '5': 9, '10': 'categoryId'},
    {'1': 'name', '3': 4, '4': 1, '5': 9, '10': 'name'},
    {'1': 'kind', '3': 5, '4': 1, '5': 14, '6': '.chirp.chat.ChannelKind', '10': 'kind'},
    {'1': 'position', '3': 6, '4': 1, '5': 5, '10': 'position'},
    {'1': 'description', '3': 7, '4': 1, '5': 9, '10': 'description'},
    {'1': 'is_nsfw', '3': 8, '4': 1, '5': 8, '10': 'isNsfw'},
    {'1': 'created_at', '3': 9, '4': 1, '5': 3, '10': 'createdAt'},
    {'1': 'slowmode_seconds', '3': 10, '4': 1, '5': 3, '10': 'slowmodeSeconds'},
    {'1': 'permission_overrides', '3': 11, '4': 3, '5': 11, '6': '.chirp.chat.PermissionOverrideEntry', '10': 'permissionOverrides'},
    {'1': 'bitrate', '3': 12, '4': 1, '5': 5, '10': 'bitrate'},
    {'1': 'user_limit', '3': 13, '4': 1, '5': 5, '10': 'userLimit'},
    {'1': 'rtc_region', '3': 14, '4': 1, '5': 9, '10': 'rtcRegion'},
  ],
};

/// Descriptor for `Channel`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelDescriptor = $convert.base64Decode(
    'CgdDaGFubmVsEh0KCmNoYW5uZWxfaWQYASABKAlSCWNoYW5uZWxJZBIZCghncm91cF9pZBgCIA'
    'EoCVIHZ3JvdXBJZBIfCgtjYXRlZ29yeV9pZBgDIAEoCVIKY2F0ZWdvcnlJZBISCgRuYW1lGAQg'
    'ASgJUgRuYW1lEisKBGtpbmQYBSABKA4yFy5jaGlycC5jaGF0LkNoYW5uZWxLaW5kUgRraW5kEh'
    'oKCHBvc2l0aW9uGAYgASgFUghwb3NpdGlvbhIgCgtkZXNjcmlwdGlvbhgHIAEoCVILZGVzY3Jp'
    'cHRpb24SFwoHaXNfbnNmdxgIIAEoCFIGaXNOc2Z3Eh0KCmNyZWF0ZWRfYXQYCSABKANSCWNyZW'
    'F0ZWRBdBIpChBzbG93bW9kZV9zZWNvbmRzGAogASgDUg9zbG93bW9kZVNlY29uZHMSVgoUcGVy'
    'bWlzc2lvbl9vdmVycmlkZXMYCyADKAsyIy5jaGlycC5jaGF0LlBlcm1pc3Npb25PdmVycmlkZU'
    'VudHJ5UhNwZXJtaXNzaW9uT3ZlcnJpZGVzEhgKB2JpdHJhdGUYDCABKAVSB2JpdHJhdGUSHQoK'
    'dXNlcl9saW1pdBgNIAEoBVIJdXNlckxpbWl0Eh0KCnJ0Y19yZWdpb24YDiABKAlSCXJ0Y1JlZ2'
    'lvbg==');

@$core.Deprecated('Use createChannelRequestDescriptor instead')
const CreateChannelRequest$json = {
  '1': 'CreateChannelRequest',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'requester_id', '3': 2, '4': 1, '5': 9, '10': 'requesterId'},
    {'1': 'name', '3': 3, '4': 1, '5': 9, '10': 'name'},
    {'1': 'kind', '3': 4, '4': 1, '5': 14, '6': '.chirp.chat.ChannelKind', '10': 'kind'},
    {'1': 'category_id', '3': 5, '4': 1, '5': 9, '10': 'categoryId'},
    {'1': 'description', '3': 6, '4': 1, '5': 9, '10': 'description'},
    {'1': 'permission_overrides', '3': 7, '4': 3, '5': 11, '6': '.chirp.chat.PermissionOverrideEntry', '10': 'permissionOverrides'},
    {'1': 'position', '3': 8, '4': 1, '5': 5, '10': 'position'},
  ],
};

/// Descriptor for `CreateChannelRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createChannelRequestDescriptor = $convert.base64Decode(
    'ChRDcmVhdGVDaGFubmVsUmVxdWVzdBIZCghncm91cF9pZBgBIAEoCVIHZ3JvdXBJZBIhCgxyZX'
    'F1ZXN0ZXJfaWQYAiABKAlSC3JlcXVlc3RlcklkEhIKBG5hbWUYAyABKAlSBG5hbWUSKwoEa2lu'
    'ZBgEIAEoDjIXLmNoaXJwLmNoYXQuQ2hhbm5lbEtpbmRSBGtpbmQSHwoLY2F0ZWdvcnlfaWQYBS'
    'ABKAlSCmNhdGVnb3J5SWQSIAoLZGVzY3JpcHRpb24YBiABKAlSC2Rlc2NyaXB0aW9uElYKFHBl'
    'cm1pc3Npb25fb3ZlcnJpZGVzGAcgAygLMiMuY2hpcnAuY2hhdC5QZXJtaXNzaW9uT3ZlcnJpZG'
    'VFbnRyeVITcGVybWlzc2lvbk92ZXJyaWRlcxIaCghwb3NpdGlvbhgIIAEoBVIIcG9zaXRpb24=');

@$core.Deprecated('Use createChannelResponseDescriptor instead')
const CreateChannelResponse$json = {
  '1': 'CreateChannelResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'channel', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.Channel', '10': 'channel'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `CreateChannelResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createChannelResponseDescriptor = $convert.base64Decode(
    'ChVDcmVhdGVDaGFubmVsUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSLQoHY2hhbm5lbBgCIAEoCzITLmNoaXJwLmNoYXQuQ2hhbm5lbFIHY2hh'
    'bm5lbBIfCgtzZXJ2ZXJfdGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use updateChannelRequestDescriptor instead')
const UpdateChannelRequest$json = {
  '1': 'UpdateChannelRequest',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'requester_id', '3': 2, '4': 1, '5': 9, '10': 'requesterId'},
    {'1': 'name', '3': 3, '4': 1, '5': 9, '9': 0, '10': 'name', '17': true},
    {'1': 'description', '3': 4, '4': 1, '5': 9, '9': 1, '10': 'description', '17': true},
    {'1': 'position', '3': 5, '4': 1, '5': 5, '9': 2, '10': 'position', '17': true},
    {'1': 'category_id', '3': 6, '4': 1, '5': 9, '9': 3, '10': 'categoryId', '17': true},
    {'1': 'permission_overrides', '3': 7, '4': 3, '5': 11, '6': '.chirp.chat.PermissionOverrideEntry', '10': 'permissionOverrides'},
  ],
  '8': [
    {'1': '_name'},
    {'1': '_description'},
    {'1': '_position'},
    {'1': '_category_id'},
  ],
};

/// Descriptor for `UpdateChannelRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List updateChannelRequestDescriptor = $convert.base64Decode(
    'ChRVcGRhdGVDaGFubmVsUmVxdWVzdBIdCgpjaGFubmVsX2lkGAEgASgJUgljaGFubmVsSWQSIQ'
    'oMcmVxdWVzdGVyX2lkGAIgASgJUgtyZXF1ZXN0ZXJJZBIXCgRuYW1lGAMgASgJSABSBG5hbWWI'
    'AQESJQoLZGVzY3JpcHRpb24YBCABKAlIAVILZGVzY3JpcHRpb26IAQESHwoIcG9zaXRpb24YBS'
    'ABKAVIAlIIcG9zaXRpb26IAQESJAoLY2F0ZWdvcnlfaWQYBiABKAlIA1IKY2F0ZWdvcnlJZIgB'
    'ARJWChRwZXJtaXNzaW9uX292ZXJyaWRlcxgHIAMoCzIjLmNoaXJwLmNoYXQuUGVybWlzc2lvbk'
    '92ZXJyaWRlRW50cnlSE3Blcm1pc3Npb25PdmVycmlkZXNCBwoFX25hbWVCDgoMX2Rlc2NyaXB0'
    'aW9uQgsKCV9wb3NpdGlvbkIOCgxfY2F0ZWdvcnlfaWQ=');

@$core.Deprecated('Use updateChannelResponseDescriptor instead')
const UpdateChannelResponse$json = {
  '1': 'UpdateChannelResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'channel', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.Channel', '10': 'channel'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `UpdateChannelResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List updateChannelResponseDescriptor = $convert.base64Decode(
    'ChVVcGRhdGVDaGFubmVsUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSLQoHY2hhbm5lbBgCIAEoCzITLmNoaXJwLmNoYXQuQ2hhbm5lbFIHY2hh'
    'bm5lbBIfCgtzZXJ2ZXJfdGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use deleteChannelRequestDescriptor instead')
const DeleteChannelRequest$json = {
  '1': 'DeleteChannelRequest',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'requester_id', '3': 2, '4': 1, '5': 9, '10': 'requesterId'},
  ],
};

/// Descriptor for `DeleteChannelRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deleteChannelRequestDescriptor = $convert.base64Decode(
    'ChREZWxldGVDaGFubmVsUmVxdWVzdBIdCgpjaGFubmVsX2lkGAEgASgJUgljaGFubmVsSWQSIQ'
    'oMcmVxdWVzdGVyX2lkGAIgASgJUgtyZXF1ZXN0ZXJJZA==');

@$core.Deprecated('Use deleteChannelResponseDescriptor instead')
const DeleteChannelResponse$json = {
  '1': 'DeleteChannelResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `DeleteChannelResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deleteChannelResponseDescriptor = $convert.base64Decode(
    'ChVEZWxldGVDaGFubmVsUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use getChannelsRequestDescriptor instead')
const GetChannelsRequest$json = {
  '1': 'GetChannelsRequest',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetChannelsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getChannelsRequestDescriptor = $convert.base64Decode(
    'ChJHZXRDaGFubmVsc1JlcXVlc3QSGQoIZ3JvdXBfaWQYASABKAlSB2dyb3VwSWQSFwoHdXNlcl'
    '9pZBgCIAEoCVIGdXNlcklk');

@$core.Deprecated('Use getChannelsResponseDescriptor instead')
const GetChannelsResponse$json = {
  '1': 'GetChannelsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'channels', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.Channel', '10': 'channels'},
    {'1': 'categories', '3': 3, '4': 3, '5': 11, '6': '.chirp.chat.ChannelCategory', '10': 'categories'},
  ],
};

/// Descriptor for `GetChannelsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getChannelsResponseDescriptor = $convert.base64Decode(
    'ChNHZXRDaGFubmVsc1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEi8KCGNoYW5uZWxzGAIgAygLMhMuY2hpcnAuY2hhdC5DaGFubmVsUghjaGFu'
    'bmVscxI7CgpjYXRlZ29yaWVzGAMgAygLMhsuY2hpcnAuY2hhdC5DaGFubmVsQ2F0ZWdvcnlSCm'
    'NhdGVnb3JpZXM=');

@$core.Deprecated('Use createCategoryRequestDescriptor instead')
const CreateCategoryRequest$json = {
  '1': 'CreateCategoryRequest',
  '2': [
    {'1': 'group_id', '3': 1, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'requester_id', '3': 2, '4': 1, '5': 9, '10': 'requesterId'},
    {'1': 'name', '3': 3, '4': 1, '5': 9, '10': 'name'},
    {'1': 'position', '3': 4, '4': 1, '5': 5, '10': 'position'},
  ],
};

/// Descriptor for `CreateCategoryRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createCategoryRequestDescriptor = $convert.base64Decode(
    'ChVDcmVhdGVDYXRlZ29yeVJlcXVlc3QSGQoIZ3JvdXBfaWQYASABKAlSB2dyb3VwSWQSIQoMcm'
    'VxdWVzdGVyX2lkGAIgASgJUgtyZXF1ZXN0ZXJJZBISCgRuYW1lGAMgASgJUgRuYW1lEhoKCHBv'
    'c2l0aW9uGAQgASgFUghwb3NpdGlvbg==');

@$core.Deprecated('Use createCategoryResponseDescriptor instead')
const CreateCategoryResponse$json = {
  '1': 'CreateCategoryResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'category', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.ChannelCategory', '10': 'category'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `CreateCategoryResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createCategoryResponseDescriptor = $convert.base64Decode(
    'ChZDcmVhdGVDYXRlZ29yeVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEjcKCGNhdGVnb3J5GAIgASgLMhsuY2hpcnAuY2hhdC5DaGFubmVsQ2F0'
    'ZWdvcnlSCGNhdGVnb3J5Eh8KC3NlcnZlcl90aW1lGAMgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use channelCreatedNotifyDescriptor instead')
const ChannelCreatedNotify$json = {
  '1': 'ChannelCreatedNotify',
  '2': [
    {'1': 'channel', '3': 1, '4': 1, '5': 11, '6': '.chirp.chat.Channel', '10': 'channel'},
    {'1': 'timestamp', '3': 2, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ChannelCreatedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelCreatedNotifyDescriptor = $convert.base64Decode(
    'ChRDaGFubmVsQ3JlYXRlZE5vdGlmeRItCgdjaGFubmVsGAEgASgLMhMuY2hpcnAuY2hhdC5DaG'
    'FubmVsUgdjaGFubmVsEhwKCXRpbWVzdGFtcBgCIAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use channelUpdatedNotifyDescriptor instead')
const ChannelUpdatedNotify$json = {
  '1': 'ChannelUpdatedNotify',
  '2': [
    {'1': 'channel', '3': 1, '4': 1, '5': 11, '6': '.chirp.chat.Channel', '10': 'channel'},
    {'1': 'timestamp', '3': 2, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ChannelUpdatedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelUpdatedNotifyDescriptor = $convert.base64Decode(
    'ChRDaGFubmVsVXBkYXRlZE5vdGlmeRItCgdjaGFubmVsGAEgASgLMhMuY2hpcnAuY2hhdC5DaG'
    'FubmVsUgdjaGFubmVsEhwKCXRpbWVzdGFtcBgCIAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use channelDeletedNotifyDescriptor instead')
const ChannelDeletedNotify$json = {
  '1': 'ChannelDeletedNotify',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'group_id', '3': 2, '4': 1, '5': 9, '10': 'groupId'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ChannelDeletedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelDeletedNotifyDescriptor = $convert.base64Decode(
    'ChRDaGFubmVsRGVsZXRlZE5vdGlmeRIdCgpjaGFubmVsX2lkGAEgASgJUgljaGFubmVsSWQSGQ'
    'oIZ3JvdXBfaWQYAiABKAlSB2dyb3VwSWQSHAoJdGltZXN0YW1wGAMgASgDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use messageReactionDescriptor instead')
const MessageReaction$json = {
  '1': 'MessageReaction',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'emoji', '3': 2, '4': 1, '5': 9, '10': 'emoji'},
    {'1': 'count', '3': 3, '4': 1, '5': 5, '10': 'count'},
    {'1': 'user_ids', '3': 4, '4': 3, '5': 9, '10': 'userIds'},
    {'1': 'reacted_by_me', '3': 5, '4': 1, '5': 8, '10': 'reactedByMe'},
  ],
};

/// Descriptor for `MessageReaction`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageReactionDescriptor = $convert.base64Decode(
    'Cg9NZXNzYWdlUmVhY3Rpb24SHQoKbWVzc2FnZV9pZBgBIAEoCVIJbWVzc2FnZUlkEhQKBWVtb2'
    'ppGAIgASgJUgVlbW9qaRIUCgVjb3VudBgDIAEoBVIFY291bnQSGQoIdXNlcl9pZHMYBCADKAlS'
    'B3VzZXJJZHMSIgoNcmVhY3RlZF9ieV9tZRgFIAEoCFILcmVhY3RlZEJ5TWU=');

@$core.Deprecated('Use addReactionRequestDescriptor instead')
const AddReactionRequest$json = {
  '1': 'AddReactionRequest',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'emoji', '3': 3, '4': 1, '5': 9, '10': 'emoji'},
  ],
};

/// Descriptor for `AddReactionRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List addReactionRequestDescriptor = $convert.base64Decode(
    'ChJBZGRSZWFjdGlvblJlcXVlc3QSHQoKbWVzc2FnZV9pZBgBIAEoCVIJbWVzc2FnZUlkEhcKB3'
    'VzZXJfaWQYAiABKAlSBnVzZXJJZBIUCgVlbW9qaRgDIAEoCVIFZW1vamk=');

@$core.Deprecated('Use addReactionResponseDescriptor instead')
const AddReactionResponse$json = {
  '1': 'AddReactionResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'reaction', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.MessageReaction', '10': 'reaction'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `AddReactionResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List addReactionResponseDescriptor = $convert.base64Decode(
    'ChNBZGRSZWFjdGlvblJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEjcKCHJlYWN0aW9uGAIgASgLMhsuY2hpcnAuY2hhdC5NZXNzYWdlUmVhY3Rp'
    'b25SCHJlYWN0aW9uEh8KC3NlcnZlcl90aW1lGAMgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use removeReactionRequestDescriptor instead')
const RemoveReactionRequest$json = {
  '1': 'RemoveReactionRequest',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'emoji', '3': 3, '4': 1, '5': 9, '10': 'emoji'},
  ],
};

/// Descriptor for `RemoveReactionRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List removeReactionRequestDescriptor = $convert.base64Decode(
    'ChVSZW1vdmVSZWFjdGlvblJlcXVlc3QSHQoKbWVzc2FnZV9pZBgBIAEoCVIJbWVzc2FnZUlkEh'
    'cKB3VzZXJfaWQYAiABKAlSBnVzZXJJZBIUCgVlbW9qaRgDIAEoCVIFZW1vamk=');

@$core.Deprecated('Use removeReactionResponseDescriptor instead')
const RemoveReactionResponse$json = {
  '1': 'RemoveReactionResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `RemoveReactionResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List removeReactionResponseDescriptor = $convert.base64Decode(
    'ChZSZW1vdmVSZWFjdGlvblJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use getReactionsRequestDescriptor instead')
const GetReactionsRequest$json = {
  '1': 'GetReactionsRequest',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'emoji', '3': 2, '4': 1, '5': 9, '10': 'emoji'},
  ],
};

/// Descriptor for `GetReactionsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getReactionsRequestDescriptor = $convert.base64Decode(
    'ChNHZXRSZWFjdGlvbnNSZXF1ZXN0Eh0KCm1lc3NhZ2VfaWQYASABKAlSCW1lc3NhZ2VJZBIUCg'
    'VlbW9qaRgCIAEoCVIFZW1vamk=');

@$core.Deprecated('Use getReactionsResponseDescriptor instead')
const GetReactionsResponse$json = {
  '1': 'GetReactionsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'reactions', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.MessageReaction', '10': 'reactions'},
  ],
};

/// Descriptor for `GetReactionsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getReactionsResponseDescriptor = $convert.base64Decode(
    'ChRHZXRSZWFjdGlvbnNSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRI5CglyZWFjdGlvbnMYAiADKAsyGy5jaGlycC5jaGF0Lk1lc3NhZ2VSZWFj'
    'dGlvblIJcmVhY3Rpb25z');

@$core.Deprecated('Use reactionAddedNotifyDescriptor instead')
const ReactionAddedNotify$json = {
  '1': 'ReactionAddedNotify',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'emoji', '3': 3, '4': 1, '5': 9, '10': 'emoji'},
    {'1': 'user_id', '3': 4, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'timestamp', '3': 5, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ReactionAddedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List reactionAddedNotifyDescriptor = $convert.base64Decode(
    'ChNSZWFjdGlvbkFkZGVkTm90aWZ5Eh0KCm1lc3NhZ2VfaWQYASABKAlSCW1lc3NhZ2VJZBIdCg'
    'pjaGFubmVsX2lkGAIgASgJUgljaGFubmVsSWQSFAoFZW1vamkYAyABKAlSBWVtb2ppEhcKB3Vz'
    'ZXJfaWQYBCABKAlSBnVzZXJJZBIcCgl0aW1lc3RhbXAYBSABKANSCXRpbWVzdGFtcA==');

@$core.Deprecated('Use reactionRemovedNotifyDescriptor instead')
const ReactionRemovedNotify$json = {
  '1': 'ReactionRemovedNotify',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'emoji', '3': 3, '4': 1, '5': 9, '10': 'emoji'},
    {'1': 'user_id', '3': 4, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'timestamp', '3': 5, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ReactionRemovedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List reactionRemovedNotifyDescriptor = $convert.base64Decode(
    'ChVSZWFjdGlvblJlbW92ZWROb3RpZnkSHQoKbWVzc2FnZV9pZBgBIAEoCVIJbWVzc2FnZUlkEh'
    '0KCmNoYW5uZWxfaWQYAiABKAlSCWNoYW5uZWxJZBIUCgVlbW9qaRgDIAEoCVIFZW1vamkSFwoH'
    'dXNlcl9pZBgEIAEoCVIGdXNlcklkEhwKCXRpbWVzdGFtcBgFIAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use mentionDescriptor instead')
const Mention$json = {
  '1': 'Mention',
  '2': [
    {'1': 'type', '3': 1, '4': 1, '5': 14, '6': '.chirp.chat.MentionType', '10': 'type'},
    {'1': 'id', '3': 2, '4': 1, '5': 9, '10': 'id'},
    {'1': 'start_index', '3': 3, '4': 1, '5': 5, '10': 'startIndex'},
    {'1': 'length', '3': 4, '4': 1, '5': 5, '10': 'length'},
  ],
};

/// Descriptor for `Mention`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List mentionDescriptor = $convert.base64Decode(
    'CgdNZW50aW9uEisKBHR5cGUYASABKA4yFy5jaGlycC5jaGF0Lk1lbnRpb25UeXBlUgR0eXBlEg'
    '4KAmlkGAIgASgJUgJpZBIfCgtzdGFydF9pbmRleBgDIAEoBVIKc3RhcnRJbmRleBIWCgZsZW5n'
    'dGgYBCABKAVSBmxlbmd0aA==');

@$core.Deprecated('Use chatMessageExDescriptor instead')
const ChatMessageEx$json = {
  '1': 'ChatMessageEx',
  '2': [
    {'1': 'base_message', '3': 1, '4': 1, '5': 11, '6': '.chirp.chat.ChatMessage', '10': 'baseMessage'},
    {'1': 'mentions', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.Mention', '10': 'mentions'},
    {'1': 'mentioned_user_ids', '3': 3, '4': 3, '5': 9, '10': 'mentionedUserIds'},
    {'1': 'mentions_everyone', '3': 4, '4': 1, '5': 8, '10': 'mentionsEveryone'},
    {'1': 'mentions_here', '3': 5, '4': 1, '5': 8, '10': 'mentionsHere'},
  ],
};

/// Descriptor for `ChatMessageEx`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List chatMessageExDescriptor = $convert.base64Decode(
    'Cg1DaGF0TWVzc2FnZUV4EjoKDGJhc2VfbWVzc2FnZRgBIAEoCzIXLmNoaXJwLmNoYXQuQ2hhdE'
    '1lc3NhZ2VSC2Jhc2VNZXNzYWdlEi8KCG1lbnRpb25zGAIgAygLMhMuY2hpcnAuY2hhdC5NZW50'
    'aW9uUghtZW50aW9ucxIsChJtZW50aW9uZWRfdXNlcl9pZHMYAyADKAlSEG1lbnRpb25lZFVzZX'
    'JJZHMSKwoRbWVudGlvbnNfZXZlcnlvbmUYBCABKAhSEG1lbnRpb25zRXZlcnlvbmUSIwoNbWVu'
    'dGlvbnNfaGVyZRgFIAEoCFIMbWVudGlvbnNIZXJl');

@$core.Deprecated('Use mentionSuggestionDescriptor instead')
const MentionSuggestion$json = {
  '1': 'MentionSuggestion',
  '2': [
    {'1': 'display_text', '3': 1, '4': 1, '5': 9, '10': 'displayText'},
    {'1': 'id', '3': 2, '4': 1, '5': 9, '10': 'id'},
    {'1': 'type', '3': 3, '4': 1, '5': 14, '6': '.chirp.chat.MentionType', '10': 'type'},
    {'1': 'icon_url', '3': 4, '4': 1, '5': 9, '10': 'iconUrl'},
  ],
};

/// Descriptor for `MentionSuggestion`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List mentionSuggestionDescriptor = $convert.base64Decode(
    'ChFNZW50aW9uU3VnZ2VzdGlvbhIhCgxkaXNwbGF5X3RleHQYASABKAlSC2Rpc3BsYXlUZXh0Eg'
    '4KAmlkGAIgASgJUgJpZBIrCgR0eXBlGAMgASgOMhcuY2hpcnAuY2hhdC5NZW50aW9uVHlwZVIE'
    'dHlwZRIZCghpY29uX3VybBgEIAEoCVIHaWNvblVybA==');

@$core.Deprecated('Use getMentionSuggestionsRequestDescriptor instead')
const GetMentionSuggestionsRequest$json = {
  '1': 'GetMentionSuggestionsRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'query', '3': 3, '4': 1, '5': 9, '10': 'query'},
  ],
};

/// Descriptor for `GetMentionSuggestionsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getMentionSuggestionsRequestDescriptor = $convert.base64Decode(
    'ChxHZXRNZW50aW9uU3VnZ2VzdGlvbnNSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZB'
    'IdCgpjaGFubmVsX2lkGAIgASgJUgljaGFubmVsSWQSFAoFcXVlcnkYAyABKAlSBXF1ZXJ5');

@$core.Deprecated('Use getMentionSuggestionsResponseDescriptor instead')
const GetMentionSuggestionsResponse$json = {
  '1': 'GetMentionSuggestionsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'suggestions', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.MentionSuggestion', '10': 'suggestions'},
  ],
};

/// Descriptor for `GetMentionSuggestionsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getMentionSuggestionsResponseDescriptor = $convert.base64Decode(
    'Ch1HZXRNZW50aW9uU3VnZ2VzdGlvbnNSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY2'
    '9tbW9uLkVycm9yQ29kZVIEY29kZRI/CgtzdWdnZXN0aW9ucxgCIAMoCzIdLmNoaXJwLmNoYXQu'
    'TWVudGlvblN1Z2dlc3Rpb25SC3N1Z2dlc3Rpb25z');

@$core.Deprecated('Use messageEditDescriptor instead')
const MessageEdit$json = {
  '1': 'MessageEdit',
  '2': [
    {'1': 'old_content', '3': 1, '4': 1, '5': 12, '10': 'oldContent'},
    {'1': 'new_content', '3': 2, '4': 1, '5': 12, '10': 'newContent'},
    {'1': 'edited_at', '3': 3, '4': 1, '5': 3, '10': 'editedAt'},
    {'1': 'edited_by', '3': 4, '4': 1, '5': 9, '10': 'editedBy'},
  ],
};

/// Descriptor for `MessageEdit`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageEditDescriptor = $convert.base64Decode(
    'CgtNZXNzYWdlRWRpdBIfCgtvbGRfY29udGVudBgBIAEoDFIKb2xkQ29udGVudBIfCgtuZXdfY2'
    '9udGVudBgCIAEoDFIKbmV3Q29udGVudBIbCgllZGl0ZWRfYXQYAyABKANSCGVkaXRlZEF0EhsK'
    'CWVkaXRlZF9ieRgEIAEoCVIIZWRpdGVkQnk=');

@$core.Deprecated('Use chatMessageFullDescriptor instead')
const ChatMessageFull$json = {
  '1': 'ChatMessageFull',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'sender_id', '3': 2, '4': 1, '5': 9, '10': 'senderId'},
    {'1': 'receiver_id', '3': 3, '4': 1, '5': 9, '10': 'receiverId'},
    {'1': 'channel_type', '3': 4, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'channel_id', '3': 5, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'msg_type', '3': 6, '4': 1, '5': 14, '6': '.chirp.chat.MsgType', '10': 'msgType'},
    {'1': 'content', '3': 7, '4': 1, '5': 12, '10': 'content'},
    {'1': 'timestamp', '3': 8, '4': 1, '5': 3, '10': 'timestamp'},
    {'1': 'is_deleted', '3': 9, '4': 1, '5': 8, '10': 'isDeleted'},
    {'1': 'deleted_at', '3': 10, '4': 1, '5': 3, '10': 'deletedAt'},
    {'1': 'deleted_by', '3': 11, '4': 1, '5': 9, '10': 'deletedBy'},
    {'1': 'is_edited', '3': 12, '4': 1, '5': 8, '10': 'isEdited'},
    {'1': 'edited_at', '3': 13, '4': 1, '5': 3, '10': 'editedAt'},
    {'1': 'edit_count', '3': 14, '4': 1, '5': 5, '10': 'editCount'},
    {'1': 'edit_history', '3': 15, '4': 3, '5': 11, '6': '.chirp.chat.MessageEdit', '10': 'editHistory'},
    {'1': 'reply_to_message_id', '3': 16, '4': 1, '5': 9, '10': 'replyToMessageId'},
    {'1': 'reply_count', '3': 17, '4': 1, '5': 5, '10': 'replyCount'},
    {'1': 'reactions', '3': 18, '4': 3, '5': 11, '6': '.chirp.chat.MessageReaction', '10': 'reactions'},
    {'1': 'mentions', '3': 19, '4': 3, '5': 11, '6': '.chirp.chat.Mention', '10': 'mentions'},
  ],
};

/// Descriptor for `ChatMessageFull`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List chatMessageFullDescriptor = $convert.base64Decode(
    'Cg9DaGF0TWVzc2FnZUZ1bGwSHQoKbWVzc2FnZV9pZBgBIAEoCVIJbWVzc2FnZUlkEhsKCXNlbm'
    'Rlcl9pZBgCIAEoCVIIc2VuZGVySWQSHwoLcmVjZWl2ZXJfaWQYAyABKAlSCnJlY2VpdmVySWQS'
    'OgoMY2hhbm5lbF90eXBlGAQgASgOMhcuY2hpcnAuY2hhdC5DaGFubmVsVHlwZVILY2hhbm5lbF'
    'R5cGUSHQoKY2hhbm5lbF9pZBgFIAEoCVIJY2hhbm5lbElkEi4KCG1zZ190eXBlGAYgASgOMhMu'
    'Y2hpcnAuY2hhdC5Nc2dUeXBlUgdtc2dUeXBlEhgKB2NvbnRlbnQYByABKAxSB2NvbnRlbnQSHA'
    'oJdGltZXN0YW1wGAggASgDUgl0aW1lc3RhbXASHQoKaXNfZGVsZXRlZBgJIAEoCFIJaXNEZWxl'
    'dGVkEh0KCmRlbGV0ZWRfYXQYCiABKANSCWRlbGV0ZWRBdBIdCgpkZWxldGVkX2J5GAsgASgJUg'
    'lkZWxldGVkQnkSGwoJaXNfZWRpdGVkGAwgASgIUghpc0VkaXRlZBIbCgllZGl0ZWRfYXQYDSAB'
    'KANSCGVkaXRlZEF0Eh0KCmVkaXRfY291bnQYDiABKAVSCWVkaXRDb3VudBI6CgxlZGl0X2hpc3'
    'RvcnkYDyADKAsyFy5jaGlycC5jaGF0Lk1lc3NhZ2VFZGl0UgtlZGl0SGlzdG9yeRItChNyZXBs'
    'eV90b19tZXNzYWdlX2lkGBAgASgJUhByZXBseVRvTWVzc2FnZUlkEh8KC3JlcGx5X2NvdW50GB'
    'EgASgFUgpyZXBseUNvdW50EjkKCXJlYWN0aW9ucxgSIAMoCzIbLmNoaXJwLmNoYXQuTWVzc2Fn'
    'ZVJlYWN0aW9uUglyZWFjdGlvbnMSLwoIbWVudGlvbnMYEyADKAsyEy5jaGlycC5jaGF0Lk1lbn'
    'Rpb25SCG1lbnRpb25z');

@$core.Deprecated('Use editMessageRequestDescriptor instead')
const EditMessageRequest$json = {
  '1': 'EditMessageRequest',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'new_content', '3': 3, '4': 1, '5': 12, '10': 'newContent'},
    {'1': 'edit_timestamp', '3': 4, '4': 1, '5': 3, '10': 'editTimestamp'},
  ],
};

/// Descriptor for `EditMessageRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List editMessageRequestDescriptor = $convert.base64Decode(
    'ChJFZGl0TWVzc2FnZVJlcXVlc3QSHQoKbWVzc2FnZV9pZBgBIAEoCVIJbWVzc2FnZUlkEhcKB3'
    'VzZXJfaWQYAiABKAlSBnVzZXJJZBIfCgtuZXdfY29udGVudBgDIAEoDFIKbmV3Q29udGVudBIl'
    'Cg5lZGl0X3RpbWVzdGFtcBgEIAEoA1INZWRpdFRpbWVzdGFtcA==');

@$core.Deprecated('Use editMessageResponseDescriptor instead')
const EditMessageResponse$json = {
  '1': 'EditMessageResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'message', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.ChatMessageFull', '10': 'message'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `EditMessageResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List editMessageResponseDescriptor = $convert.base64Decode(
    'ChNFZGl0TWVzc2FnZVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEjUKB21lc3NhZ2UYAiABKAsyGy5jaGlycC5jaGF0LkNoYXRNZXNzYWdlRnVs'
    'bFIHbWVzc2FnZRIfCgtzZXJ2ZXJfdGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use deleteMessageRequestDescriptor instead')
const DeleteMessageRequest$json = {
  '1': 'DeleteMessageRequest',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'is_hard_delete', '3': 3, '4': 1, '5': 8, '10': 'isHardDelete'},
  ],
};

/// Descriptor for `DeleteMessageRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deleteMessageRequestDescriptor = $convert.base64Decode(
    'ChREZWxldGVNZXNzYWdlUmVxdWVzdBIdCgptZXNzYWdlX2lkGAEgASgJUgltZXNzYWdlSWQSFw'
    'oHdXNlcl9pZBgCIAEoCVIGdXNlcklkEiQKDmlzX2hhcmRfZGVsZXRlGAMgASgIUgxpc0hhcmRE'
    'ZWxldGU=');

@$core.Deprecated('Use deleteMessageResponseDescriptor instead')
const DeleteMessageResponse$json = {
  '1': 'DeleteMessageResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
    {'1': 'was_permanently_deleted', '3': 3, '4': 1, '5': 8, '10': 'wasPermanentlyDeleted'},
  ],
};

/// Descriptor for `DeleteMessageResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deleteMessageResponseDescriptor = $convert.base64Decode(
    'ChVEZWxldGVNZXNzYWdlUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWUSNgoXd2FzX3Bl'
    'cm1hbmVudGx5X2RlbGV0ZWQYAyABKAhSFXdhc1Blcm1hbmVudGx5RGVsZXRlZA==');

@$core.Deprecated('Use bulkDeleteRequestDescriptor instead')
const BulkDeleteRequest$json = {
  '1': 'BulkDeleteRequest',
  '2': [
    {'1': 'message_ids', '3': 1, '4': 3, '5': 9, '10': 'messageIds'},
    {'1': 'requester_id', '3': 2, '4': 1, '5': 9, '10': 'requesterId'},
    {'1': 'channel_id', '3': 3, '4': 1, '5': 9, '10': 'channelId'},
  ],
};

/// Descriptor for `BulkDeleteRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List bulkDeleteRequestDescriptor = $convert.base64Decode(
    'ChFCdWxrRGVsZXRlUmVxdWVzdBIfCgttZXNzYWdlX2lkcxgBIAMoCVIKbWVzc2FnZUlkcxIhCg'
    'xyZXF1ZXN0ZXJfaWQYAiABKAlSC3JlcXVlc3RlcklkEh0KCmNoYW5uZWxfaWQYAyABKAlSCWNo'
    'YW5uZWxJZA==');

@$core.Deprecated('Use bulkDeleteResponseDescriptor instead')
const BulkDeleteResponse$json = {
  '1': 'BulkDeleteResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'deleted_count', '3': 2, '4': 1, '5': 5, '10': 'deletedCount'},
    {'1': 'failed_message_ids', '3': 3, '4': 3, '5': 9, '10': 'failedMessageIds'},
    {'1': 'server_time', '3': 4, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `BulkDeleteResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List bulkDeleteResponseDescriptor = $convert.base64Decode(
    'ChJCdWxrRGVsZXRlUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSIwoNZGVsZXRlZF9jb3VudBgCIAEoBVIMZGVsZXRlZENvdW50EiwKEmZhaWxl'
    'ZF9tZXNzYWdlX2lkcxgDIAMoCVIQZmFpbGVkTWVzc2FnZUlkcxIfCgtzZXJ2ZXJfdGltZRgEIA'
    'EoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use messageEditedNotifyDescriptor instead')
const MessageEditedNotify$json = {
  '1': 'MessageEditedNotify',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'new_content', '3': 3, '4': 1, '5': 12, '10': 'newContent'},
    {'1': 'edited_at', '3': 4, '4': 1, '5': 3, '10': 'editedAt'},
    {'1': 'edited_by', '3': 5, '4': 1, '5': 9, '10': 'editedBy'},
  ],
};

/// Descriptor for `MessageEditedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageEditedNotifyDescriptor = $convert.base64Decode(
    'ChNNZXNzYWdlRWRpdGVkTm90aWZ5Eh0KCm1lc3NhZ2VfaWQYASABKAlSCW1lc3NhZ2VJZBIdCg'
    'pjaGFubmVsX2lkGAIgASgJUgljaGFubmVsSWQSHwoLbmV3X2NvbnRlbnQYAyABKAxSCm5ld0Nv'
    'bnRlbnQSGwoJZWRpdGVkX2F0GAQgASgDUghlZGl0ZWRBdBIbCgllZGl0ZWRfYnkYBSABKAlSCG'
    'VkaXRlZEJ5');

@$core.Deprecated('Use messageDeletedNotifyDescriptor instead')
const MessageDeletedNotify$json = {
  '1': 'MessageDeletedNotify',
  '2': [
    {'1': 'message_id', '3': 1, '4': 1, '5': 9, '10': 'messageId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'is_hard_delete', '3': 3, '4': 1, '5': 8, '10': 'isHardDelete'},
    {'1': 'deleted_by', '3': 4, '4': 1, '5': 9, '10': 'deletedBy'},
    {'1': 'deleted_at', '3': 5, '4': 1, '5': 3, '10': 'deletedAt'},
  ],
};

/// Descriptor for `MessageDeletedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageDeletedNotifyDescriptor = $convert.base64Decode(
    'ChRNZXNzYWdlRGVsZXRlZE5vdGlmeRIdCgptZXNzYWdlX2lkGAEgASgJUgltZXNzYWdlSWQSHQ'
    'oKY2hhbm5lbF9pZBgCIAEoCVIJY2hhbm5lbElkEiQKDmlzX2hhcmRfZGVsZXRlGAMgASgIUgxp'
    'c0hhcmREZWxldGUSHQoKZGVsZXRlZF9ieRgEIAEoCVIJZGVsZXRlZEJ5Eh0KCmRlbGV0ZWRfYX'
    'QYBSABKANSCWRlbGV0ZWRBdA==');

@$core.Deprecated('Use typingIndicatorDescriptor instead')
const TypingIndicator$json = {
  '1': 'TypingIndicator',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'user_id', '3': 3, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'username', '3': 4, '4': 1, '5': 9, '10': 'username'},
    {'1': 'is_typing', '3': 5, '4': 1, '5': 8, '10': 'isTyping'},
    {'1': 'timestamp', '3': 6, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `TypingIndicator`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List typingIndicatorDescriptor = $convert.base64Decode(
    'Cg9UeXBpbmdJbmRpY2F0b3ISHQoKY2hhbm5lbF9pZBgBIAEoCVIJY2hhbm5lbElkEjoKDGNoYW'
    '5uZWxfdHlwZRgCIAEoDjIXLmNoaXJwLmNoYXQuQ2hhbm5lbFR5cGVSC2NoYW5uZWxUeXBlEhcK'
    'B3VzZXJfaWQYAyABKAlSBnVzZXJJZBIaCgh1c2VybmFtZRgEIAEoCVIIdXNlcm5hbWUSGwoJaX'
    'NfdHlwaW5nGAUgASgIUghpc1R5cGluZxIcCgl0aW1lc3RhbXAYBiABKANSCXRpbWVzdGFtcA==');

@$core.Deprecated('Use getTypingUsersRequestDescriptor instead')
const GetTypingUsersRequest$json = {
  '1': 'GetTypingUsersRequest',
  '2': [
    {'1': 'channel_id', '3': 1, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
  ],
};

/// Descriptor for `GetTypingUsersRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getTypingUsersRequestDescriptor = $convert.base64Decode(
    'ChVHZXRUeXBpbmdVc2Vyc1JlcXVlc3QSHQoKY2hhbm5lbF9pZBgBIAEoCVIJY2hhbm5lbElkEj'
    'oKDGNoYW5uZWxfdHlwZRgCIAEoDjIXLmNoaXJwLmNoYXQuQ2hhbm5lbFR5cGVSC2NoYW5uZWxU'
    'eXBl');

@$core.Deprecated('Use getTypingUsersResponseDescriptor instead')
const GetTypingUsersResponse$json = {
  '1': 'GetTypingUsersResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'typing_user_ids', '3': 2, '4': 3, '5': 9, '10': 'typingUserIds'},
    {'1': 'usernames', '3': 3, '4': 3, '5': 9, '10': 'usernames'},
  ],
};

/// Descriptor for `GetTypingUsersResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getTypingUsersResponseDescriptor = $convert.base64Decode(
    'ChZHZXRUeXBpbmdVc2Vyc1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEiYKD3R5cGluZ191c2VyX2lkcxgCIAMoCVINdHlwaW5nVXNlcklkcxIc'
    'Cgl1c2VybmFtZXMYAyADKAlSCXVzZXJuYW1lcw==');

@$core.Deprecated('Use fileInfoDescriptor instead')
const FileInfo$json = {
  '1': 'FileInfo',
  '2': [
    {'1': 'file_id', '3': 1, '4': 1, '5': 9, '10': 'fileId'},
    {'1': 'filename', '3': 2, '4': 1, '5': 9, '10': 'filename'},
    {'1': 'file_size', '3': 3, '4': 1, '5': 3, '10': 'fileSize'},
    {'1': 'mime_type', '3': 4, '4': 1, '5': 9, '10': 'mimeType'},
    {'1': 'checksum', '3': 5, '4': 1, '5': 9, '10': 'checksum'},
    {'1': 'storage_url', '3': 6, '4': 1, '5': 9, '10': 'storageUrl'},
    {'1': 'uploaded_at', '3': 7, '4': 1, '5': 3, '10': 'uploadedAt'},
    {'1': 'uploaded_by', '3': 8, '4': 1, '5': 9, '10': 'uploadedBy'},
    {'1': 'width', '3': 9, '4': 1, '5': 5, '10': 'width'},
    {'1': 'height', '3': 10, '4': 1, '5': 5, '10': 'height'},
    {'1': 'duration', '3': 11, '4': 1, '5': 5, '10': 'duration'},
  ],
};

/// Descriptor for `FileInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List fileInfoDescriptor = $convert.base64Decode(
    'CghGaWxlSW5mbxIXCgdmaWxlX2lkGAEgASgJUgZmaWxlSWQSGgoIZmlsZW5hbWUYAiABKAlSCG'
    'ZpbGVuYW1lEhsKCWZpbGVfc2l6ZRgDIAEoA1IIZmlsZVNpemUSGwoJbWltZV90eXBlGAQgASgJ'
    'UghtaW1lVHlwZRIaCghjaGVja3N1bRgFIAEoCVIIY2hlY2tzdW0SHwoLc3RvcmFnZV91cmwYBi'
    'ABKAlSCnN0b3JhZ2VVcmwSHwoLdXBsb2FkZWRfYXQYByABKANSCnVwbG9hZGVkQXQSHwoLdXBs'
    'b2FkZWRfYnkYCCABKAlSCnVwbG9hZGVkQnkSFAoFd2lkdGgYCSABKAVSBXdpZHRoEhYKBmhlaW'
    'dodBgKIAEoBVIGaGVpZ2h0EhoKCGR1cmF0aW9uGAsgASgFUghkdXJhdGlvbg==');

@$core.Deprecated('Use prepareFileUploadRequestDescriptor instead')
const PrepareFileUploadRequest$json = {
  '1': 'PrepareFileUploadRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'channel_type', '3': 3, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'filename', '3': 4, '4': 1, '5': 9, '10': 'filename'},
    {'1': 'file_size', '3': 5, '4': 1, '5': 3, '10': 'fileSize'},
    {'1': 'mime_type', '3': 6, '4': 1, '5': 9, '10': 'mimeType'},
    {'1': 'checksum', '3': 7, '4': 1, '5': 9, '10': 'checksum'},
  ],
};

/// Descriptor for `PrepareFileUploadRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List prepareFileUploadRequestDescriptor = $convert.base64Decode(
    'ChhQcmVwYXJlRmlsZVVwbG9hZFJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEh0KCm'
    'NoYW5uZWxfaWQYAiABKAlSCWNoYW5uZWxJZBI6CgxjaGFubmVsX3R5cGUYAyABKA4yFy5jaGly'
    'cC5jaGF0LkNoYW5uZWxUeXBlUgtjaGFubmVsVHlwZRIaCghmaWxlbmFtZRgEIAEoCVIIZmlsZW'
    '5hbWUSGwoJZmlsZV9zaXplGAUgASgDUghmaWxlU2l6ZRIbCgltaW1lX3R5cGUYBiABKAlSCG1p'
    'bWVUeXBlEhoKCGNoZWNrc3VtGAcgASgJUghjaGVja3N1bQ==');

@$core.Deprecated('Use prepareFileUploadResponseDescriptor instead')
const PrepareFileUploadResponse$json = {
  '1': 'PrepareFileUploadResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'upload_id', '3': 2, '4': 1, '5': 9, '10': 'uploadId'},
    {'1': 'upload_url', '3': 3, '4': 1, '5': 9, '10': 'uploadUrl'},
    {'1': 'expires_at', '3': 4, '4': 1, '5': 3, '10': 'expiresAt'},
    {'1': 'file_id', '3': 5, '4': 1, '5': 9, '10': 'fileId'},
    {'1': 'headers', '3': 6, '4': 3, '5': 11, '6': '.chirp.chat.PrepareFileUploadResponse.HeadersEntry', '10': 'headers'},
  ],
  '3': [PrepareFileUploadResponse_HeadersEntry$json],
};

@$core.Deprecated('Use prepareFileUploadResponseDescriptor instead')
const PrepareFileUploadResponse_HeadersEntry$json = {
  '1': 'HeadersEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `PrepareFileUploadResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List prepareFileUploadResponseDescriptor = $convert.base64Decode(
    'ChlQcmVwYXJlRmlsZVVwbG9hZFJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb2'
    '4uRXJyb3JDb2RlUgRjb2RlEhsKCXVwbG9hZF9pZBgCIAEoCVIIdXBsb2FkSWQSHQoKdXBsb2Fk'
    'X3VybBgDIAEoCVIJdXBsb2FkVXJsEh0KCmV4cGlyZXNfYXQYBCABKANSCWV4cGlyZXNBdBIXCg'
    'dmaWxlX2lkGAUgASgJUgZmaWxlSWQSTAoHaGVhZGVycxgGIAMoCzIyLmNoaXJwLmNoYXQuUHJl'
    'cGFyZUZpbGVVcGxvYWRSZXNwb25zZS5IZWFkZXJzRW50cnlSB2hlYWRlcnMaOgoMSGVhZGVyc0'
    'VudHJ5EhAKA2tleRgBIAEoCVIDa2V5EhQKBXZhbHVlGAIgASgJUgV2YWx1ZToCOAE=');

@$core.Deprecated('Use confirmFileUploadRequestDescriptor instead')
const ConfirmFileUploadRequest$json = {
  '1': 'ConfirmFileUploadRequest',
  '2': [
    {'1': 'upload_id', '3': 1, '4': 1, '5': 9, '10': 'uploadId'},
    {'1': 'file_id', '3': 2, '4': 1, '5': 9, '10': 'fileId'},
    {'1': 'message_id', '3': 3, '4': 1, '5': 9, '10': 'messageId'},
  ],
};

/// Descriptor for `ConfirmFileUploadRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List confirmFileUploadRequestDescriptor = $convert.base64Decode(
    'ChhDb25maXJtRmlsZVVwbG9hZFJlcXVlc3QSGwoJdXBsb2FkX2lkGAEgASgJUgh1cGxvYWRJZB'
    'IXCgdmaWxlX2lkGAIgASgJUgZmaWxlSWQSHQoKbWVzc2FnZV9pZBgDIAEoCVIJbWVzc2FnZUlk');

@$core.Deprecated('Use confirmFileUploadResponseDescriptor instead')
const ConfirmFileUploadResponse$json = {
  '1': 'ConfirmFileUploadResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'file_info', '3': 2, '4': 1, '5': 11, '6': '.chirp.chat.FileInfo', '10': 'fileInfo'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `ConfirmFileUploadResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List confirmFileUploadResponseDescriptor = $convert.base64Decode(
    'ChlDb25maXJtRmlsZVVwbG9hZFJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb2'
    '4uRXJyb3JDb2RlUgRjb2RlEjEKCWZpbGVfaW5mbxgCIAEoCzIULmNoaXJwLmNoYXQuRmlsZUlu'
    'Zm9SCGZpbGVJbmZvEh8KC3NlcnZlcl90aW1lGAMgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use getFileDownloadRequestDescriptor instead')
const GetFileDownloadRequest$json = {
  '1': 'GetFileDownloadRequest',
  '2': [
    {'1': 'file_id', '3': 1, '4': 1, '5': 9, '10': 'fileId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetFileDownloadRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getFileDownloadRequestDescriptor = $convert.base64Decode(
    'ChZHZXRGaWxlRG93bmxvYWRSZXF1ZXN0EhcKB2ZpbGVfaWQYASABKAlSBmZpbGVJZBIXCgd1c2'
    'VyX2lkGAIgASgJUgZ1c2VySWQ=');

@$core.Deprecated('Use getFileDownloadResponseDescriptor instead')
const GetFileDownloadResponse$json = {
  '1': 'GetFileDownloadResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'download_url', '3': 2, '4': 1, '5': 9, '10': 'downloadUrl'},
    {'1': 'expires_at', '3': 3, '4': 1, '5': 3, '10': 'expiresAt'},
    {'1': 'file_info', '3': 4, '4': 1, '5': 11, '6': '.chirp.chat.FileInfo', '10': 'fileInfo'},
  ],
};

/// Descriptor for `GetFileDownloadResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getFileDownloadResponseDescriptor = $convert.base64Decode(
    'ChdHZXRGaWxlRG93bmxvYWRSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLk'
    'Vycm9yQ29kZVIEY29kZRIhCgxkb3dubG9hZF91cmwYAiABKAlSC2Rvd25sb2FkVXJsEh0KCmV4'
    'cGlyZXNfYXQYAyABKANSCWV4cGlyZXNBdBIxCglmaWxlX2luZm8YBCABKAsyFC5jaGlycC5jaG'
    'F0LkZpbGVJbmZvUghmaWxlSW5mbw==');

@$core.Deprecated('Use fileAttachmentDescriptor instead')
const FileAttachment$json = {
  '1': 'FileAttachment',
  '2': [
    {'1': 'file', '3': 1, '4': 1, '5': 11, '6': '.chirp.chat.FileInfo', '10': 'file'},
    {'1': 'is_spoiler', '3': 2, '4': 1, '5': 8, '10': 'isSpoiler'},
    {'1': 'alt_text', '3': 3, '4': 1, '5': 9, '10': 'altText'},
  ],
};

/// Descriptor for `FileAttachment`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List fileAttachmentDescriptor = $convert.base64Decode(
    'Cg5GaWxlQXR0YWNobWVudBIoCgRmaWxlGAEgASgLMhQuY2hpcnAuY2hhdC5GaWxlSW5mb1IEZm'
    'lsZRIdCgppc19zcG9pbGVyGAIgASgIUglpc1Nwb2lsZXISGQoIYWx0X3RleHQYAyABKAlSB2Fs'
    'dFRleHQ=');

@$core.Deprecated('Use fileMessageDescriptor instead')
const FileMessage$json = {
  '1': 'FileMessage',
  '2': [
    {'1': 'base_message', '3': 1, '4': 1, '5': 11, '6': '.chirp.chat.ChatMessage', '10': 'baseMessage'},
    {'1': 'attachments', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.FileAttachment', '10': 'attachments'},
  ],
};

/// Descriptor for `FileMessage`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List fileMessageDescriptor = $convert.base64Decode(
    'CgtGaWxlTWVzc2FnZRI6CgxiYXNlX21lc3NhZ2UYASABKAsyFy5jaGlycC5jaGF0LkNoYXRNZX'
    'NzYWdlUgtiYXNlTWVzc2FnZRI8CgthdHRhY2htZW50cxgCIAMoCzIaLmNoaXJwLmNoYXQuRmls'
    'ZUF0dGFjaG1lbnRSC2F0dGFjaG1lbnRz');

@$core.Deprecated('Use itemMetadataDescriptor instead')
const ItemMetadata$json = {
  '1': 'ItemMetadata',
  '2': [
    {'1': 'item_id', '3': 1, '4': 1, '5': 9, '10': 'itemId'},
    {'1': 'item_name', '3': 2, '4': 1, '5': 9, '10': 'itemName'},
    {'1': 'quality', '3': 3, '4': 1, '5': 5, '10': 'quality'},
    {'1': 'icon_url', '3': 4, '4': 1, '5': 9, '10': 'iconUrl'},
    {'1': 'count', '3': 5, '4': 1, '5': 5, '10': 'count'},
    {'1': 'attrs', '3': 6, '4': 3, '5': 11, '6': '.chirp.chat.ItemMetadata.AttrsEntry', '10': 'attrs'},
  ],
  '3': [ItemMetadata_AttrsEntry$json],
};

@$core.Deprecated('Use itemMetadataDescriptor instead')
const ItemMetadata_AttrsEntry$json = {
  '1': 'AttrsEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `ItemMetadata`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List itemMetadataDescriptor = $convert.base64Decode(
    'CgxJdGVtTWV0YWRhdGESFwoHaXRlbV9pZBgBIAEoCVIGaXRlbUlkEhsKCWl0ZW1fbmFtZRgCIA'
    'EoCVIIaXRlbU5hbWUSGAoHcXVhbGl0eRgDIAEoBVIHcXVhbGl0eRIZCghpY29uX3VybBgEIAEo'
    'CVIHaWNvblVybBIUCgVjb3VudBgFIAEoBVIFY291bnQSOQoFYXR0cnMYBiADKAsyIy5jaGlycC'
    '5jaGF0Lkl0ZW1NZXRhZGF0YS5BdHRyc0VudHJ5UgVhdHRycxo4CgpBdHRyc0VudHJ5EhAKA2tl'
    'eRgBIAEoCVIDa2V5EhQKBXZhbHVlGAIgASgJUgV2YWx1ZToCOAE=');

@$core.Deprecated('Use skillMetadataDescriptor instead')
const SkillMetadata$json = {
  '1': 'SkillMetadata',
  '2': [
    {'1': 'skill_id', '3': 1, '4': 1, '5': 9, '10': 'skillId'},
    {'1': 'skill_name', '3': 2, '4': 1, '5': 9, '10': 'skillName'},
    {'1': 'level', '3': 3, '4': 1, '5': 5, '10': 'level'},
    {'1': 'icon_url', '3': 4, '4': 1, '5': 9, '10': 'iconUrl'},
    {'1': 'description', '3': 5, '4': 1, '5': 9, '10': 'description'},
  ],
};

/// Descriptor for `SkillMetadata`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List skillMetadataDescriptor = $convert.base64Decode(
    'Cg1Ta2lsbE1ldGFkYXRhEhkKCHNraWxsX2lkGAEgASgJUgdza2lsbElkEh0KCnNraWxsX25hbW'
    'UYAiABKAlSCXNraWxsTmFtZRIUCgVsZXZlbBgDIAEoBVIFbGV2ZWwSGQoIaWNvbl91cmwYBCAB'
    'KAlSB2ljb25VcmwSIAoLZGVzY3JpcHRpb24YBSABKAlSC2Rlc2NyaXB0aW9u');

@$core.Deprecated('Use achievementMetadataDescriptor instead')
const AchievementMetadata$json = {
  '1': 'AchievementMetadata',
  '2': [
    {'1': 'achievement_id', '3': 1, '4': 1, '5': 9, '10': 'achievementId'},
    {'1': 'achievement_name', '3': 2, '4': 1, '5': 9, '10': 'achievementName'},
    {'1': 'description', '3': 3, '4': 1, '5': 9, '10': 'description'},
    {'1': 'icon_url', '3': 4, '4': 1, '5': 9, '10': 'iconUrl'},
    {'1': 'rarity', '3': 5, '4': 1, '5': 5, '10': 'rarity'},
  ],
};

/// Descriptor for `AchievementMetadata`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List achievementMetadataDescriptor = $convert.base64Decode(
    'ChNBY2hpZXZlbWVudE1ldGFkYXRhEiUKDmFjaGlldmVtZW50X2lkGAEgASgJUg1hY2hpZXZlbW'
    'VudElkEikKEGFjaGlldmVtZW50X25hbWUYAiABKAlSD2FjaGlldmVtZW50TmFtZRIgCgtkZXNj'
    'cmlwdGlvbhgDIAEoCVILZGVzY3JpcHRpb24SGQoIaWNvbl91cmwYBCABKAlSB2ljb25VcmwSFg'
    'oGcmFyaXR5GAUgASgFUgZyYXJpdHk=');

@$core.Deprecated('Use tradeMetadataDescriptor instead')
const TradeMetadata$json = {
  '1': 'TradeMetadata',
  '2': [
    {'1': 'trade_id', '3': 1, '4': 1, '5': 9, '10': 'tradeId'},
    {'1': 'status', '3': 2, '4': 1, '5': 9, '10': 'status'},
    {'1': 'amount', '3': 3, '4': 1, '5': 3, '10': 'amount'},
    {'1': 'item_name', '3': 4, '4': 1, '5': 9, '10': 'itemName'},
    {'1': 'item_count', '3': 5, '4': 1, '5': 5, '10': 'itemCount'},
  ],
};

/// Descriptor for `TradeMetadata`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List tradeMetadataDescriptor = $convert.base64Decode(
    'Cg1UcmFkZU1ldGFkYXRhEhkKCHRyYWRlX2lkGAEgASgJUgd0cmFkZUlkEhYKBnN0YXR1cxgCIA'
    'EoCVIGc3RhdHVzEhYKBmFtb3VudBgDIAEoA1IGYW1vdW50EhsKCWl0ZW1fbmFtZRgEIAEoCVII'
    'aXRlbU5hbWUSHQoKaXRlbV9jb3VudBgFIAEoBVIJaXRlbUNvdW50');

@$core.Deprecated('Use npcDialogMetadataDescriptor instead')
const NpcDialogMetadata$json = {
  '1': 'NpcDialogMetadata',
  '2': [
    {'1': 'npc_id', '3': 1, '4': 1, '5': 9, '10': 'npcId'},
    {'1': 'npc_name', '3': 2, '4': 1, '5': 9, '10': 'npcName'},
    {'1': 'dialog_id', '3': 3, '4': 1, '5': 9, '10': 'dialogId'},
    {'1': 'options', '3': 4, '4': 3, '5': 9, '10': 'options'},
  ],
};

/// Descriptor for `NpcDialogMetadata`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List npcDialogMetadataDescriptor = $convert.base64Decode(
    'ChFOcGNEaWFsb2dNZXRhZGF0YRIVCgZucGNfaWQYASABKAlSBW5wY0lkEhkKCG5wY19uYW1lGA'
    'IgASgJUgducGNOYW1lEhsKCWRpYWxvZ19pZBgDIAEoCVIIZGlhbG9nSWQSGAoHb3B0aW9ucxgE'
    'IAMoCVIHb3B0aW9ucw==');

@$core.Deprecated('Use setChannelMuteRequestDescriptor instead')
const SetChannelMuteRequest$json = {
  '1': 'SetChannelMuteRequest',
  '2': [
    {'1': 'channel_type', '3': 1, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'muted', '3': 2, '4': 1, '5': 8, '10': 'muted'},
  ],
};

/// Descriptor for `SetChannelMuteRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setChannelMuteRequestDescriptor = $convert.base64Decode(
    'ChVTZXRDaGFubmVsTXV0ZVJlcXVlc3QSOgoMY2hhbm5lbF90eXBlGAEgASgOMhcuY2hpcnAuY2'
    'hhdC5DaGFubmVsVHlwZVILY2hhbm5lbFR5cGUSFAoFbXV0ZWQYAiABKAhSBW11dGVk');

@$core.Deprecated('Use setChannelMuteResponseDescriptor instead')
const SetChannelMuteResponse$json = {
  '1': 'SetChannelMuteResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'channel_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'muted', '3': 3, '4': 1, '5': 8, '10': 'muted'},
  ],
};

/// Descriptor for `SetChannelMuteResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setChannelMuteResponseDescriptor = $convert.base64Decode(
    'ChZTZXRDaGFubmVsTXV0ZVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEjoKDGNoYW5uZWxfdHlwZRgCIAEoDjIXLmNoaXJwLmNoYXQuQ2hhbm5l'
    'bFR5cGVSC2NoYW5uZWxUeXBlEhQKBW11dGVkGAMgASgIUgVtdXRlZA==');

@$core.Deprecated('Use channelMuteStateDescriptor instead')
const ChannelMuteState$json = {
  '1': 'ChannelMuteState',
  '2': [
    {'1': 'channel_type', '3': 1, '4': 1, '5': 14, '6': '.chirp.chat.ChannelType', '10': 'channelType'},
    {'1': 'muted', '3': 2, '4': 1, '5': 8, '10': 'muted'},
  ],
};

/// Descriptor for `ChannelMuteState`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelMuteStateDescriptor = $convert.base64Decode(
    'ChBDaGFubmVsTXV0ZVN0YXRlEjoKDGNoYW5uZWxfdHlwZRgBIAEoDjIXLmNoaXJwLmNoYXQuQ2'
    'hhbm5lbFR5cGVSC2NoYW5uZWxUeXBlEhQKBW11dGVkGAIgASgIUgVtdXRlZA==');

@$core.Deprecated('Use getChannelMutesRequestDescriptor instead')
const GetChannelMutesRequest$json = {
  '1': 'GetChannelMutesRequest',
};

/// Descriptor for `GetChannelMutesRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getChannelMutesRequestDescriptor = $convert.base64Decode(
    'ChZHZXRDaGFubmVsTXV0ZXNSZXF1ZXN0');

@$core.Deprecated('Use getChannelMutesResponseDescriptor instead')
const GetChannelMutesResponse$json = {
  '1': 'GetChannelMutesResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'states', '3': 2, '4': 3, '5': 11, '6': '.chirp.chat.ChannelMuteState', '10': 'states'},
  ],
};

/// Descriptor for `GetChannelMutesResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getChannelMutesResponseDescriptor = $convert.base64Decode(
    'ChdHZXRDaGFubmVsTXV0ZXNSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLk'
    'Vycm9yQ29kZVIEY29kZRI0CgZzdGF0ZXMYAiADKAsyHC5jaGlycC5jaGF0LkNoYW5uZWxNdXRl'
    'U3RhdGVSBnN0YXRlcw==');

@$core.Deprecated('Use blockMessageSenderRequestDescriptor instead')
const BlockMessageSenderRequest$json = {
  '1': 'BlockMessageSenderRequest',
  '2': [
    {'1': 'target_user_id', '3': 1, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `BlockMessageSenderRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List blockMessageSenderRequestDescriptor = $convert.base64Decode(
    'ChlCbG9ja01lc3NhZ2VTZW5kZXJSZXF1ZXN0EiQKDnRhcmdldF91c2VyX2lkGAEgASgJUgx0YX'
    'JnZXRVc2VySWQ=');

@$core.Deprecated('Use blockMessageSenderResponseDescriptor instead')
const BlockMessageSenderResponse$json = {
  '1': 'BlockMessageSenderResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'target_user_id', '3': 2, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `BlockMessageSenderResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List blockMessageSenderResponseDescriptor = $convert.base64Decode(
    'ChpCbG9ja01lc3NhZ2VTZW5kZXJSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW'
    '9uLkVycm9yQ29kZVIEY29kZRIkCg50YXJnZXRfdXNlcl9pZBgCIAEoCVIMdGFyZ2V0VXNlcklk');

@$core.Deprecated('Use unblockMessageSenderRequestDescriptor instead')
const UnblockMessageSenderRequest$json = {
  '1': 'UnblockMessageSenderRequest',
  '2': [
    {'1': 'target_user_id', '3': 1, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `UnblockMessageSenderRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unblockMessageSenderRequestDescriptor = $convert.base64Decode(
    'ChtVbmJsb2NrTWVzc2FnZVNlbmRlclJlcXVlc3QSJAoOdGFyZ2V0X3VzZXJfaWQYASABKAlSDH'
    'RhcmdldFVzZXJJZA==');

@$core.Deprecated('Use unblockMessageSenderResponseDescriptor instead')
const UnblockMessageSenderResponse$json = {
  '1': 'UnblockMessageSenderResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'target_user_id', '3': 2, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `UnblockMessageSenderResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unblockMessageSenderResponseDescriptor = $convert.base64Decode(
    'ChxVbmJsb2NrTWVzc2FnZVNlbmRlclJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb2'
    '1tb24uRXJyb3JDb2RlUgRjb2RlEiQKDnRhcmdldF91c2VyX2lkGAIgASgJUgx0YXJnZXRVc2Vy'
    'SWQ=');

@$core.Deprecated('Use getBlockedSendersRequestDescriptor instead')
const GetBlockedSendersRequest$json = {
  '1': 'GetBlockedSendersRequest',
};

/// Descriptor for `GetBlockedSendersRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getBlockedSendersRequestDescriptor = $convert.base64Decode(
    'ChhHZXRCbG9ja2VkU2VuZGVyc1JlcXVlc3Q=');

@$core.Deprecated('Use getBlockedSendersResponseDescriptor instead')
const GetBlockedSendersResponse$json = {
  '1': 'GetBlockedSendersResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'target_user_ids', '3': 2, '4': 3, '5': 9, '10': 'targetUserIds'},
  ],
};

/// Descriptor for `GetBlockedSendersResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getBlockedSendersResponseDescriptor = $convert.base64Decode(
    'ChlHZXRCbG9ja2VkU2VuZGVyc1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb2'
    '4uRXJyb3JDb2RlUgRjb2RlEiYKD3RhcmdldF91c2VyX2lkcxgCIAMoCVINdGFyZ2V0VXNlcklk'
    'cw==');

