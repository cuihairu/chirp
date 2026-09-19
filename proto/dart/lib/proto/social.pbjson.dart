//
//  Generated code. Do not modify.
//  source: proto/social.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use presenceStatusDescriptor instead')
const PresenceStatus$json = {
  '1': 'PresenceStatus',
  '2': [
    {'1': 'OFFLINE', '2': 0},
    {'1': 'ONLINE', '2': 1},
    {'1': 'AWAY', '2': 2},
    {'1': 'DND', '2': 3},
    {'1': 'IN_GAME', '2': 4},
    {'1': 'IN_BATTLE', '2': 5},
  ],
};

/// Descriptor for `PresenceStatus`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List presenceStatusDescriptor = $convert.base64Decode(
    'Cg5QcmVzZW5jZVN0YXR1cxILCgdPRkZMSU5FEAASCgoGT05MSU5FEAESCAoEQVdBWRACEgcKA0'
    'RORBADEgsKB0lOX0dBTUUQBBINCglJTl9CQVRUTEUQBQ==');

@$core.Deprecated('Use friendStatusDescriptor instead')
const FriendStatus$json = {
  '1': 'FriendStatus',
  '2': [
    {'1': 'NONE', '2': 0},
    {'1': 'PENDING', '2': 1},
    {'1': 'ACCEPTED', '2': 2},
    {'1': 'BLOCKED', '2': 3},
  ],
};

/// Descriptor for `FriendStatus`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List friendStatusDescriptor = $convert.base64Decode(
    'CgxGcmllbmRTdGF0dXMSCAoETk9ORRAAEgsKB1BFTkRJTkcQARIMCghBQ0NFUFRFRBACEgsKB0'
    'JMT0NLRUQQAw==');

@$core.Deprecated('Use friendRequestDescriptor instead')
const FriendRequest$json = {
  '1': 'FriendRequest',
  '2': [
    {'1': 'from_user_id', '3': 1, '4': 1, '5': 9, '10': 'fromUserId'},
    {'1': 'to_user_id', '3': 2, '4': 1, '5': 9, '10': 'toUserId'},
    {'1': 'message', '3': 3, '4': 1, '5': 9, '10': 'message'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
    {'1': 'request_id', '3': 5, '4': 1, '5': 9, '10': 'requestId'},
  ],
};

/// Descriptor for `FriendRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List friendRequestDescriptor = $convert.base64Decode(
    'Cg1GcmllbmRSZXF1ZXN0EiAKDGZyb21fdXNlcl9pZBgBIAEoCVIKZnJvbVVzZXJJZBIcCgp0b1'
    '91c2VyX2lkGAIgASgJUgh0b1VzZXJJZBIYCgdtZXNzYWdlGAMgASgJUgdtZXNzYWdlEhwKCXRp'
    'bWVzdGFtcBgEIAEoA1IJdGltZXN0YW1wEh0KCnJlcXVlc3RfaWQYBSABKAlSCXJlcXVlc3RJZA'
    '==');

@$core.Deprecated('Use friendInfoDescriptor instead')
const FriendInfo$json = {
  '1': 'FriendInfo',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'username', '3': 2, '4': 1, '5': 9, '10': 'username'},
    {'1': 'avatar_url', '3': 3, '4': 1, '5': 9, '10': 'avatarUrl'},
    {'1': 'status', '3': 4, '4': 1, '5': 14, '6': '.chirp.social.FriendStatus', '10': 'status'},
    {'1': 'added_at', '3': 5, '4': 1, '5': 3, '10': 'addedAt'},
  ],
};

/// Descriptor for `FriendInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List friendInfoDescriptor = $convert.base64Decode(
    'CgpGcmllbmRJbmZvEhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIaCgh1c2VybmFtZRgCIAEoCV'
    'IIdXNlcm5hbWUSHQoKYXZhdGFyX3VybBgDIAEoCVIJYXZhdGFyVXJsEjIKBnN0YXR1cxgEIAEo'
    'DjIaLmNoaXJwLnNvY2lhbC5GcmllbmRTdGF0dXNSBnN0YXR1cxIZCghhZGRlZF9hdBgFIAEoA1'
    'IHYWRkZWRBdA==');

