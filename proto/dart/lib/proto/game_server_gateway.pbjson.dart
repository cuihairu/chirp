//
//  Generated code. Do not modify.
//  source: proto/game_server_gateway.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use senderKindDescriptor instead')
const SenderKind$json = {
  '1': 'SenderKind',
  '2': [
    {'1': 'SENDER_UNKNOWN', '2': 0},
    {'1': 'SENDER_SYSTEM', '2': 1},
    {'1': 'SENDER_NPC', '2': 2},
    {'1': 'SENDER_SERVICE', '2': 3},
  ],
};

/// Descriptor for `SenderKind`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List senderKindDescriptor = $convert.base64Decode(
    'CgpTZW5kZXJLaW5kEhIKDlNFTkRFUl9VTktOT1dOEAASEQoNU0VOREVSX1NZU1RFTRABEg4KCl'
    'NFTkRFUl9OUEMQAhISCg5TRU5ERVJfU0VSVklDRRAD');

@$core.Deprecated('Use serverAuthRequestDescriptor instead')
const ServerAuthRequest$json = {
  '1': 'ServerAuthRequest',
  '2': [
    {'1': 'service_id', '3': 1, '4': 1, '5': 9, '10': 'serviceId'},
    {'1': 'secret', '3': 2, '4': 1, '5': 9, '10': 'secret'},
    {'1': 'protocol_version', '3': 3, '4': 1, '5': 5, '10': 'protocolVersion'},
  ],
};

/// Descriptor for `ServerAuthRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverAuthRequestDescriptor = $convert.base64Decode(
    'ChFTZXJ2ZXJBdXRoUmVxdWVzdBIdCgpzZXJ2aWNlX2lkGAEgASgJUglzZXJ2aWNlSWQSFgoGc2'
    'VjcmV0GAIgASgJUgZzZWNyZXQSKQoQcHJvdG9jb2xfdmVyc2lvbhgDIAEoBVIPcHJvdG9jb2xW'
    'ZXJzaW9u');

@$core.Deprecated('Use serverAuthResponseDescriptor instead')
const ServerAuthResponse$json = {
  '1': 'ServerAuthResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time_ms', '3': 2, '4': 1, '5': 3, '10': 'serverTimeMs'},
    {'1': 'heartbeat_interval_seconds', '3': 3, '4': 1, '5': 5, '10': 'heartbeatIntervalSeconds'},
  ],
};

/// Descriptor for `ServerAuthResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverAuthResponseDescriptor = $convert.base64Decode(
    'ChJTZXJ2ZXJBdXRoUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5FcnJvck'
    'NvZGVSBGNvZGUSJAoOc2VydmVyX3RpbWVfbXMYAiABKANSDHNlcnZlclRpbWVNcxI8ChpoZWFy'
    'dGJlYXRfaW50ZXJ2YWxfc2Vjb25kcxgDIAEoBVIYaGVhcnRiZWF0SW50ZXJ2YWxTZWNvbmRz');

@$core.Deprecated('Use serverHeartbeatPingDescriptor instead')
const ServerHeartbeatPing$json = {
  '1': 'ServerHeartbeatPing',
  '2': [
    {'1': 'client_time_ms', '3': 1, '4': 1, '5': 3, '10': 'clientTimeMs'},
  ],
};

/// Descriptor for `ServerHeartbeatPing`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverHeartbeatPingDescriptor = $convert.base64Decode(
    'ChNTZXJ2ZXJIZWFydGJlYXRQaW5nEiQKDmNsaWVudF90aW1lX21zGAEgASgDUgxjbGllbnRUaW'
    '1lTXM=');

@$core.Deprecated('Use serverHeartbeatPongDescriptor instead')
const ServerHeartbeatPong$json = {
  '1': 'ServerHeartbeatPong',
  '2': [
    {'1': 'server_time_ms', '3': 1, '4': 1, '5': 3, '10': 'serverTimeMs'},
  ],
};

