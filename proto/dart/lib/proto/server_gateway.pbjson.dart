//
//  Generated code. Do not modify.
//  source: proto/server_gateway.proto
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
    {'1': 'sender_kind', '3': 2, '4': 1, '5': 14, '6': '.chirp.server_gateway.SenderKind', '10': 'senderKind'},
    {'1': 'sender_id', '3': 3, '4': 1, '5': 9, '10': 'senderId'},
    {'1': 'channel_type', '3': 4, '4': 1, '5': 5, '10': 'channelType'},
    {'1': 'channel_id', '3': 5, '4': 1, '5': 9, '10': 'channelId'},
    {'1': 'receiver_id', '3': 6, '4': 1, '5': 9, '10': 'receiverId'},
    {'1': 'content', '3': 7, '4': 1, '5': 12, '10': 'content'},
  ],
};

/// Descriptor for `MessageInjectRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List messageInjectRequestDescriptor = $convert.base64Decode(
    'ChRNZXNzYWdlSW5qZWN0UmVxdWVzdBIbCglpbmplY3RfaWQYASABKAlSCGluamVjdElkEkEKC3'
    'NlbmRlcl9raW5kGAIgASgOMiAuY2hpcnAuc2VydmVyX2dhdGV3YXkuU2VuZGVyS2luZFIKc2Vu'
    'ZGVyS2luZBIbCglzZW5kZXJfaWQYAyABKAlSCHNlbmRlcklkEiEKDGNoYW5uZWxfdHlwZRgEIA'
    'EoBVILY2hhbm5lbFR5cGUSHQoKY2hhbm5lbF9pZBgFIAEoCVIJY2hhbm5lbElkEh8KC3JlY2Vp'
    'dmVyX2lkGAYgASgJUgpyZWNlaXZlcklkEhgKB2NvbnRlbnQYByABKAxSB2NvbnRlbnQ=');

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
    {'1': 'message', '3': 1, '4': 1, '5': 11, '6': '.chirp.server_gateway.MessageInjectRequest', '10': 'message'},
  ],
};

/// Descriptor for `InjectMessageNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List injectMessageNotifyDescriptor = $convert.base64Decode(
    'ChNJbmplY3RNZXNzYWdlTm90aWZ5EkQKB21lc3NhZ2UYASABKAsyKi5jaGlycC5zZXJ2ZXJfZ2'
    'F0ZXdheS5NZXNzYWdlSW5qZWN0UmVxdWVzdFIHbWVzc2FnZQ==');

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

