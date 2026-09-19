//
//  Generated code. Do not modify.
//  source: proto/voice.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use roomTypeDescriptor instead')
const RoomType$json = {
  '1': 'RoomType',
  '2': [
    {'1': 'PEER_TO_PEER', '2': 0},
    {'1': 'GROUP', '2': 1},
    {'1': 'CHANNEL', '2': 2},
  ],
};

/// Descriptor for `RoomType`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List roomTypeDescriptor = $convert.base64Decode(
    'CghSb29tVHlwZRIQCgxQRUVSX1RPX1BFRVIQABIJCgVHUk9VUBABEgsKB0NIQU5ORUwQAg==');

@$core.Deprecated('Use participantStateDescriptor instead')
const ParticipantState$json = {
  '1': 'ParticipantState',
  '2': [
    {'1': 'JOINING', '2': 0},
    {'1': 'CONNECTED', '2': 1},
    {'1': 'MUTED', '2': 2},
    {'1': 'DEAFENED', '2': 3},
    {'1': 'DISCONNECTED', '2': 4},
  ],
};

/// Descriptor for `ParticipantState`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List participantStateDescriptor = $convert.base64Decode(
    'ChBQYXJ0aWNpcGFudFN0YXRlEgsKB0pPSU5JTkcQABINCglDT05ORUNURUQQARIJCgVNVVRFRB'
    'ACEgwKCERFQUZFTkVEEAMSEAoMRElTQ09OTkVDVEVEEAQ=');

@$core.Deprecated('Use createRoomRequestDescriptor instead')
const CreateRoomRequest$json = {
  '1': 'CreateRoomRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'room_type', '3': 2, '4': 1, '5': 14, '6': '.chirp.voice.RoomType', '10': 'roomType'},
    {'1': 'room_name', '3': 3, '4': 1, '5': 9, '10': 'roomName'},
    {'1': 'max_participants', '3': 4, '4': 1, '5': 5, '10': 'maxParticipants'},
    {'1': 'metadata', '3': 5, '4': 3, '5': 11, '6': '.chirp.voice.CreateRoomRequest.MetadataEntry', '10': 'metadata'},
  ],
  '3': [CreateRoomRequest_MetadataEntry$json],
};

@$core.Deprecated('Use createRoomRequestDescriptor instead')
const CreateRoomRequest_MetadataEntry$json = {
  '1': 'MetadataEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `CreateRoomRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createRoomRequestDescriptor = $convert.base64Decode(
    'ChFDcmVhdGVSb29tUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSMgoJcm9vbV90eX'
    'BlGAIgASgOMhUuY2hpcnAudm9pY2UuUm9vbVR5cGVSCHJvb21UeXBlEhsKCXJvb21fbmFtZRgD'
    'IAEoCVIIcm9vbU5hbWUSKQoQbWF4X3BhcnRpY2lwYW50cxgEIAEoBVIPbWF4UGFydGljaXBhbn'
    'RzEkgKCG1ldGFkYXRhGAUgAygLMiwuY2hpcnAudm9pY2UuQ3JlYXRlUm9vbVJlcXVlc3QuTWV0'
    'YWRhdGFFbnRyeVIIbWV0YWRhdGEaOwoNTWV0YWRhdGFFbnRyeRIQCgNrZXkYASABKAlSA2tleR'
    'IUCgV2YWx1ZRgCIAEoCVIFdmFsdWU6AjgB');

@$core.Deprecated('Use createRoomResponseDescriptor instead')
const CreateRoomResponse$json = {
  '1': 'CreateRoomResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `CreateRoomResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createRoomResponseDescriptor = $convert.base64Decode(
    'ChJDcmVhdGVSb29tUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSFwoHcm9vbV9pZBgCIAEoCVIGcm9vbUlkEh8KC3NlcnZlcl90aW1lGAMgASgD'
    'UgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use iceServerDescriptor instead')
const IceServer$json = {
  '1': 'IceServer',
  '2': [
    {'1': 'urls', '3': 1, '4': 3, '5': 9, '10': 'urls'},
    {'1': 'username', '3': 2, '4': 1, '5': 9, '10': 'username'},
    {'1': 'credential', '3': 3, '4': 1, '5': 9, '10': 'credential'},
  ],
};