/// Descriptor for `ServerHeartbeatPong`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverHeartbeatPongDescriptor = $convert.base64Decode(
    'ChNTZXJ2ZXJIZWFydGJlYXRQb25nEiQKDnNlcnZlcl90aW1lX21zGAEgASgDUgxzZXJ2ZXJUaW'
    '1lTXM=');

@$core.Deprecated('Use messageInjectRequestDescriptor instead')
const MessageInjectRequest$json = {
  '1': 'MessageInjectRequest',
  '2': [
    {'1': 'inject_id', '3': 1, '4': 1, '5': 9, '10': 'injectId'},
    {'1': 'sender_kind', '3': 2, '4': 1, '5': 14, '6': '.chirp.game_server_gateway.SenderKind', '10': 'senderKind'},
    {'1': 'sender_id', '3': 3, '4': 1, '5': 9, '10': 'senderId'},
    {'1': 'channel_type', '3': 4, '4': 1, '5': 5, '10': 'channelType'},
    {'1': 'channel_id', '3': 5, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'receiver_id', '3': 6, '4': 1, '5': 9, '10': 'receiverId'},
    {'1': 'content', '3': 7, '4': 1, '5': 12, '10': 'content'},
    {'1': 'game_id', '3': 8, '4': 1, '5': 9, '10': 'gameId'},
  ],
};

/// Descriptor for `MessageInjectRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageInjectRequestDescriptor = $convert.base64Decode(
    'ChRNZXNzYWdlSW5qZWN0UmVxdWVzdBIbCglpbmplY3RfaWQYASABKAlSCGluamVjdElkEkYKC3'
    'NlbmRlcl9raW5kGAIgASgOMiUuY2hpcnAuZ2FtZV9zZXJ2ZXJfZ2F0ZXdheS5TZW5kZXJLaW5k'
    'UgpzZW5kZXJLaW5kEhsKCXNlbmRlcl9pZBgDIAEoCVIIc2VuZGVySWQSIQoMY2hhbm5lbF90eX'
    'BlGAQgASgFUgtjaGFubmVsVHlwZRIdCgpjaGFubmVsX2lkGAUgASgJUgljaGFubmVsSWQSHwoL'
    'cmVjZWl2ZXJfaWQYBiABKAlSCnJlY2VpdmVySWQSGAoHY29udGVudBgHIAEoDFIHY29udGVudB'
    'IXCgdnYW1lX2lkGAggASgJUgZnYW1lSWQ=');

@$core.Deprecated('Use messageInjectResponseDescriptor instead')
const MessageInjectResponse$json = {
  '1': 'MessageInjectResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'inject_id', '3': 2, '4': 1, '5': 9, '10': 'injectId'},
  ],
};

/// Descriptor for `MessageInjectResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageInjectResponseDescriptor = $convert.base64Decode(
    'ChVNZXNzYWdlSW5qZWN0UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSGwoJaW5qZWN0X2lkGAIgASgJUghpbmplY3RJZA==');

@$core.Deprecated('Use injectMessageNotifyDescriptor instead')
const InjectMessageNotify$json = {
  '1': 'InjectMessageNotify',
  '2': [
    {'1': 'message', '3': 1, '4': 1, '5': 11, '6': '.chirp.game_server_gateway.MessageInjectRequest', '10': 'message'},
  ],
};

/// Descriptor for `InjectMessageNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List injectMessageNotifyDescriptor = $convert.base64Decode(
    'ChNJbmplY3RNZXNzYWdlTm90aWZ5EkkKB21lc3NhZ2UYASABKAsyLy5jaGlycC5nYW1lX3Nlcn'
    'Zlcl9nYXRld2F5Lk1lc3NhZ2VJbmplY3RSZXF1ZXN0UgdtZXNzYWdl');