@$core.Deprecated('Use addFriendRequestDescriptor instead')
const AddFriendRequest$json = {
  '1': 'AddFriendRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'target_user_id', '3': 2, '4': 1, '5': 9, '10': 'targetUserId'},
    {'1': 'message', '3': 3, '4': 1, '5': 9, '10': 'message'},
  ],
};

/// Descriptor for `AddFriendRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List addFriendRequestDescriptor = $convert.base64Decode(
    'ChBBZGRGcmllbmRSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIkCg50YXJnZXRfdX'
    'Nlcl9pZBgCIAEoCVIMdGFyZ2V0VXNlcklkEhgKB21lc3NhZ2UYAyABKAlSB21lc3NhZ2U=');

@$core.Deprecated('Use addFriendResponseDescriptor instead')
const AddFriendResponse$json = {
  '1': 'AddFriendResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'request_id', '3': 2, '4': 1, '5': 9, '10': 'requestId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `AddFriendResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List addFriendResponseDescriptor = $convert.base64Decode(
    'ChFBZGRGcmllbmRSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm9yQ2'
    '9kZVIEY29kZRIdCgpyZXF1ZXN0X2lkGAIgASgJUglyZXF1ZXN0SWQSHwoLc2VydmVyX3RpbWUY'
    'AyABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use friendRequestActionDescriptor instead')
const FriendRequestAction$json = {
  '1': 'FriendRequestAction',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'request_id', '3': 2, '4': 1, '5': 9, '10': 'requestId'},
    {'1': 'accept', '3': 3, '4': 1, '5': 8, '10': 'accept'},
  ],
};

/// Descriptor for `FriendRequestAction`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List friendRequestActionDescriptor = $convert.base64Decode(
    'ChNGcmllbmRSZXF1ZXN0QWN0aW9uEhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIdCgpyZXF1ZX'
    'N0X2lkGAIgASgJUglyZXF1ZXN0SWQSFgoGYWNjZXB0GAMgASgIUgZhY2NlcHQ=');

@$core.Deprecated('Use friendRequestActionResponseDescriptor instead')
const FriendRequestActionResponse$json = {
  '1': 'FriendRequestActionResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `FriendRequestActionResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List friendRequestActionResponseDescriptor = $convert.base64Decode(
    'ChtGcmllbmRSZXF1ZXN0QWN0aW9uUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW'
    '1vbi5FcnJvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use removeFriendRequestDescriptor instead')
const RemoveFriendRequest$json = {
  '1': 'RemoveFriendRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'friend_user_id', '3': 2, '4': 1, '5': 9, '10': 'friendUserId'},
  ],
};

/// Descriptor for `RemoveFriendRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List removeFriendRequestDescriptor = $convert.base64Decode(
    'ChNSZW1vdmVGcmllbmRSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIkCg5mcmllbm'
    'RfdXNlcl9pZBgCIAEoCVIMZnJpZW5kVXNlcklk');

@$core.Deprecated('Use removeFriendResponseDescriptor instead')
const RemoveFriendResponse$json = {
  '1': 'RemoveFriendResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `RemoveFriendResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List removeFriendResponseDescriptor = $convert.base64Decode(
    'ChRSZW1vdmVGcmllbmRSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIfCgtzZXJ2ZXJfdGltZRgCIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use getFriendListRequestDescriptor instead')
const GetFriendListRequest$json = {
  '1': 'GetFriendListRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'limit', '3': 2, '4': 1, '5': 5, '10': 'limit'},
    {'1': 'offset', '3': 3, '4': 1, '5': 5, '10': 'offset'},
  ],
};

/// Descriptor for `GetFriendListRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getFriendListRequestDescriptor = $convert.base64Decode(
    'ChRHZXRGcmllbmRMaXN0UmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSFAoFbGltaX'
    'QYAiABKAVSBWxpbWl0EhYKBm9mZnNldBgDIAEoBVIGb2Zmc2V0');