/// Descriptor for `IceServer`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List iceServerDescriptor = $convert.base64Decode(
    'CglJY2VTZXJ2ZXISEgoEdXJscxgBIAMoCVIEdXJscxIaCgh1c2VybmFtZRgCIAEoCVIIdXNlcm'
    '5hbWUSHgoKY3JlZGVudGlhbBgDIAEoCVIKY3JlZGVudGlhbA==');

@$core.Deprecated('Use joinRoomRequestDescriptor instead')
const JoinRoomRequest$json = {
  '1': 'JoinRoomRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'sdp_offer', '3': 3, '4': 1, '5': 9, '10': 'sdpOffer'},
  ],
};

/// Descriptor for `JoinRoomRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List joinRoomRequestDescriptor = $convert.base64Decode(
    'Cg9Kb2luUm9vbVJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEhcKB3Jvb21faWQYAi'
    'ABKAlSBnJvb21JZBIbCglzZHBfb2ZmZXIYAyABKAlSCHNkcE9mZmVy');

@$core.Deprecated('Use joinRoomResponseDescriptor instead')
const JoinRoomResponse$json = {
  '1': 'JoinRoomResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'sdp_answer', '3': 3, '4': 1, '5': 9, '10': 'sdpAnswer'},
    {'1': 'participant_ids', '3': 4, '4': 3, '5': 9, '10': 'participantIds'},
    {'1': 'server_time', '3': 5, '4': 1, '5': 3, '10': 'serverTime'},
    {'1': 'ice_servers', '3': 6, '4': 3, '5': 11, '6': '.chirp.voice.IceServer', '10': 'iceServers'},
  ],
};

/// Descriptor for `JoinRoomResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List joinRoomResponseDescriptor = $convert.base64Decode(
    'ChBKb2luUm9vbVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3JDb2'
    'RlUgRjb2RlEhcKB3Jvb21faWQYAiABKAlSBnJvb21JZBIdCgpzZHBfYW5zd2VyGAMgASgJUglz'
    'ZHBBbnN3ZXISJwoPcGFydGljaXBhbnRfaWRzGAQgAygJUg5wYXJ0aWNpcGFudElkcxIfCgtzZX'
    'J2ZXJfdGltZRgFIAEoA1IKc2VydmVyVGltZRI3CgtpY2Vfc2VydmVycxgGIAMoCzIWLmNoaXJw'
    'LnZvaWNlLkljZVNlcnZlclIKaWNlU2VydmVycw==');

@$core.Deprecated('Use leaveRoomRequestDescriptor instead')
const LeaveRoomRequest$json = {
  '1': 'LeaveRoomRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
  ],
};

/// Descriptor for `LeaveRoomRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List leaveRoomRequestDescriptor = $convert.base64Decode(
    'ChBMZWF2ZVJvb21SZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIXCgdyb29tX2lkGA'
    'IgASgJUgZyb29tSWQ=');

@$core.Deprecated('Use leaveRoomResponseDescriptor instead')
const LeaveRoomResponse$json = {
  '1': 'LeaveRoomResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `LeaveRoomResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List leaveRoomResponseDescriptor = $convert.base64Decode(
    'ChFMZWF2ZVJvb21SZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm9yQ2'
    '9kZVIEY29kZRIfCgtzZXJ2ZXJfdGltZRgCIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use iceCandidateDescriptor instead')
const IceCandidate$json = {
  '1': 'IceCandidate',
  '2': [
    {'1': 'candidate', '3': 1, '4': 1, '5': 9, '10': 'candidate'},
    {'1': 'sdp_mid', '3': 2, '4': 1, '5': 9, '10': 'sdpMid'},
    {'1': 'sdp_mline_index', '3': 3, '4': 1, '5': 5, '10': 'sdpMlineIndex'},
  ],
};

/// Descriptor for `IceCandidate`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List iceCandidateDescriptor = $convert.base64Decode(
    'CgxJY2VDYW5kaWRhdGUSHAoJY2FuZGlkYXRlGAEgASgJUgljYW5kaWRhdGUSFwoHc2RwX21pZB'
    'gCIAEoCVIGc2RwTWlkEiYKD3NkcF9tbGluZV9pbmRleBgDIAEoBVINc2RwTWxpbmVJbmRleA==');