@$core.Deprecated('Use eventPublishRequestDescriptor instead')
const EventPublishRequest$json = {
  '1': 'EventPublishRequest',
  '2': [
    {'1': 'event_id', '3': 1, '4': 1, '5': 9, '10': 'eventId'},
    {'1': 'target_service_id', '3': 2, '4': 1, '5': 9, '10': 'targetServiceId'},
    {'1': 'event_type', '3': 3, '4': 1, '5': 9, '10': 'eventType'},
    {'1': 'payload', '3': 4, '4': 1, '5': 12, '10': 'payload'},
  ],
};

/// Descriptor for `EventPublishRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List eventPublishRequestDescriptor = $convert.base64Decode(
    'ChNFdmVudFB1Ymxpc2hSZXF1ZXN0EhkKCGV2ZW50X2lkGAEgASgJUgdldmVudElkEioKEXRhcm'
    'dldF9zZXJ2aWNlX2lkGAIgASgJUg90YXJnZXRTZXJ2aWNlSWQSHQoKZXZlbnRfdHlwZRgDIAEo'
    'CVIJZXZlbnRUeXBlEhgKB3BheWxvYWQYBCABKAxSB3BheWxvYWQ=');

@$core.Deprecated('Use eventPublishResponseDescriptor instead')
const EventPublishResponse$json = {
  '1': 'EventPublishResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'event_id', '3': 2, '4': 1, '5': 9, '10': 'eventId'},
    {'1': 'queued', '3': 3, '4': 1, '5': 8, '10': 'queued'},
  ],
};

/// Descriptor for `EventPublishResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List eventPublishResponseDescriptor = $convert.base64Decode(
    'ChRFdmVudFB1Ymxpc2hSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIZCghldmVudF9pZBgCIAEoCVIHZXZlbnRJZBIWCgZxdWV1ZWQYAyABKAhS'
    'BnF1ZXVlZA==');

@$core.Deprecated('Use eventDeliverNotifyDescriptor instead')
const EventDeliverNotify$json = {
  '1': 'EventDeliverNotify',
  '2': [
    {'1': 'event_id', '3': 1, '4': 1, '5': 9, '10': 'eventId'},
    {'1': 'event_type', '3': 2, '4': 1, '5': 9, '10': 'eventType'},
    {'1': 'payload', '3': 3, '4': 1, '5': 12, '10': 'payload'},
    {'1': 'published_at_ms', '3': 4, '4': 1, '5': 3, '10': 'publishedAtMs'},
    {'1': 'attempt', '3': 5, '4': 1, '5': 5, '10': 'attempt'},
  ],
};

/// Descriptor for `EventDeliverNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List eventDeliverNotifyDescriptor = $convert.base64Decode(
    'ChJFdmVudERlbGl2ZXJOb3RpZnkSGQoIZXZlbnRfaWQYASABKAlSB2V2ZW50SWQSHQoKZXZlbn'
    'RfdHlwZRgCIAEoCVIJZXZlbnRUeXBlEhgKB3BheWxvYWQYAyABKAxSB3BheWxvYWQSJgoPcHVi'
    'bGlzaGVkX2F0X21zGAQgASgDUg1wdWJsaXNoZWRBdE1zEhgKB2F0dGVtcHQYBSABKAVSB2F0dG'
    'VtcHQ=');

@$core.Deprecated('Use eventAckRequestDescriptor instead')
const EventAckRequest$json = {
  '1': 'EventAckRequest',
  '2': [
    {'1': 'event_ids', '3': 1, '4': 3, '5': 9, '10': 'eventIds'},
  ],
};

/// Descriptor for `EventAckRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List eventAckRequestDescriptor = $convert.base64Decode(
    'Cg9FdmVudEFja1JlcXVlc3QSGwoJZXZlbnRfaWRzGAEgAygJUghldmVudElkcw==');

@$core.Deprecated('Use eventAckResponseDescriptor instead')
const EventAckResponse$json = {
  '1': 'EventAckResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
  ],
};

/// Descriptor for `EventAckResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List eventAckResponseDescriptor = $convert.base64Decode(
    'ChBFdmVudEFja1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3JDb2'
    'RlUgRjb2Rl');