@$core.Deprecated('Use getFriendListResponseDescriptor instead')
const GetFriendListResponse$json = {
  '1': 'GetFriendListResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'friends', '3': 2, '4': 3, '5': 11, '6': '.chirp.social.FriendInfo', '10': 'friends'},
    {'1': 'total_count', '3': 3, '4': 1, '5': 5, '10': 'totalCount'},
  ],
};

/// Descriptor for `GetFriendListResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getFriendListResponseDescriptor = $convert.base64Decode(
    'ChVHZXRGcmllbmRMaXN0UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSMgoHZnJpZW5kcxgCIAMoCzIYLmNoaXJwLnNvY2lhbC5GcmllbmRJbmZv'
    'UgdmcmllbmRzEh8KC3RvdGFsX2NvdW50GAMgASgFUgp0b3RhbENvdW50');

@$core.Deprecated('Use getPendingRequestsRequestDescriptor instead')
const GetPendingRequestsRequest$json = {
  '1': 'GetPendingRequestsRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetPendingRequestsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPendingRequestsRequestDescriptor = $convert.base64Decode(
    'ChlHZXRQZW5kaW5nUmVxdWVzdHNSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZA==');

@$core.Deprecated('Use getPendingRequestsResponseDescriptor instead')
const GetPendingRequestsResponse$json = {
  '1': 'GetPendingRequestsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'requests', '3': 2, '4': 3, '5': 11, '6': '.chirp.social.FriendRequest', '10': 'requests'},
  ],
};

/// Descriptor for `GetPendingRequestsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPendingRequestsResponseDescriptor = $convert.base64Decode(
    'ChpHZXRQZW5kaW5nUmVxdWVzdHNSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW'
    '9uLkVycm9yQ29kZVIEY29kZRI3CghyZXF1ZXN0cxgCIAMoCzIbLmNoaXJwLnNvY2lhbC5Gcmll'
    'bmRSZXF1ZXN0UghyZXF1ZXN0cw==');

@$core.Deprecated('Use blockUserRequestDescriptor instead')
const BlockUserRequest$json = {
  '1': 'BlockUserRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'target_user_id', '3': 2, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `BlockUserRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List blockUserRequestDescriptor = $convert.base64Decode(
    'ChBCbG9ja1VzZXJSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIkCg50YXJnZXRfdX'
    'Nlcl9pZBgCIAEoCVIMdGFyZ2V0VXNlcklk');

@$core.Deprecated('Use blockUserResponseDescriptor instead')
const BlockUserResponse$json = {
  '1': 'BlockUserResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `BlockUserResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List blockUserResponseDescriptor = $convert.base64Decode(
    'ChFCbG9ja1VzZXJSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm9yQ2'
    '9kZVIEY29kZRIfCgtzZXJ2ZXJfdGltZRgCIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use unblockUserRequestDescriptor instead')
const UnblockUserRequest$json = {
  '1': 'UnblockUserRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'target_user_id', '3': 2, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `UnblockUserRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unblockUserRequestDescriptor = $convert.base64Decode(
    'ChJVbmJsb2NrVXNlclJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEiQKDnRhcmdldF'
    '91c2VyX2lkGAIgASgJUgx0YXJnZXRVc2VySWQ=');

@$core.Deprecated('Use unblockUserResponseDescriptor instead')
const UnblockUserResponse$json = {
  '1': 'UnblockUserResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `UnblockUserResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unblockUserResponseDescriptor = $convert.base64Decode(
    'ChNVbmJsb2NrVXNlclJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use getBlockedListRequestDescriptor instead')
const GetBlockedListRequest$json = {
  '1': 'GetBlockedListRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetBlockedListRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getBlockedListRequestDescriptor = $convert.base64Decode(
    'ChVHZXRCbG9ja2VkTGlzdFJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklk');

@$core.Deprecated('Use getBlockedListResponseDescriptor instead')
const GetBlockedListResponse$json = {
  '1': 'GetBlockedListResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'blocked_user_ids', '3': 2, '4': 3, '5': 9, '10': 'blockedUserIds'},
  ],
};

