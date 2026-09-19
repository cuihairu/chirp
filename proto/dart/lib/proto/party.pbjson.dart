//
//  Generated code. Do not modify.
//  source: proto/party.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use partyMemberDescriptor instead')
const PartyMember$json = {
  '1': 'PartyMember',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'ready', '3': 2, '4': 1, '5': 8, '10': 'ready'},
    {'1': 'joined_at', '3': 3, '4': 1, '5': 3, '10': 'joinedAt'},
  ],
};

/// Descriptor for `PartyMember`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List partyMemberDescriptor = $convert.base64Decode(
    'CgtQYXJ0eU1lbWJlchIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSFAoFcmVhZHkYAiABKAhSBX'
    'JlYWR5EhsKCWpvaW5lZF9hdBgDIAEoA1IIam9pbmVkQXQ=');

@$core.Deprecated('Use partyInfoDescriptor instead')
const PartyInfo$json = {
  '1': 'PartyInfo',
  '2': [
    {'1': 'party_id', '3': 1, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'leader_id', '3': 2, '4': 1, '5': 9, '10': 'leaderId'},
    {'1': 'max_members', '3': 3, '4': 1, '5': 5, '10': 'maxMembers'},
    {'1': 'members', '3': 4, '4': 3, '5': 11, '6': '.chirp.party.PartyMember', '10': 'members'},
    {'1': 'created_at', '3': 5, '4': 1, '5': 3, '10': 'createdAt'},
  ],
};

/// Descriptor for `PartyInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List partyInfoDescriptor = $convert.base64Decode(
    'CglQYXJ0eUluZm8SGQoIcGFydHlfaWQYASABKAlSB3BhcnR5SWQSGwoJbGVhZGVyX2lkGAIgAS'
    'gJUghsZWFkZXJJZBIfCgttYXhfbWVtYmVycxgDIAEoBVIKbWF4TWVtYmVycxIyCgdtZW1iZXJz'
    'GAQgAygLMhguY2hpcnAucGFydHkuUGFydHlNZW1iZXJSB21lbWJlcnMSHQoKY3JlYXRlZF9hdB'
    'gFIAEoA1IJY3JlYXRlZEF0');

@$core.Deprecated('Use createPartyRequestDescriptor instead')
const CreatePartyRequest$json = {
  '1': 'CreatePartyRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'max_members', '3': 2, '4': 1, '5': 5, '10': 'maxMembers'},
  ],
};

/// Descriptor for `CreatePartyRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createPartyRequestDescriptor = $convert.base64Decode(
    'ChJDcmVhdGVQYXJ0eVJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEh8KC21heF9tZW'
    '1iZXJzGAIgASgFUgptYXhNZW1iZXJz');

@$core.Deprecated('Use createPartyResponseDescriptor instead')
const CreatePartyResponse$json = {
  '1': 'CreatePartyResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'party', '3': 2, '4': 1, '5': 11, '6': '.chirp.party.PartyInfo', '10': 'party'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `CreatePartyResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List createPartyResponseDescriptor = $convert.base64Decode(
    'ChNDcmVhdGVQYXJ0eVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEiwKBXBhcnR5GAIgASgLMhYuY2hpcnAucGFydHkuUGFydHlJbmZvUgVwYXJ0'
    'eRIfCgtzZXJ2ZXJfdGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use disbandPartyRequestDescriptor instead')
const DisbandPartyRequest$json = {
  '1': 'DisbandPartyRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'party_id', '3': 2, '4': 1, '5': 9, '10': 'partyId'},
  ],
};

/// Descriptor for `DisbandPartyRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List disbandPartyRequestDescriptor = $convert.base64Decode(
    'ChNEaXNiYW5kUGFydHlSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIZCghwYXJ0eV'
    '9pZBgCIAEoCVIHcGFydHlJZA==');

@$core.Deprecated('Use disbandPartyResponseDescriptor instead')
const DisbandPartyResponse$json = {
  '1': 'DisbandPartyResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `DisbandPartyResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List disbandPartyResponseDescriptor = $convert.base64Decode(
    'ChREaXNiYW5kUGFydHlSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIfCgtzZXJ2ZXJfdGltZRgCIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use inviteToPartyRequestDescriptor instead')