@$core.Deprecated('Use storedIdentityBindingDescriptor instead')
const StoredIdentityBinding$json = {
  '1': 'StoredIdentityBinding',
  '2': [
    {'1': 'binding_id', '3': 1, '4': 1, '5': 9, '10': 'bindingId'},
    {'1': 'player_id', '3': 2, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 3, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'game_user_id', '3': 4, '4': 1, '5': 9, '10': 'gameUserId'},
    {'1': 'bound_at_ms', '3': 5, '4': 1, '5': 3, '10': 'boundAtMs'},
  ],
};

/// Descriptor for `StoredIdentityBinding`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedIdentityBindingDescriptor = $convert.base64Decode(
    'ChVTdG9yZWRJZGVudGl0eUJpbmRpbmcSHQoKYmluZGluZ19pZBgBIAEoCVIJYmluZGluZ0lkEh'
    'sKCXBsYXllcl9pZBgCIAEoCVIIcGxheWVySWQSFwoHZ2FtZV9pZBgDIAEoCVIGZ2FtZUlkEiAK'
    'DGdhbWVfdXNlcl9pZBgEIAEoCVIKZ2FtZVVzZXJJZBIeCgtib3VuZF9hdF9tcxgFIAEoA1IJYm'
    '91bmRBdE1z');

@$core.Deprecated('Use bindPlayerIdentityRequestDescriptor instead')
const BindPlayerIdentityRequest$json = {
  '1': 'BindPlayerIdentityRequest',
  '2': [
    {'1': 'binding_id', '3': 1, '4': 1, '5': 9, '10': 'bindingId'},
    {'1': 'player_id', '3': 2, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 3, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'game_user_id', '3': 4, '4': 1, '5': 9, '10': 'gameUserId'},
  ],
};

/// Descriptor for `BindPlayerIdentityRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List bindPlayerIdentityRequestDescriptor = $convert.base64Decode(
    'ChlCaW5kUGxheWVySWRlbnRpdHlSZXF1ZXN0Eh0KCmJpbmRpbmdfaWQYASABKAlSCWJpbmRpbm'
    'dJZBIbCglwbGF5ZXJfaWQYAiABKAlSCHBsYXllcklkEhcKB2dhbWVfaWQYAyABKAlSBmdhbWVJ'
    'ZBIgCgxnYW1lX3VzZXJfaWQYBCABKAlSCmdhbWVVc2VySWQ=');

@$core.Deprecated('Use bindPlayerIdentityResponseDescriptor instead')
const BindPlayerIdentityResponse$json = {
  '1': 'BindPlayerIdentityResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'binding_id', '3': 2, '4': 1, '5': 9, '10': 'bindingId'},
    {'1': 'existed', '3': 3, '4': 1, '5': 8, '10': 'existed'},
  ],
};

/// Descriptor for `BindPlayerIdentityResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List bindPlayerIdentityResponseDescriptor = $convert.base64Decode(
    'ChpCaW5kUGxheWVySWRlbnRpdHlSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW'
    '9uLkVycm9yQ29kZVIEY29kZRIdCgpiaW5kaW5nX2lkGAIgASgJUgliaW5kaW5nSWQSGAoHZXhp'
    'c3RlZBgDIAEoCFIHZXhpc3RlZA==');

@$core.Deprecated('Use unbindPlayerIdentityRequestDescriptor instead')
const UnbindPlayerIdentityRequest$json = {
  '1': 'UnbindPlayerIdentityRequest',
  '2': [
    {'1': 'binding_id', '3': 1, '4': 1, '5': 9, '10': 'bindingId'},
    {'1': 'game_id', '3': 2, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'game_user_id', '3': 3, '4': 1, '5': 9, '10': 'gameUserId'},
  ],
};

/// Descriptor for `UnbindPlayerIdentityRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unbindPlayerIdentityRequestDescriptor = $convert.base64Decode(
    'ChtVbmJpbmRQbGF5ZXJJZGVudGl0eVJlcXVlc3QSHQoKYmluZGluZ19pZBgBIAEoCVIJYmluZG'
    'luZ0lkEhcKB2dhbWVfaWQYAiABKAlSBmdhbWVJZBIgCgxnYW1lX3VzZXJfaWQYAyABKAlSCmdh'
    'bWVVc2VySWQ=');