/// Descriptor for `GetBlockedListResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getBlockedListResponseDescriptor = $convert.base64Decode(
    'ChZHZXRCbG9ja2VkTGlzdFJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEigKEGJsb2NrZWRfdXNlcl9pZHMYAiADKAlSDmJsb2NrZWRVc2VySWRz');

@$core.Deprecated('Use setPresenceRequestDescriptor instead')
const SetPresenceRequest$json = {
  '1': 'SetPresenceRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'status', '3': 2, '4': 1, '5': 14, '6': '.chirp.social.PresenceStatus', '10': 'status'},
    {'1': 'status_message', '3': 3, '4': 1, '5': 9, '10': 'statusMessage'},
    {'1': 'metadata', '3': 4, '4': 3, '5': 11, '6': '.chirp.social.SetPresenceRequest.MetadataEntry', '10': 'metadata'},
  ],
  '3': [SetPresenceRequest_MetadataEntry$json],
};

@$core.Deprecated('Use setPresenceRequestDescriptor instead')
const SetPresenceRequest_MetadataEntry$json = {
  '1': 'MetadataEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `SetPresenceRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setPresenceRequestDescriptor = $convert.base64Decode(
    'ChJTZXRQcmVzZW5jZVJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEjQKBnN0YXR1cx'
    'gCIAEoDjIcLmNoaXJwLnNvY2lhbC5QcmVzZW5jZVN0YXR1c1IGc3RhdHVzEiUKDnN0YXR1c19t'
    'ZXNzYWdlGAMgASgJUg1zdGF0dXNNZXNzYWdlEkoKCG1ldGFkYXRhGAQgAygLMi4uY2hpcnAuc2'
    '9jaWFsLlNldFByZXNlbmNlUmVxdWVzdC5NZXRhZGF0YUVudHJ5UghtZXRhZGF0YRo7Cg1NZXRh'
    'ZGF0YUVudHJ5EhAKA2tleRgBIAEoCVIDa2V5EhQKBXZhbHVlGAIgASgJUgV2YWx1ZToCOAE=');

@$core.Deprecated('Use setPresenceResponseDescriptor instead')
const SetPresenceResponse$json = {
  '1': 'SetPresenceResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `SetPresenceResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setPresenceResponseDescriptor = $convert.base64Decode(
    'ChNTZXRQcmVzZW5jZVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use presenceInfoDescriptor instead')
const PresenceInfo$json = {
  '1': 'PresenceInfo',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'status', '3': 2, '4': 1, '5': 14, '6': '.chirp.social.PresenceStatus', '10': 'status'},
    {'1': 'status_message', '3': 3, '4': 1, '5': 9, '10': 'statusMessage'},
    {'1': 'last_seen', '3': 4, '4': 1, '5': 3, '10': 'lastSeen'},
    {'1': 'metadata', '3': 5, '4': 3, '5': 11, '6': '.chirp.social.PresenceInfo.MetadataEntry', '10': 'metadata'},
  ],
  '3': [PresenceInfo_MetadataEntry$json],
};

@$core.Deprecated('Use presenceInfoDescriptor instead')
const PresenceInfo_MetadataEntry$json = {
  '1': 'MetadataEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `PresenceInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List presenceInfoDescriptor = $convert.base64Decode(
    'CgxQcmVzZW5jZUluZm8SFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEjQKBnN0YXR1cxgCIAEoDj'
    'IcLmNoaXJwLnNvY2lhbC5QcmVzZW5jZVN0YXR1c1IGc3RhdHVzEiUKDnN0YXR1c19tZXNzYWdl'
    'GAMgASgJUg1zdGF0dXNNZXNzYWdlEhsKCWxhc3Rfc2VlbhgEIAEoA1IIbGFzdFNlZW4SRAoIbW'
    'V0YWRhdGEYBSADKAsyKC5jaGlycC5zb2NpYWwuUHJlc2VuY2VJbmZvLk1ldGFkYXRhRW50cnlS'
    'CG1ldGFkYXRhGjsKDU1ldGFkYXRhRW50cnkSEAoDa2V5GAEgASgJUgNrZXkSFAoFdmFsdWUYAi'
    'ABKAlSBXZhbHVlOgI4AQ==');