@$core.Deprecated('Use iceCandidateMessageDescriptor instead')
const IceCandidateMessage$json = {
  '1': 'IceCandidateMessage',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'from_user_id', '3': 2, '4': 1, '5': 9, '10': 'fromUserId'},
    {'1': 'to_user_id', '3': 3, '4': 1, '5': 9, '10': 'toUserId'},
    {'1': 'candidate', '3': 4, '4': 1, '5': 11, '6': '.chirp.voice.IceCandidate', '10': 'candidate'},
  ],
};

/// Descriptor for `IceCandidateMessage`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List iceCandidateMessageDescriptor = $convert.base64Decode(
    'ChNJY2VDYW5kaWRhdGVNZXNzYWdlEhcKB3Jvb21faWQYASABKAlSBnJvb21JZBIgCgxmcm9tX3'
    'VzZXJfaWQYAiABKAlSCmZyb21Vc2VySWQSHAoKdG9fdXNlcl9pZBgDIAEoCVIIdG9Vc2VySWQS'
    'NwoJY2FuZGlkYXRlGAQgASgLMhkuY2hpcnAudm9pY2UuSWNlQ2FuZGlkYXRlUgljYW5kaWRhdG'
    'U=');

@$core.Deprecated('Use sdpOfferMessageDescriptor instead')
const SdpOfferMessage$json = {
  '1': 'SdpOfferMessage',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'from_user_id', '3': 2, '4': 1, '5': 9, '10': 'fromUserId'},
    {'1': 'to_user_id', '3': 3, '4': 1, '5': 9, '10': 'toUserId'},
    {'1': 'sdp_offer', '3': 4, '4': 1, '5': 9, '10': 'sdpOffer'},
  ],
};

/// Descriptor for `SdpOfferMessage`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List sdpOfferMessageDescriptor = $convert.base64Decode(
    'Cg9TZHBPZmZlck1lc3NhZ2USFwoHcm9vbV9pZBgBIAEoCVIGcm9vbUlkEiAKDGZyb21fdXNlcl'
    '9pZBgCIAEoCVIKZnJvbVVzZXJJZBIcCgp0b191c2VyX2lkGAMgASgJUgh0b1VzZXJJZBIbCglz'
    'ZHBfb2ZmZXIYBCABKAlSCHNkcE9mZmVy');

@$core.Deprecated('Use sdpAnswerMessageDescriptor instead')
const SdpAnswerMessage$json = {
  '1': 'SdpAnswerMessage',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'from_user_id', '3': 2, '4': 1, '5': 9, '10': 'fromUserId'},
    {'1': 'to_user_id', '3': 3, '4': 1, '5': 9, '10': 'toUserId'},
    {'1': 'sdp_answer', '3': 4, '4': 1, '5': 9, '10': 'sdpAnswer'},
  ],
};

/// Descriptor for `SdpAnswerMessage`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List sdpAnswerMessageDescriptor = $convert.base64Decode(
    'ChBTZHBBbnN3ZXJNZXNzYWdlEhcKB3Jvb21faWQYASABKAlSBnJvb21JZBIgCgxmcm9tX3VzZX'
    'JfaWQYAiABKAlSCmZyb21Vc2VySWQSHAoKdG9fdXNlcl9pZBgDIAEoCVIIdG9Vc2VySWQSHQoK'
    'c2RwX2Fuc3dlchgEIAEoCVIJc2RwQW5zd2Vy');

@$core.Deprecated('Use participantInfoDescriptor instead')
const ParticipantInfo$json = {
  '1': 'ParticipantInfo',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'username', '3': 2, '4': 1, '5': 9, '10': 'username'},
    {'1': 'state', '3': 3, '4': 1, '5': 14, '6': '.chirp.voice.ParticipantState', '10': 'state'},
    {'1': 'joined_at', '3': 4, '4': 1, '5': 3, '10': 'joinedAt'},
    {'1': 'is_speaking', '3': 5, '4': 1, '5': 8, '10': 'isSpeaking'},
    {'1': 'muted', '3': 6, '4': 1, '5': 8, '10': 'muted'},
    {'1': 'deafened', '3': 7, '4': 1, '5': 8, '10': 'deafened'},
  ],
};