@$core.Deprecated('Use unbindPlayerIdentityResponseDescriptor instead')
const UnbindPlayerIdentityResponse$json = {
  '1': 'UnbindPlayerIdentityResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
  ],
};

/// Descriptor for `UnbindPlayerIdentityResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unbindPlayerIdentityResponseDescriptor = $convert.base64Decode(
    'ChxVbmJpbmRQbGF5ZXJJZGVudGl0eVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb2'
    '1tb24uRXJyb3JDb2RlUgRjb2Rl');

@$core.Deprecated('Use getPlayerIdentitiesRequestDescriptor instead')
const GetPlayerIdentitiesRequest$json = {
  '1': 'GetPlayerIdentitiesRequest',
  '2': [
    {'1': 'player_id', '3': 1, '4': 1, '5': 9, '10': 'playerId'},
  ],
};

/// Descriptor for `GetPlayerIdentitiesRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPlayerIdentitiesRequestDescriptor = $convert.base64Decode(
    'ChpHZXRQbGF5ZXJJZGVudGl0aWVzUmVxdWVzdBIbCglwbGF5ZXJfaWQYASABKAlSCHBsYXllck'
    'lk');

@$core.Deprecated('Use getPlayerIdentitiesResponseDescriptor instead')
const GetPlayerIdentitiesResponse$json = {
  '1': 'GetPlayerIdentitiesResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'bindings', '3': 2, '4': 3, '5': 11, '6': '.chirp.game_server_gateway.StoredIdentityBinding', '10': 'bindings'},
  ],
};

/// Descriptor for `GetPlayerIdentitiesResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPlayerIdentitiesResponseDescriptor = $convert.base64Decode(
    'ChtHZXRQbGF5ZXJJZGVudGl0aWVzUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW'
    '1vbi5FcnJvckNvZGVSBGNvZGUSTAoIYmluZGluZ3MYAiADKAsyMC5jaGlycC5nYW1lX3NlcnZl'
    'cl9nYXRld2F5LlN0b3JlZElkZW50aXR5QmluZGluZ1IIYmluZGluZ3M=');

@$core.Deprecated('Use resolveGameUserRequestDescriptor instead')
const ResolveGameUserRequest$json = {
  '1': 'ResolveGameUserRequest',
  '2': [
    {'1': 'game_id', '3': 1, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'game_user_id', '3': 2, '4': 1, '5': 9, '10': 'gameUserId'},
  ],
};

/// Descriptor for `ResolveGameUserRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List resolveGameUserRequestDescriptor = $convert.base64Decode(
    'ChZSZXNvbHZlR2FtZVVzZXJSZXF1ZXN0EhcKB2dhbWVfaWQYASABKAlSBmdhbWVJZBIgCgxnYW'
    '1lX3VzZXJfaWQYAiABKAlSCmdhbWVVc2VySWQ=');

@$core.Deprecated('Use resolveGameUserResponseDescriptor instead')
const ResolveGameUserResponse$json = {
  '1': 'ResolveGameUserResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'player_id', '3': 2, '4': 1, '5': 9, '10': 'playerId'},
  ],
};

/// Descriptor for `ResolveGameUserResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List resolveGameUserResponseDescriptor = $convert.base64Decode(
    'ChdSZXNvbHZlR2FtZVVzZXJSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLk'
    'Vycm9yQ29kZVIEY29kZRIbCglwbGF5ZXJfaWQYAiABKAlSCHBsYXllcklk');

@$core.Deprecated('Use storedChannelSubscriptionDescriptor instead')
const StoredChannelSubscription$json = {
  '1': 'StoredChannelSubscription',
  '2': [
    {'1': 'subscription_id', '3': 1, '4': 1, '5': 9, '10': 'subscriptionId'},
    {'1': 'player_id', '3': 2, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 3, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'channel_id', '3': 4, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'subscribed_at_ms', '3': 5, '4': 1, '5': 3, '10': 'subscribedAtMs'},
  ],
};