const InviteToPartyRequest$json = {
  '1': 'InviteToPartyRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'party_id', '3': 2, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'target_user_id', '3': 3, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `InviteToPartyRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List inviteToPartyRequestDescriptor = $convert.base64Decode(
    'ChRJbnZpdGVUb1BhcnR5UmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGQoIcGFydH'
    'lfaWQYAiABKAlSB3BhcnR5SWQSJAoOdGFyZ2V0X3VzZXJfaWQYAyABKAlSDHRhcmdldFVzZXJJ'
    'ZA==');

@$core.Deprecated('Use inviteToPartyResponseDescriptor instead')
const InviteToPartyResponse$json = {
  '1': 'InviteToPartyResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'invite_id', '3': 2, '4': 1, '5': 9, '10': 'inviteId'},
    {'1': 'expires_at', '3': 3, '4': 1, '5': 3, '10': 'expiresAt'},
    {'1': 'server_time', '3': 4, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `InviteToPartyResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List inviteToPartyResponseDescriptor = $convert.base64Decode(
    'ChVJbnZpdGVUb1BhcnR5UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSGwoJaW52aXRlX2lkGAIgASgJUghpbnZpdGVJZBIdCgpleHBpcmVzX2F0'
    'GAMgASgDUglleHBpcmVzQXQSHwoLc2VydmVyX3RpbWUYBCABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use acceptInviteRequestDescriptor instead')
const AcceptInviteRequest$json = {
  '1': 'AcceptInviteRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'invite_id', '3': 2, '4': 1, '5': 9, '10': 'inviteId'},
  ],
};

/// Descriptor for `AcceptInviteRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List acceptInviteRequestDescriptor = $convert.base64Decode(
    'ChNBY2NlcHRJbnZpdGVSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIbCglpbnZpdG'
    'VfaWQYAiABKAlSCGludml0ZUlk');

@$core.Deprecated('Use acceptInviteResponseDescriptor instead')
const AcceptInviteResponse$json = {
  '1': 'AcceptInviteResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'party', '3': 2, '4': 1, '5': 11, '6': '.chirp.party.PartyInfo', '10': 'party'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `AcceptInviteResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List acceptInviteResponseDescriptor = $convert.base64Decode(
    'ChRBY2NlcHRJbnZpdGVSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIsCgVwYXJ0eRgCIAEoCzIWLmNoaXJwLnBhcnR5LlBhcnR5SW5mb1IFcGFy'
    'dHkSHwoLc2VydmVyX3RpbWUYAyABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use declineInviteRequestDescriptor instead')
const DeclineInviteRequest$json = {
  '1': 'DeclineInviteRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'invite_id', '3': 2, '4': 1, '5': 9, '10': 'inviteId'},
  ],
};

/// Descriptor for `DeclineInviteRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List declineInviteRequestDescriptor = $convert.base64Decode(
    'ChREZWNsaW5lSW52aXRlUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGwoJaW52aX'
    'RlX2lkGAIgASgJUghpbnZpdGVJZA==');

@$core.Deprecated('Use declineInviteResponseDescriptor instead')
const DeclineInviteResponse$json = {
  '1': 'DeclineInviteResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `DeclineInviteResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List declineInviteResponseDescriptor = $convert.base64Decode(
    'ChVEZWNsaW5lSW52aXRlUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use leavePartyRequestDescriptor instead')
const LeavePartyRequest$json = {
  '1': 'LeavePartyRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'party_id', '3': 2, '4': 1, '5': 9, '10': 'partyId'},
  ],
};

/// Descriptor for `LeavePartyRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List leavePartyRequestDescriptor = $convert.base64Decode(
    'ChFMZWF2ZVBhcnR5UmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGQoIcGFydHlfaW'
    'QYAiABKAlSB3BhcnR5SWQ=');

@$core.Deprecated('Use leavePartyResponseDescriptor instead')
const LeavePartyResponse$json = {
  '1': 'LeavePartyResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'party_disbanded', '3': 2, '4': 1, '5': 8, '10': 'partyDisbanded'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `LeavePartyResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List leavePartyResponseDescriptor = $convert.base64Decode(
    'ChJMZWF2ZVBhcnR5UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSJwoPcGFydHlfZGlzYmFuZGVkGAIgASgIUg5wYXJ0eURpc2JhbmRlZBIfCgtz'
    'ZXJ2ZXJfdGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use kickMemberRequestDescriptor instead')