@$core.Deprecated('Use getPresenceRequestDescriptor instead')
const GetPresenceRequest$json = {
  '1': 'GetPresenceRequest',
  '2': [
    {'1': 'user_ids', '3': 1, '4': 3, '5': 9, '10': 'userIds'},
  ],
};

/// Descriptor for `GetPresenceRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPresenceRequestDescriptor = $convert.base64Decode(
    'ChJHZXRQcmVzZW5jZVJlcXVlc3QSGQoIdXNlcl9pZHMYASADKAlSB3VzZXJJZHM=');

@$core.Deprecated('Use getPresenceResponseDescriptor instead')
const GetPresenceResponse$json = {
  '1': 'GetPresenceResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'presences', '3': 2, '4': 3, '5': 11, '6': '.chirp.social.PresenceInfo', '10': 'presences'},
  ],
};

/// Descriptor for `GetPresenceResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPresenceResponseDescriptor = $convert.base64Decode(
    'ChNHZXRQcmVzZW5jZVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEjgKCXByZXNlbmNlcxgCIAMoCzIaLmNoaXJwLnNvY2lhbC5QcmVzZW5jZUlu'
    'Zm9SCXByZXNlbmNlcw==');

@$core.Deprecated('Use presenceNotifyDescriptor instead')
const PresenceNotify$json = {
  '1': 'PresenceNotify',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'status', '3': 2, '4': 1, '5': 14, '6': '.chirp.social.PresenceStatus', '10': 'status'},
    {'1': 'status_message', '3': 3, '4': 1, '5': 9, '10': 'statusMessage'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
    {'1': 'metadata', '3': 5, '4': 3, '5': 11, '6': '.chirp.social.PresenceNotify.MetadataEntry', '10': 'metadata'},
  ],
  '3': [PresenceNotify_MetadataEntry$json],
};

@$core.Deprecated('Use presenceNotifyDescriptor instead')
const PresenceNotify_MetadataEntry$json = {
  '1': 'MetadataEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `PresenceNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List presenceNotifyDescriptor = $convert.base64Decode(
    'Cg5QcmVzZW5jZU5vdGlmeRIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSNAoGc3RhdHVzGAIgAS'
    'gOMhwuY2hpcnAuc29jaWFsLlByZXNlbmNlU3RhdHVzUgZzdGF0dXMSJQoOc3RhdHVzX21lc3Nh'
    'Z2UYAyABKAlSDXN0YXR1c01lc3NhZ2USHAoJdGltZXN0YW1wGAQgASgDUgl0aW1lc3RhbXASRg'
    'oIbWV0YWRhdGEYBSADKAsyKi5jaGlycC5zb2NpYWwuUHJlc2VuY2VOb3RpZnkuTWV0YWRhdGFF'
    'bnRyeVIIbWV0YWRhdGEaOwoNTWV0YWRhdGFFbnRyeRIQCgNrZXkYASABKAlSA2tleRIUCgV2YW'
    'x1ZRgCIAEoCVIFdmFsdWU6AjgB');