/// Descriptor for `StoredChannelSubscription`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedChannelSubscriptionDescriptor = $convert.base64Decode(
    'ChlTdG9yZWRDaGFubmVsU3Vic2NyaXB0aW9uEicKD3N1YnNjcmlwdGlvbl9pZBgBIAEoCVIOc3'
    'Vic2NyaXB0aW9uSWQSGwoJcGxheWVyX2lkGAIgASgJUghwbGF5ZXJJZBIXCgdnYW1lX2lkGAMg'
    'ASgJUgZnYW1lSWQSHQoKY2hhbm5lbF9pZBgEIAEoCVIJY2hhbm5lbElkEigKEHN1YnNjcmliZW'
    'RfYXRfbXMYBSABKANSDnN1YnNjcmliZWRBdE1z');

@$core.Deprecated('Use subscribePlayerChannelRequestDescriptor instead')
const SubscribePlayerChannelRequest$json = {
  '1': 'SubscribePlayerChannelRequest',
  '2': [
    {'1': 'subscription_id', '3': 1, '4': 1, '5': 9, '10': 'subscriptionId'},
    {'1': 'player_id', '3': 2, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 3, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'channel_id', '3': 4, '4': 1, '5': 9, '10': 'channelId'},
  ],
};

/// Descriptor for `SubscribePlayerChannelRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List subscribePlayerChannelRequestDescriptor = $convert.base64Decode(
    'Ch1TdWJzY3JpYmVQbGF5ZXJDaGFubmVsUmVxdWVzdBInCg9zdWJzY3JpcHRpb25faWQYASABKA'
    'lSDnN1YnNjcmlwdGlvbklkEhsKCXBsYXllcl9pZBgCIAEoCVIIcGxheWVySWQSFwoHZ2FtZV9p'
    'ZBgDIAEoCVIGZ2FtZUlkEh0KCmNoYW5uZWxfaWQYBCABKAlSCWNoYW5uZWxJZA==');

@$core.Deprecated('Use subscribePlayerChannelResponseDescriptor instead')
const SubscribePlayerChannelResponse$json = {
  '1': 'SubscribePlayerChannelResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'subscription_id', '3': 2, '4': 1, '5': 9, '10': 'subscriptionId'},
    {'1': 'existed', '3': 3, '4': 1, '5': 8, '10': 'existed'},
  ],
};

/// Descriptor for `SubscribePlayerChannelResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List subscribePlayerChannelResponseDescriptor = $convert.base64Decode(
    'Ch5TdWJzY3JpYmVQbGF5ZXJDaGFubmVsUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLm'
    'NvbW1vbi5FcnJvckNvZGVSBGNvZGUSJwoPc3Vic2NyaXB0aW9uX2lkGAIgASgJUg5zdWJzY3Jp'
    'cHRpb25JZBIYCgdleGlzdGVkGAMgASgIUgdleGlzdGVk');

@$core.Deprecated('Use unsubscribePlayerChannelRequestDescriptor instead')
const UnsubscribePlayerChannelRequest$json = {
  '1': 'UnsubscribePlayerChannelRequest',
  '2': [
    {'1': 'subscription_id', '3': 1, '4': 1, '5': 9, '10': 'subscriptionId'},
    {'1': 'player_id', '3': 2, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 3, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'channel_id', '3': 4, '4': 1, '5': 9, '10': 'channelId'},
  ],
};

/// Descriptor for `UnsubscribePlayerChannelRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unsubscribePlayerChannelRequestDescriptor = $convert.base64Decode(
    'Ch9VbnN1YnNjcmliZVBsYXllckNoYW5uZWxSZXF1ZXN0EicKD3N1YnNjcmlwdGlvbl9pZBgBIA'
    'EoCVIOc3Vic2NyaXB0aW9uSWQSGwoJcGxheWVyX2lkGAIgASgJUghwbGF5ZXJJZBIXCgdnYW1l'
    'X2lkGAMgASgJUgZnYW1lSWQSHQoKY2hhbm5lbF9pZBgEIAEoCVIJY2hhbm5lbElk');