const KickMemberRequest$json = {
  '1': 'KickMemberRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'party_id', '3': 2, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'target_user_id', '3': 3, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `KickMemberRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List kickMemberRequestDescriptor = $convert.base64Decode(
    'ChFLaWNrTWVtYmVyUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGQoIcGFydHlfaW'
    'QYAiABKAlSB3BhcnR5SWQSJAoOdGFyZ2V0X3VzZXJfaWQYAyABKAlSDHRhcmdldFVzZXJJZA==');

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

@$core.Deprecated('Use transferLeaderRequestDescriptor instead')
const TransferLeaderRequest$json = {
  '1': 'TransferLeaderRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'party_id', '3': 2, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'target_user_id', '3': 3, '4': 1, '5': 9, '10': 'targetUserId'},
  ],
};

/// Descriptor for `TransferLeaderRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List transferLeaderRequestDescriptor = $convert.base64Decode(
    'ChVUcmFuc2ZlckxlYWRlclJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEhkKCHBhcn'
    'R5X2lkGAIgASgJUgdwYXJ0eUlkEiQKDnRhcmdldF91c2VyX2lkGAMgASgJUgx0YXJnZXRVc2Vy'
    'SWQ=');

@$core.Deprecated('Use transferLeaderResponseDescriptor instead')
const TransferLeaderResponse$json = {
  '1': 'TransferLeaderResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'leader_id', '3': 2, '4': 1, '5': 9, '10': 'leaderId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `TransferLeaderResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List transferLeaderResponseDescriptor = $convert.base64Decode(
    'ChZUcmFuc2ZlckxlYWRlclJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEhsKCWxlYWRlcl9pZBgCIAEoCVIIbGVhZGVySWQSHwoLc2VydmVyX3Rp'
    'bWUYAyABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use setReadyRequestDescriptor instead')
const SetReadyRequest$json = {
  '1': 'SetReadyRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'party_id', '3': 2, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'ready', '3': 3, '4': 1, '5': 8, '10': 'ready'},
  ],
};

/// Descriptor for `SetReadyRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setReadyRequestDescriptor = $convert.base64Decode(
    'Cg9TZXRSZWFkeVJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEhkKCHBhcnR5X2lkGA'
    'IgASgJUgdwYXJ0eUlkEhQKBXJlYWR5GAMgASgIUgVyZWFkeQ==');

@$core.Deprecated('Use setReadyResponseDescriptor instead')
const SetReadyResponse$json = {
  '1': 'SetReadyResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `SetReadyResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setReadyResponseDescriptor = $convert.base64Decode(
    'ChBTZXRSZWFkeVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3JDb2'
    'RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use getMyPartyRequestDescriptor instead')
const GetMyPartyRequest$json = {
  '1': 'GetMyPartyRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetMyPartyRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getMyPartyRequestDescriptor = $convert.base64Decode(
    'ChFHZXRNeVBhcnR5UmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQ=');

@$core.Deprecated('Use getMyPartyResponseDescriptor instead')
const GetMyPartyResponse$json = {
  '1': 'GetMyPartyResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'in_party', '3': 2, '4': 1, '5': 8, '10': 'inParty'},
    {'1': 'party', '3': 3, '4': 1, '5': 11, '6': '.chirp.party.PartyInfo', '10': 'party'},
  ],
};

/// Descriptor for `GetMyPartyResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getMyPartyResponseDescriptor = $convert.base64Decode(
    'ChJHZXRNeVBhcnR5UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSGQoIaW5fcGFydHkYAiABKAhSB2luUGFydHkSLAoFcGFydHkYAyABKAsyFi5j'
    'aGlycC5wYXJ0eS5QYXJ0eUluZm9SBXBhcnR5');