/// Descriptor for `ParticipantInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List participantInfoDescriptor = $convert.base64Decode(
    'Cg9QYXJ0aWNpcGFudEluZm8SFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEhoKCHVzZXJuYW1lGA'
    'IgASgJUgh1c2VybmFtZRIzCgVzdGF0ZRgDIAEoDjIdLmNoaXJwLnZvaWNlLlBhcnRpY2lwYW50'
    'U3RhdGVSBXN0YXRlEhsKCWpvaW5lZF9hdBgEIAEoA1IIam9pbmVkQXQSHwoLaXNfc3BlYWtpbm'
    'cYBSABKAhSCmlzU3BlYWtpbmcSFAoFbXV0ZWQYBiABKAhSBW11dGVkEhoKCGRlYWZlbmVkGAcg'
    'ASgIUghkZWFmZW5lZA==');

@$core.Deprecated('Use getRoomInfoRequestDescriptor instead')
const GetRoomInfoRequest$json = {
  '1': 'GetRoomInfoRequest',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
  ],
};

/// Descriptor for `GetRoomInfoRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getRoomInfoRequestDescriptor = $convert.base64Decode(
    'ChJHZXRSb29tSW5mb1JlcXVlc3QSFwoHcm9vbV9pZBgBIAEoCVIGcm9vbUlk');

@$core.Deprecated('Use getRoomInfoResponseDescriptor instead')
const GetRoomInfoResponse$json = {
  '1': 'GetRoomInfoResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'room_name', '3': 3, '4': 1, '5': 9, '10': 'roomName'},
    {'1': 'room_type', '3': 4, '4': 1, '5': 14, '6': '.chirp.voice.RoomType', '10': 'roomType'},
    {'1': 'participants', '3': 5, '4': 3, '5': 11, '6': '.chirp.voice.ParticipantInfo', '10': 'participants'},
    {'1': 'max_participants', '3': 6, '4': 1, '5': 5, '10': 'maxParticipants'},
  ],
};

/// Descriptor for `GetRoomInfoResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getRoomInfoResponseDescriptor = $convert.base64Decode(
    'ChNHZXRSb29tSW5mb1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEhcKB3Jvb21faWQYAiABKAlSBnJvb21JZBIbCglyb29tX25hbWUYAyABKAlS'
    'CHJvb21OYW1lEjIKCXJvb21fdHlwZRgEIAEoDjIVLmNoaXJwLnZvaWNlLlJvb21UeXBlUghyb2'
    '9tVHlwZRJACgxwYXJ0aWNpcGFudHMYBSADKAsyHC5jaGlycC52b2ljZS5QYXJ0aWNpcGFudElu'
    'Zm9SDHBhcnRpY2lwYW50cxIpChBtYXhfcGFydGljaXBhbnRzGAYgASgFUg9tYXhQYXJ0aWNpcG'
    'FudHM=');

@$core.Deprecated('Use getUserRoomRequestDescriptor instead')
const GetUserRoomRequest$json = {
  '1': 'GetUserRoomRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetUserRoomRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUserRoomRequestDescriptor = $convert.base64Decode(
    'ChJHZXRVc2VyUm9vbVJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklk');

@$core.Deprecated('Use getUserRoomResponseDescriptor instead')
const GetUserRoomResponse$json = {
  '1': 'GetUserRoomResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'participant', '3': 3, '4': 1, '5': 11, '6': '.chirp.voice.ParticipantInfo', '10': 'participant'},
  ],
};

/// Descriptor for `GetUserRoomResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUserRoomResponseDescriptor = $convert.base64Decode(
    'ChNHZXRVc2VyUm9vbVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEhcKB3Jvb21faWQYAiABKAlSBnJvb21JZBI+CgtwYXJ0aWNpcGFudBgDIAEo'
    'CzIcLmNoaXJwLnZvaWNlLlBhcnRpY2lwYW50SW5mb1ILcGFydGljaXBhbnQ=');

@$core.Deprecated('Use setMuteRequestDescriptor instead')
const SetMuteRequest$json = {
  '1': 'SetMuteRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'muted', '3': 3, '4': 1, '5': 8, '10': 'muted'},
  ],
};

/// Descriptor for `SetMuteRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setMuteRequestDescriptor = $convert.base64Decode(
    'Cg5TZXRNdXRlUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSFwoHcm9vbV9pZBgCIA'
    'EoCVIGcm9vbUlkEhQKBW11dGVkGAMgASgIUgVtdXRlZA==');