@$core.Deprecated('Use unsubscribePlayerChannelResponseDescriptor instead')
const UnsubscribePlayerChannelResponse$json = {
  '1': 'UnsubscribePlayerChannelResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
  ],
};

/// Descriptor for `UnsubscribePlayerChannelResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unsubscribePlayerChannelResponseDescriptor = $convert.base64Decode(
    'CiBVbnN1YnNjcmliZVBsYXllckNoYW5uZWxSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcn'
    'AuY29tbW9uLkVycm9yQ29kZVIEY29kZQ==');

@$core.Deprecated('Use getPlayerSubscriptionsRequestDescriptor instead')
const GetPlayerSubscriptionsRequest$json = {
  '1': 'GetPlayerSubscriptionsRequest',
  '2': [
    {'1': 'player_id', '3': 1, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 2, '4': 1, '5': 9, '10': 'gameId'},
  ],
};

/// Descriptor for `GetPlayerSubscriptionsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPlayerSubscriptionsRequestDescriptor = $convert.base64Decode(
    'Ch1HZXRQbGF5ZXJTdWJzY3JpcHRpb25zUmVxdWVzdBIbCglwbGF5ZXJfaWQYASABKAlSCHBsYX'
    'llcklkEhcKB2dhbWVfaWQYAiABKAlSBmdhbWVJZA==');

@$core.Deprecated('Use getPlayerSubscriptionsResponseDescriptor instead')
const GetPlayerSubscriptionsResponse$json = {
  '1': 'GetPlayerSubscriptionsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'subscriptions', '3': 2, '4': 3, '5': 11, '6': '.chirp.game_server_gateway.StoredChannelSubscription', '10': 'subscriptions'},
  ],
};

/// Descriptor for `GetPlayerSubscriptionsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPlayerSubscriptionsResponseDescriptor = $convert.base64Decode(
    'Ch5HZXRQbGF5ZXJTdWJzY3JpcHRpb25zUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLm'
    'NvbW1vbi5FcnJvckNvZGVSBGNvZGUSWgoNc3Vic2NyaXB0aW9ucxgCIAMoCzI0LmNoaXJwLmdh'
    'bWVfc2VydmVyX2dhdGV3YXkuU3RvcmVkQ2hhbm5lbFN1YnNjcmlwdGlvblINc3Vic2NyaXB0aW'
    '9ucw==');

@$core.Deprecated('Use storedUnreadEntryDescriptor instead')
const StoredUnreadEntry$json = {
  '1': 'StoredUnreadEntry',
  '2': [
    {'1': 'player_id', '3': 1, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 2, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'channel_id', '3': 3, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'unread_count', '3': 4, '4': 1, '5': 5, '10': 'unreadCount'},
  ],
};

/// Descriptor for `StoredUnreadEntry`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List storedUnreadEntryDescriptor = $convert.base64Decode(
    'ChFTdG9yZWRVbnJlYWRFbnRyeRIbCglwbGF5ZXJfaWQYASABKAlSCHBsYXllcklkEhcKB2dhbW'
    'VfaWQYAiABKAlSBmdhbWVJZBIdCgpjaGFubmVsX2lkGAMgASgJUgljaGFubmVsSWQSIQoMdW5y'
    'ZWFkX2NvdW50GAQgASgFUgt1bnJlYWRDb3VudA==');

@$core.Deprecated('Use markChannelsReadRequestDescriptor instead')
const MarkChannelsReadRequest$json = {
  '1': 'MarkChannelsReadRequest',
  '2': [
    {'1': 'player_id', '3': 1, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 2, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'channel_id', '3': 3, '4': 1, '5': 9, '10': 'channelId'},
  ],
};