@$core.Deprecated('Use inviteNotifyDescriptor instead')
const InviteNotify$json = {
  '1': 'InviteNotify',
  '2': [
    {'1': 'invite_id', '3': 1, '4': 1, '5': 9, '10': 'inviteId'},
    {'1': 'from_user_id', '3': 2, '4': 1, '5': 9, '10': 'fromUserId'},
    {'1': 'party', '3': 3, '4': 1, '5': 11, '6': '.chirp.party.PartyInfo', '10': 'party'},
    {'1': 'expires_at', '3': 4, '4': 1, '5': 3, '10': 'expiresAt'},
    {'1': 'timestamp', '3': 5, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `InviteNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List inviteNotifyDescriptor = $convert.base64Decode(
    'CgxJbnZpdGVOb3RpZnkSGwoJaW52aXRlX2lkGAEgASgJUghpbnZpdGVJZBIgCgxmcm9tX3VzZX'
    'JfaWQYAiABKAlSCmZyb21Vc2VySWQSLAoFcGFydHkYAyABKAsyFi5jaGlycC5wYXJ0eS5QYXJ0'
    'eUluZm9SBXBhcnR5Eh0KCmV4cGlyZXNfYXQYBCABKANSCWV4cGlyZXNBdBIcCgl0aW1lc3RhbX'
    'AYBSABKANSCXRpbWVzdGFtcA==');

@$core.Deprecated('Use inviteResultNotifyDescriptor instead')
const InviteResultNotify$json = {
  '1': 'InviteResultNotify',
  '2': [
    {'1': 'invite_id', '3': 1, '4': 1, '5': 9, '10': 'inviteId'},
    {'1': 'target_user_id', '3': 2, '4': 1, '5': 9, '10': 'targetUserId'},
    {'1': 'accepted', '3': 3, '4': 1, '5': 8, '10': 'accepted'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `InviteResultNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List inviteResultNotifyDescriptor = $convert.base64Decode(
    'ChJJbnZpdGVSZXN1bHROb3RpZnkSGwoJaW52aXRlX2lkGAEgASgJUghpbnZpdGVJZBIkCg50YX'
    'JnZXRfdXNlcl9pZBgCIAEoCVIMdGFyZ2V0VXNlcklkEhoKCGFjY2VwdGVkGAMgASgIUghhY2Nl'
    'cHRlZBIcCgl0aW1lc3RhbXAYBCABKANSCXRpbWVzdGFtcA==');

@$core.Deprecated('Use partyJoinedNotifyDescriptor instead')
const PartyJoinedNotify$json = {
  '1': 'PartyJoinedNotify',
  '2': [
    {'1': 'party_id', '3': 1, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'member', '3': 2, '4': 1, '5': 11, '6': '.chirp.party.PartyMember', '10': 'member'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `PartyJoinedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List partyJoinedNotifyDescriptor = $convert.base64Decode(
    'ChFQYXJ0eUpvaW5lZE5vdGlmeRIZCghwYXJ0eV9pZBgBIAEoCVIHcGFydHlJZBIwCgZtZW1iZX'
    'IYAiABKAsyGC5jaGlycC5wYXJ0eS5QYXJ0eU1lbWJlclIGbWVtYmVyEhwKCXRpbWVzdGFtcBgD'
    'IAEoA1IJdGltZXN0YW1w');

@$core.Deprecated('Use partyLeftNotifyDescriptor instead')
const PartyLeftNotify$json = {
  '1': 'PartyLeftNotify',
  '2': [
    {'1': 'party_id', '3': 1, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'reason', '3': 3, '4': 1, '5': 9, '10': 'reason'},
    {'1': 'timestamp', '3': 4, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `PartyLeftNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List partyLeftNotifyDescriptor = $convert.base64Decode(
    'Cg9QYXJ0eUxlZnROb3RpZnkSGQoIcGFydHlfaWQYASABKAlSB3BhcnR5SWQSFwoHdXNlcl9pZB'
    'gCIAEoCVIGdXNlcklkEhYKBnJlYXNvbhgDIAEoCVIGcmVhc29uEhwKCXRpbWVzdGFtcBgEIAEo'
    'A1IJdGltZXN0YW1w');

@$core.Deprecated('Use partyKickedNotifyDescriptor instead')
const PartyKickedNotify$json = {
  '1': 'PartyKickedNotify',
  '2': [
    {'1': 'party_id', '3': 1, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'actor_user_id', '3': 2, '4': 1, '5': 9, '10': 'actorUserId'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `PartyKickedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List partyKickedNotifyDescriptor = $convert.base64Decode(
    'ChFQYXJ0eUtpY2tlZE5vdGlmeRIZCghwYXJ0eV9pZBgBIAEoCVIHcGFydHlJZBIiCg1hY3Rvcl'
    '91c2VyX2lkGAIgASgJUgthY3RvclVzZXJJZBIcCgl0aW1lc3RhbXAYAyABKANSCXRpbWVzdGFt'
    'cA==');

@$core.Deprecated('Use partyStateChangedNotifyDescriptor instead')
const PartyStateChangedNotify$json = {
  '1': 'PartyStateChangedNotify',
  '2': [
    {'1': 'party', '3': 1, '4': 1, '5': 11, '6': '.chirp.party.PartyInfo', '10': 'party'},
    {'1': 'timestamp', '3': 2, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `PartyStateChangedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List partyStateChangedNotifyDescriptor = $convert.base64Decode(
    'ChdQYXJ0eVN0YXRlQ2hhbmdlZE5vdGlmeRIsCgVwYXJ0eRgBIAEoCzIWLmNoaXJwLnBhcnR5Ll'
    'BhcnR5SW5mb1IFcGFydHkSHAoJdGltZXN0YW1wGAIgASgDUgl0aW1lc3RhbXA=');

@$core.Deprecated('Use partyDisbandedNotifyDescriptor instead')
const PartyDisbandedNotify$json = {
  '1': 'PartyDisbandedNotify',
  '2': [
    {'1': 'party_id', '3': 1, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'actor_user_id', '3': 2, '4': 1, '5': 9, '10': 'actorUserId'},
    {'1': 'timestamp', '3': 3, '4': 1, '5': 3, '10': 'timestamp'},
  ],
};

/// Descriptor for `PartyDisbandedNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List partyDisbandedNotifyDescriptor = $convert.base64Decode(
    'ChRQYXJ0eURpc2JhbmRlZE5vdGlmeRIZCghwYXJ0eV9pZBgBIAEoCVIHcGFydHlJZBIiCg1hY3'
    'Rvcl91c2VyX2lkGAIgASgJUgthY3RvclVzZXJJZBIcCgl0aW1lc3RhbXAYAyABKANSCXRpbWVz'
    'dGFtcA==');

@$core.Deprecated('Use storedPartyDescriptor instead')
const StoredParty$json = {
  '1': 'StoredParty',
  '2': [
    {'1': 'party_id', '3': 1, '4': 1, '5': 9, '10': 'partyId'},
    {'1': 'leader_id', '3': 2, '4': 1, '5': 9, '10': 'leaderId'},
    {'1': 'max_members', '3': 3, '4': 1, '5': 5, '10': 'maxMembers'},
    {'1': 'created_at', '3': 4, '4': 1, '5': 3, '10': 'createdAt'},
    {'1': 'members', '3': 5, '4': 3, '5': 11, '6': '.chirp.party.StoredMember', '10': 'members'},
  ],
};

/// Descriptor for `StoredParty`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedPartyDescriptor = $convert.base64Decode(
    'CgtTdG9yZWRQYXJ0eRIZCghwYXJ0eV9pZBgBIAEoCVIHcGFydHlJZBIbCglsZWFkZXJfaWQYAi'
    'ABKAlSCGxlYWRlcklkEh8KC21heF9tZW1iZXJzGAMgASgFUgptYXhNZW1iZXJzEh0KCmNyZWF0'
    'ZWRfYXQYBCABKANSCWNyZWF0ZWRBdBIzCgdtZW1iZXJzGAUgAygLMhkuY2hpcnAucGFydHkuU3'
    'RvcmVkTWVtYmVyUgdtZW1iZXJz');

@$core.Deprecated('Use storedMemberDescriptor instead')
const StoredMember$json = {
  '1': 'StoredMember',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'ready', '3': 2, '4': 1, '5': 8, '10': 'ready'},
    {'1': 'joined_at', '3': 3, '4': 1, '5': 3, '10': 'joinedAt'},
  ],
};

/// Descriptor for `StoredMember`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedMemberDescriptor = $convert.base64Decode(
    'CgxTdG9yZWRNZW1iZXISFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEhQKBXJlYWR5GAIgASgIUg'
    'VyZWFkeRIbCglqb2luZWRfYXQYAyABKANSCGpvaW5lZEF0');