@$core.Deprecated('Use setMuteResponseDescriptor instead')
const SetMuteResponse$json = {
  '1': 'SetMuteResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `SetMuteResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setMuteResponseDescriptor = $convert.base64Decode(
    'Cg9TZXRNdXRlUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvckNvZG'
    'VSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use setDeafenRequestDescriptor instead')
const SetDeafenRequest$json = {
  '1': 'SetDeafenRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'room_id', '3': 2, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'deafened', '3': 3, '4': 1, '5': 8, '10': 'deafened'},
  ],
};

/// Descriptor for `SetDeafenRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setDeafenRequestDescriptor = $convert.base64Decode(
    'ChBTZXREZWFmZW5SZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIXCgdyb29tX2lkGA'
    'IgASgJUgZyb29tSWQSGgoIZGVhZmVuZWQYAyABKAhSCGRlYWZlbmVk');

@$core.Deprecated('Use setDeafenResponseDescriptor instead')
const SetDeafenResponse$json = {
  '1': 'SetDeafenResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `SetDeafenResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setDeafenResponseDescriptor = $convert.base64Decode(
    'ChFTZXREZWFmZW5SZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm9yQ2'
    '9kZVIEY29kZRIfCgtzZXJ2ZXJfdGltZRgCIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use participantJoinedNotifyDescriptor instead')
const ParticipantJoinedNotify$json = {
  '1': 'ParticipantJoinedNotify',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'participant', '3': 2, '4': 1, '5': 11, '6': '.chirp.voice.ParticipantInfo', '10': 'participant'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ParticipantJoinedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List participantJoinedNotifyDescriptor = $convert.base64Decode(
    'ChdQYXJ0aWNpcGFudEpvaW5lZE5vdGlmeRIXCgdyb29tX2lkGAEgASgJUgZyb29tSWQSPgoLcG'
    'FydGljaXBhbnQYAiABKAsyHC5jaGlycC52b2ljZS5QYXJ0aWNpcGFudEluZm9SC3BhcnRpY2lw'
    'YW50EhwKCXRpbWVzdGFtcBgDIAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use participantLeftNotifyDescriptor instead')
const ParticipantLeftNotify$json = {
  '1': 'ParticipantLeftNotify',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ParticipantLeftNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List participantLeftNotifyDescriptor = $convert.base64Decode(
    'ChVQYXJ0aWNpcGFudExlZnROb3RpZnkSFwoHcm9vbV9pZBgBIAEoCVIGcm9vbUlkEhcKB3VzZX'
    'JfaWQYAiABKAlSBnVzZXJJZBIcCgl0aW1lc3RhbXAYAyABKANSCXRpbWVzdGFtcA==');

@$core.Deprecated('Use participantStateChangedNotifyDescriptor instead')
const ParticipantStateChangedNotify$json = {
  '1': 'ParticipantStateChangedNotify',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'state', '3': 3, '4': 1, '5': 14, '6': '.chirp.voice.ParticipantState', '10': 'state'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `ParticipantStateChangedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List participantStateChangedNotifyDescriptor = $convert.base64Decode(
    'Ch1QYXJ0aWNpcGFudFN0YXRlQ2hhbmdlZE5vdGlmeRIXCgdyb29tX2lkGAEgASgJUgZyb29tSW'
    'QSFwoHdXNlcl9pZBgCIAEoCVIGdXNlcklkEjMKBXN0YXRlGAMgASgOMh0uY2hpcnAudm9pY2Uu'
    'UGFydGljaXBhbnRTdGF0ZVIFc3RhdGUSHAoJdGltZXN0YW1wGAQgASgDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use speakingNotifyDescriptor instead')
const SpeakingNotify$json = {
  '1': 'SpeakingNotify',
  '2': [
    {'1': 'room_id', '3': 1, '4': 1, '5': 9, '10': 'roomId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'speaking', '3': 3, '4': 1, '5': 8, '10': 'speaking'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `SpeakingNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List speakingNotifyDescriptor = $convert.base64Decode(
    'Cg5TcGVha2luZ05vdGlmeRIXCgdyb29tX2lkGAEgASgJUgZyb29tSWQSFwoHdXNlcl9pZBgCIA'
    'EoCVIGdXNlcklkEhoKCHNwZWFraW5nGAMgASgIUghzcGVha2luZxIcCgl0aW1lc3RhbXAYBCAB'
    'KANSCXRpbWVzdGFtcA==');