@$core.Deprecated('Use friendRequestNotifyDescriptor instead')
const FriendRequestNotify$json = {
  '1': 'FriendRequestNotify',
  '2': [
    {'1': 'request_id', '3': 1, '4': 1, '5': 9, '10': 'requestId'},
    {'1': 'from_user_id', '3': 2, '4': 1, '5': 9, '10': 'fromUserId'},
    {'1': 'from_username', '3': 3, '4': 1, '5': 9, '10': 'fromUsername'},
    {'1': 'message', '3': 4, '4': 1, '5': 9, '10': 'message'},
    {'1': 'timestamp', '3': 5, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `FriendRequestNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List friendRequestNotifyDescriptor = $convert.base64Decode(
    'ChNGcmllbmRSZXF1ZXN0Tm90aWZ5Eh0KCnJlcXVlc3RfaWQYASABKAlSCXJlcXVlc3RJZBIgCg'
    'xmcm9tX3VzZXJfaWQYAiABKAlSCmZyb21Vc2VySWQSIwoNZnJvbV91c2VybmFtZRgDIAEoCVIM'
    'ZnJvbVVzZXJuYW1lEhgKB21lc3NhZ2UYBCABKAlSB21lc3NhZ2USHAoJdGltZXN0YW1wGAUgAS'
    'gDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use friendAcceptedNotifyDescriptor instead')
const FriendAcceptedNotify$json = {
  '1': 'FriendAcceptedNotify',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'username', '3': 2, '4': 1, '5': 9, '10': 'username'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `FriendAcceptedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List friendAcceptedNotifyDescriptor = $convert.base64Decode(
    'ChRGcmllbmRBY2NlcHRlZE5vdGlmeRIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGgoIdXNlcm'
    '5hbWUYAiABKAlSCHVzZXJuYW1lEhwKCXRpbWVzdGFtcBgDIAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use friendRemovedNotifyDescriptor instead')
const FriendRemovedNotify$json = {
  '1': 'FriendRemovedNotify',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'timestamp', '3': 2, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `FriendRemovedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List friendRemovedNotifyDescriptor = $convert.base64Decode(
    'ChNGcmllbmRSZW1vdmVkTm90aWZ5EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIcCgl0aW1lc3'
    'RhbXAYAiABKANSCXRpbWVzdGFtcA==');

@$core.Deprecated('Use storedFriendListDescriptor instead')
const StoredFriendList$json = {
  '1': 'StoredFriendList',
  '2': [
    {'1': 'friend_user_ids', '3': 1, '4': 3, '5': 9, '10': 'friendUserIds'},
  ],
};

/// Descriptor for `StoredFriendList`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedFriendListDescriptor = $convert.base64Decode(
    'ChBTdG9yZWRGcmllbmRMaXN0EiYKD2ZyaWVuZF91c2VyX2lkcxgBIAMoCVINZnJpZW5kVXNlck'
    'lkcw==');

@$core.Deprecated('Use storedPendingRequestsDescriptor instead')
const StoredPendingRequests$json = {
  '1': 'StoredPendingRequests',
  '2': [
    {'1': 'requests', '3': 1, '4': 3, '5': 11, '6': '.chirp.social.StoredPendingRequests.RequestsEntry', '10': 'requests'},
  ],
  '3': [StoredPendingRequests_RequestsEntry$json],
};

@$core.Deprecated('Use storedPendingRequestsDescriptor instead')
const StoredPendingRequests_RequestsEntry$json = {
  '1': 'RequestsEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 11, '6': '.chirp.social.FriendRequest', '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `StoredPendingRequests`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedPendingRequestsDescriptor = $convert.base64Decode(
    'ChVTdG9yZWRQZW5kaW5nUmVxdWVzdHMSTQoIcmVxdWVzdHMYASADKAsyMS5jaGlycC5zb2NpYW'
    'wuU3RvcmVkUGVuZGluZ1JlcXVlc3RzLlJlcXVlc3RzRW50cnlSCHJlcXVlc3RzGlgKDVJlcXVl'
    'c3RzRW50cnkSEAoDa2V5GAEgASgJUgNrZXkSMQoFdmFsdWUYAiABKAsyGy5jaGlycC5zb2NpYW'
    'wuRnJpZW5kUmVxdWVzdFIFdmFsdWU6AjgB');

@$core.Deprecated('Use storedBlockedListDescriptor instead')
const StoredBlockedList$json = {
  '1': 'StoredBlockedList',
  '2': [
    {'1': 'blocked_user_ids', '3': 1, '4': 3, '5': 9, '10': 'blockedUserIds'},
  ],
};

/// Descriptor for `StoredBlockedList`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedBlockedListDescriptor = $convert.base64Decode(
    'ChFTdG9yZWRCbG9ja2VkTGlzdBIoChBibG9ja2VkX3VzZXJfaWRzGAEgAygJUg5ibG9ja2VkVX'
    'Nlcklkcw==');