/// Descriptor for `MarkChannelsReadRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List markChannelsReadRequestDescriptor = $convert.base64Decode(
    'ChdNYXJrQ2hhbm5lbHNSZWFkUmVxdWVzdBIbCglwbGF5ZXJfaWQYASABKAlSCHBsYXllcklkEh'
    'cKB2dhbWVfaWQYAiABKAlSBmdhbWVJZBIdCgpjaGFubmVsX2lkGAMgASgJUgljaGFubmVsSWQ=');

@$core.Deprecated('Use markChannelsReadResponseDescriptor instead')
const MarkChannelsReadResponse$json = {
  '1': 'MarkChannelsReadResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'cleared', '3': 2, '4': 1, '5': 5, '10': 'cleared'},
  ],
};

/// Descriptor for `MarkChannelsReadResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List markChannelsReadResponseDescriptor = $convert.base64Decode(
    'ChhNYXJrQ2hhbm5lbHNSZWFkUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi'
    '5FcnJvckNvZGVSBGNvZGUSGAoHY2xlYXJlZBgCIAEoBVIHY2xlYXJlZA==');

@$core.Deprecated('Use unreadSummaryEntryDescriptor instead')
const UnreadSummaryEntry$json = {
  '1': 'UnreadSummaryEntry',
  '2': [
    {'1': 'game_id', '3': 1, '4': 1, '5': 9, '10': 'gameId'},
    {'1': 'channel_id', '3': 2, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'unread_count', '3': 3, '4': 1, '5': 5, '10': 'unreadCount'},
  ],
};

/// Descriptor for `UnreadSummaryEntry`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unreadSummaryEntryDescriptor = $convert.base64Decode(
    'ChJVbnJlYWRTdW1tYXJ5RW50cnkSFwoHZ2FtZV9pZBgBIAEoCVIGZ2FtZUlkEh0KCmNoYW5uZW'
    'xfaWQYAiABKAlSCWNoYW5uZWxJZBIhCgx1bnJlYWRfY291bnQYAyABKAVSC3VucmVhZENvdW50');

@$core.Deprecated('Use getUnreadSummaryRequestDescriptor instead')
const GetUnreadSummaryRequest$json = {
  '1': 'GetUnreadSummaryRequest',
  '2': [
    {'1': 'player_id', '3': 1, '4': 1, '5': 9, '10': 'playerId'},
    {'1': 'game_id', '3': 2, '4': 1, '5': 9, '10': 'gameId'},
  ],
};

/// Descriptor for `GetUnreadSummaryRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUnreadSummaryRequestDescriptor = $convert.base64Decode(
    'ChdHZXRVbnJlYWRTdW1tYXJ5UmVxdWVzdBIbCglwbGF5ZXJfaWQYASABKAlSCHBsYXllcklkEh'
    'cKB2dhbWVfaWQYAiABKAlSBmdhbWVJZA==');

@$core.Deprecated('Use getUnreadSummaryResponseDescriptor instead')
const GetUnreadSummaryResponse$json = {
  '1': 'GetUnreadSummaryResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'entries', '3': 2, '4': 3, '5': 11, '6': '.chirp.game_server_gateway.UnreadSummaryEntry', '10': 'entries'},
    {'1': 'total_unread', '3': 3, '4': 1, '5': 5, '10': 'totalUnread'},
  ],
};

/// Descriptor for `GetUnreadSummaryResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUnreadSummaryResponseDescriptor = $convert.base64Decode(
    'ChhHZXRVbnJlYWRTdW1tYXJ5UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi'
    '5FcnJvckNvZGVSBGNvZGUSRwoHZW50cmllcxgCIAMoCzItLmNoaXJwLmdhbWVfc2VydmVyX2dh'
    'dGV3YXkuVW5yZWFkU3VtbWFyeUVudHJ5UgdlbnRyaWVzEiEKDHRvdGFsX3VucmVhZBgDIAEoBV'
    'ILdG90YWxVbnJlYWQ=');

