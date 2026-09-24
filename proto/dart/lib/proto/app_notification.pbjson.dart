//
//  Generated code. Do not modify.
//  source: proto/app_notification.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use notificationPriorityDescriptor instead')
const NotificationPriority$json = {
  '1': 'NotificationPriority',
  '2': [
    {'1': 'LOW', '2': 0},
    {'1': 'NORMAL', '2': 1},
    {'1': 'HIGH', '2': 2},
    {'1': 'URGENT', '2': 3},
  ],
};

/// Descriptor for `NotificationPriority`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List notificationPriorityDescriptor = $convert.base64Decode(
    'ChROb3RpZmljYXRpb25Qcmlvcml0eRIHCgNMT1cQABIKCgZOT1JNQUwQARIICgRISUdIEAISCg'
    'oGVVJHRU5UEAM=');

@$core.Deprecated('Use notificationTypeDescriptor instead')
const NotificationType$json = {
  '1': 'NotificationType',
  '2': [
    {'1': 'MESSAGE', '2': 0},
    {'1': 'MENTION', '2': 1},
    {'1': 'REACTION', '2': 2},
    {'1': 'FRIEND_REQUEST', '2': 3},
    {'1': 'FRIEND_ACCEPTED', '2': 4},
    {'1': 'VOICE_CALL', '2': 5},
    {'1': 'VOICE_INVITE', '2': 6},
    {'1': 'SYSTEM', '2': 99},
  ],
};

/// Descriptor for `NotificationType`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List notificationTypeDescriptor = $convert.base64Decode(
    'ChBOb3RpZmljYXRpb25UeXBlEgsKB01FU1NBR0UQABILCgdNRU5USU9OEAESDAoIUkVBQ1RJT0'
    '4QAhISCg5GUklFTkRfUkVRVUVTVBADEhMKD0ZSSUVORF9BQ0NFUFRFRBAEEg4KClZPSUNFX0NB'
    'TEwQBRIQCgxWT0lDRV9JTlZJVEUQBhIKCgZTWVNURU0QYw==');

@$core.Deprecated('Use pushNotificationRequestDescriptor instead')
const PushNotificationRequest$json = {
  '1': 'PushNotificationRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'type', '3': 2, '4': 1, '5': 14, '6': '.chirp.app_notification.NotificationType', '10': 'type'},
    {'1': 'priority', '3': 3, '4': 1, '5': 14, '6': '.chirp.app_notification.NotificationPriority', '10': 'priority'},
    {'1': 'title', '3': 4, '4': 1, '5': 9, '10': 'title'},
    {'1': 'body', '3': 5, '4': 1, '5': 9, '10': 'body'},
    {'1': 'icon', '3': 6, '4': 1, '5': 9, '10': 'icon'},
    {'1': 'image', '3': 7, '4': 1, '5': 9, '10': 'image'},
    {'1': 'sound', '3': 8, '4': 1, '5': 9, '10': 'sound'},
    {'1': 'tag', '3': 9, '4': 1, '5': 9, '10': 'tag'},
    {'1': 'data', '3': 10, '4': 3, '5': 11, '6': '.chirp.app_notification.PushNotificationRequest.DataEntry', '10': 'data'},
    {'1': 'badge', '3': 11, '4': 1, '5': 5, '10': 'badge'},
    {'1': 'click_action', '3': 12, '4': 1, '5': 9, '10': 'clickAction'},
    {'1': 'ttl_ms', '3': 13, '4': 1, '5': 3, '10': 'ttlMs'},
    {'1': 'collapse_key', '3': 14, '4': 1, '5': 8, '10': 'collapseKey'},
  ],
  '3': [PushNotificationRequest_DataEntry$json],
};

@$core.Deprecated('Use pushNotificationRequestDescriptor instead')
const PushNotificationRequest_DataEntry$json = {
  '1': 'DataEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `PushNotificationRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List pushNotificationRequestDescriptor = $convert.base64Decode(
    'ChdQdXNoTm90aWZpY2F0aW9uUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSPAoEdH'
    'lwZRgCIAEoDjIoLmNoaXJwLmFwcF9ub3RpZmljYXRpb24uTm90aWZpY2F0aW9uVHlwZVIEdHlw'
    'ZRJICghwcmlvcml0eRgDIAEoDjIsLmNoaXJwLmFwcF9ub3RpZmljYXRpb24uTm90aWZpY2F0aW'
    '9uUHJpb3JpdHlSCHByaW9yaXR5EhQKBXRpdGxlGAQgASgJUgV0aXRsZRISCgRib2R5GAUgASgJ'
    'UgRib2R5EhIKBGljb24YBiABKAlSBGljb24SFAoFaW1hZ2UYByABKAlSBWltYWdlEhQKBXNvdW'
    '5kGAggASgJUgVzb3VuZBIQCgN0YWcYCSABKAlSA3RhZxJNCgRkYXRhGAogAygLMjkuY2hpcnAu'
    'YXBwX25vdGlmaWNhdGlvbi5QdXNoTm90aWZpY2F0aW9uUmVxdWVzdC5EYXRhRW50cnlSBGRhdG'
    'ESFAoFYmFkZ2UYCyABKAVSBWJhZGdlEiEKDGNsaWNrX2FjdGlvbhgMIAEoCVILY2xpY2tBY3Rp'
    'b24SFQoGdHRsX21zGA0gASgDUgV0dGxNcxIhCgxjb2xsYXBzZV9rZXkYDiABKAhSC2NvbGxhcH'
    'NlS2V5GjcKCURhdGFFbnRyeRIQCgNrZXkYASABKAlSA2tleRIUCgV2YWx1ZRgCIAEoCVIFdmFs'
    'dWU6AjgB');

@$core.Deprecated('Use pushNotificationResponseDescriptor instead')
const PushNotificationResponse$json = {
  '1': 'PushNotificationResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'notification_id', '3': 2, '4': 1, '5': 9, '10': 'notificationId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `PushNotificationResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List pushNotificationResponseDescriptor = $convert.base64Decode(
    'ChhQdXNoTm90aWZpY2F0aW9uUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi'
    '5FcnJvckNvZGVSBGNvZGUSJwoPbm90aWZpY2F0aW9uX2lkGAIgASgJUg5ub3RpZmljYXRpb25J'
    'ZBIfCgtzZXJ2ZXJfdGltZRgDIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use registerDeviceRequestDescriptor instead')
const RegisterDeviceRequest$json = {
  '1': 'RegisterDeviceRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'device_id', '3': 2, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'platform', '3': 3, '4': 1, '5': 9, '10': 'platform'},
    {'1': 'fcm_token', '3': 4, '4': 1, '5': 9, '10': 'fcmToken'},
    {'1': 'apns_token', '3': 5, '4': 1, '5': 9, '10': 'apnsToken'},
    {'1': 'push_kit_token', '3': 6, '4': 1, '5': 9, '10': 'pushKitToken'},
    {'1': 'app_version', '3': 7, '4': 1, '5': 9, '10': 'appVersion'},
    {'1': 'os_version', '3': 8, '4': 1, '5': 9, '10': 'osVersion'},
    {'1': 'device_name', '3': 9, '4': 1, '5': 9, '10': 'deviceName'},
  ],
};

/// Descriptor for `RegisterDeviceRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List registerDeviceRequestDescriptor = $convert.base64Decode(
    'ChVSZWdpc3RlckRldmljZVJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEhsKCWRldm'
    'ljZV9pZBgCIAEoCVIIZGV2aWNlSWQSGgoIcGxhdGZvcm0YAyABKAlSCHBsYXRmb3JtEhsKCWZj'
    'bV90b2tlbhgEIAEoCVIIZmNtVG9rZW4SHQoKYXBuc190b2tlbhgFIAEoCVIJYXBuc1Rva2VuEi'
    'QKDnB1c2hfa2l0X3Rva2VuGAYgASgJUgxwdXNoS2l0VG9rZW4SHwoLYXBwX3ZlcnNpb24YByAB'
    'KAlSCmFwcFZlcnNpb24SHQoKb3NfdmVyc2lvbhgIIAEoCVIJb3NWZXJzaW9uEh8KC2RldmljZV'
    '9uYW1lGAkgASgJUgpkZXZpY2VOYW1l');

@$core.Deprecated('Use registerDeviceResponseDescriptor instead')
const RegisterDeviceResponse$json = {
  '1': 'RegisterDeviceResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `RegisterDeviceResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List registerDeviceResponseDescriptor = $convert.base64Decode(
    'ChZSZWdpc3RlckRldmljZVJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use unregisterDeviceRequestDescriptor instead')
const UnregisterDeviceRequest$json = {
  '1': 'UnregisterDeviceRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'device_id', '3': 2, '4': 1, '5': 9, '10': 'deviceId'},
  ],
};

/// Descriptor for `UnregisterDeviceRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unregisterDeviceRequestDescriptor = $convert.base64Decode(
    'ChdVbnJlZ2lzdGVyRGV2aWNlUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSGwoJZG'
    'V2aWNlX2lkGAIgASgJUghkZXZpY2VJZA==');

@$core.Deprecated('Use unregisterDeviceResponseDescriptor instead')
const UnregisterDeviceResponse$json = {
  '1': 'UnregisterDeviceResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `UnregisterDeviceResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List unregisterDeviceResponseDescriptor = $convert.base64Decode(
    'ChhVbnJlZ2lzdGVyRGV2aWNlUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi'
    '5FcnJvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use updateDeviceTokenRequestDescriptor instead')
const UpdateDeviceTokenRequest$json = {
  '1': 'UpdateDeviceTokenRequest',
  '2': [
    {'1': 'device_id', '3': 1, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'fcm_token', '3': 2, '4': 1, '5': 9, '10': 'fcmToken'},
    {'1': 'apns_token', '3': 3, '4': 1, '5': 9, '10': 'apnsToken'},
    {'1': 'push_kit_token', '3': 4, '4': 1, '5': 9, '10': 'pushKitToken'},
  ],
};

/// Descriptor for `UpdateDeviceTokenRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List updateDeviceTokenRequestDescriptor = $convert.base64Decode(
    'ChhVcGRhdGVEZXZpY2VUb2tlblJlcXVlc3QSGwoJZGV2aWNlX2lkGAEgASgJUghkZXZpY2VJZB'
    'IbCglmY21fdG9rZW4YAiABKAlSCGZjbVRva2VuEh0KCmFwbnNfdG9rZW4YAyABKAlSCWFwbnNU'
    'b2tlbhIkCg5wdXNoX2tpdF90b2tlbhgEIAEoCVIMcHVzaEtpdFRva2Vu');

@$core.Deprecated('Use updateDeviceTokenResponseDescriptor instead')
const UpdateDeviceTokenResponse$json = {
  '1': 'UpdateDeviceTokenResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `UpdateDeviceTokenResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List updateDeviceTokenResponseDescriptor = $convert.base64Decode(
    'ChlVcGRhdGVEZXZpY2VUb2tlblJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb2'
    '4uRXJyb3JDb2RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use getUserDevicesRequestDescriptor instead')
const GetUserDevicesRequest$json = {
  '1': 'GetUserDevicesRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetUserDevicesRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUserDevicesRequestDescriptor = $convert.base64Decode(
    'ChVHZXRVc2VyRGV2aWNlc1JlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklk');

@$core.Deprecated('Use getUserDevicesResponseDescriptor instead')
const GetUserDevicesResponse$json = {
  '1': 'GetUserDevicesResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'devices', '3': 2, '4': 3, '5': 11, '6': '.chirp.app_notification.DeviceInfo', '10': 'devices'},
  ],
};

/// Descriptor for `GetUserDevicesResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getUserDevicesResponseDescriptor = $convert.base64Decode(
    'ChZHZXRVc2VyRGV2aWNlc1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEjwKB2RldmljZXMYAiADKAsyIi5jaGlycC5hcHBfbm90aWZpY2F0aW9u'
    'LkRldmljZUluZm9SB2RldmljZXM=');

@$core.Deprecated('Use deviceInfoDescriptor instead')
const DeviceInfo$json = {
  '1': 'DeviceInfo',
  '2': [
    {'1': 'device_id', '3': 1, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'platform', '3': 3, '4': 1, '5': 9, '10': 'platform'},
    {'1': 'app_version', '3': 4, '4': 1, '5': 9, '10': 'appVersion'},
    {'1': 'os_version', '3': 5, '4': 1, '5': 9, '10': 'osVersion'},
    {'1': 'device_name', '3': 6, '4': 1, '5': 9, '10': 'deviceName'},
    {'1': 'registered_at', '3': 7, '4': 1, '5': 3, '10': 'registeredAt'},
    {'1': 'is_active', '3': 8, '4': 1, '5': 8, '10': 'isActive'},
  ],
};

/// Descriptor for `DeviceInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deviceInfoDescriptor = $convert.base64Decode(
    'CgpEZXZpY2VJbmZvEhsKCWRldmljZV9pZBgBIAEoCVIIZGV2aWNlSWQSFwoHdXNlcl9pZBgCIA'
    'EoCVIGdXNlcklkEhoKCHBsYXRmb3JtGAMgASgJUghwbGF0Zm9ybRIfCgthcHBfdmVyc2lvbhgE'
    'IAEoCVIKYXBwVmVyc2lvbhIdCgpvc192ZXJzaW9uGAUgASgJUglvc1ZlcnNpb24SHwoLZGV2aW'
    'NlX25hbWUYBiABKAlSCmRldmljZU5hbWUSIwoNcmVnaXN0ZXJlZF9hdBgHIAEoA1IMcmVnaXN0'
    'ZXJlZEF0EhsKCWlzX2FjdGl2ZRgIIAEoCFIIaXNBY3RpdmU=');

@$core.Deprecated('Use setBadgeCountRequestDescriptor instead')
const SetBadgeCountRequest$json = {
  '1': 'SetBadgeCountRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'count', '3': 2, '4': 1, '5': 5, '10': 'count'},
  ],
};

/// Descriptor for `SetBadgeCountRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setBadgeCountRequestDescriptor = $convert.base64Decode(
    'ChRTZXRCYWRnZUNvdW50UmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSFAoFY291bn'
    'QYAiABKAVSBWNvdW50');

@$core.Deprecated('Use setBadgeCountResponseDescriptor instead')
const SetBadgeCountResponse$json = {
  '1': 'SetBadgeCountResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `SetBadgeCountResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setBadgeCountResponseDescriptor = $convert.base64Decode(
    'ChVTZXRCYWRnZUNvdW50UmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use silentNotificationRequestDescriptor instead')
const SilentNotificationRequest$json = {
  '1': 'SilentNotificationRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'data', '3': 2, '4': 3, '5': 11, '6': '.chirp.app_notification.SilentNotificationRequest.DataEntry', '10': 'data'},
    {'1': 'ttl_ms', '3': 3, '4': 1, '5': 3, '10': 'ttlMs'},
  ],
  '3': [SilentNotificationRequest_DataEntry$json],
};

@$core.Deprecated('Use silentNotificationRequestDescriptor instead')
const SilentNotificationRequest_DataEntry$json = {
  '1': 'DataEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 9, '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `SilentNotificationRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List silentNotificationRequestDescriptor = $convert.base64Decode(
    'ChlTaWxlbnROb3RpZmljYXRpb25SZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBJPCg'
    'RkYXRhGAIgAygLMjsuY2hpcnAuYXBwX25vdGlmaWNhdGlvbi5TaWxlbnROb3RpZmljYXRpb25S'
    'ZXF1ZXN0LkRhdGFFbnRyeVIEZGF0YRIVCgZ0dGxfbXMYAyABKANSBXR0bE1zGjcKCURhdGFFbn'
    'RyeRIQCgNrZXkYASABKAlSA2tleRIUCgV2YWx1ZRgCIAEoCVIFdmFsdWU6AjgB');

@$core.Deprecated('Use notificationPreferencesDescriptor instead')
const NotificationPreferences$json = {
  '1': 'NotificationPreferences',
  '2': [
    {'1': 'enabled', '3': 1, '4': 1, '5': 8, '10': 'enabled'},
    {'1': 'sound_enabled', '3': 2, '4': 1, '5': 8, '10': 'soundEnabled'},
    {'1': 'vibration_enabled', '3': 3, '4': 1, '5': 8, '10': 'vibrationEnabled'},
    {'1': 'show_preview', '3': 4, '4': 1, '5': 8, '10': 'showPreview'},
    {'1': 'allow_messages', '3': 5, '4': 1, '5': 8, '10': 'allowMessages'},
    {'1': 'allow_mentions', '3': 6, '4': 1, '5': 8, '10': 'allowMentions'},
    {'1': 'allow_friend_requests', '3': 7, '4': 1, '5': 8, '10': 'allowFriendRequests'},
    {'1': 'allow_voice_calls', '3': 8, '4': 1, '5': 8, '10': 'allowVoiceCalls'},
    {'1': 'dnd_enabled', '3': 9, '4': 1, '5': 8, '10': 'dndEnabled'},
    {'1': 'dnd_start_hour', '3': 10, '4': 1, '5': 5, '10': 'dndStartHour'},
    {'1': 'dnd_end_hour', '3': 11, '4': 1, '5': 5, '10': 'dndEndHour'},
    {'1': 'dnd_days', '3': 12, '4': 3, '5': 5, '10': 'dndDays'},
    {'1': 'channel_settings', '3': 13, '4': 3, '5': 11, '6': '.chirp.app_notification.NotificationPreferences.ChannelSettingsEntry', '10': 'channelSettings'},
  ],
  '3': [NotificationPreferences_ChannelSettingsEntry$json],
};

@$core.Deprecated('Use notificationPreferencesDescriptor instead')
const NotificationPreferences_ChannelSettingsEntry$json = {
  '1': 'ChannelSettingsEntry',
  '2': [
    {'1': 'key', '3': 1, '4': 1, '5': 9, '10': 'key'},
    {'1': 'value', '3': 2, '4': 1, '5': 11, '6': '.chirp.app_notification.ChannelNotificationSettings', '10': 'value'},
  ],
  '7': {'7': true},
};

/// Descriptor for `NotificationPreferences`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List notificationPreferencesDescriptor = $convert.base64Decode(
    'ChdOb3RpZmljYXRpb25QcmVmZXJlbmNlcxIYCgdlbmFibGVkGAEgASgIUgdlbmFibGVkEiMKDX'
    'NvdW5kX2VuYWJsZWQYAiABKAhSDHNvdW5kRW5hYmxlZBIrChF2aWJyYXRpb25fZW5hYmxlZBgD'
    'IAEoCFIQdmlicmF0aW9uRW5hYmxlZBIhCgxzaG93X3ByZXZpZXcYBCABKAhSC3Nob3dQcmV2aW'
    'V3EiUKDmFsbG93X21lc3NhZ2VzGAUgASgIUg1hbGxvd01lc3NhZ2VzEiUKDmFsbG93X21lbnRp'
    'b25zGAYgASgIUg1hbGxvd01lbnRpb25zEjIKFWFsbG93X2ZyaWVuZF9yZXF1ZXN0cxgHIAEoCF'
    'ITYWxsb3dGcmllbmRSZXF1ZXN0cxIqChFhbGxvd192b2ljZV9jYWxscxgIIAEoCFIPYWxsb3dW'
    'b2ljZUNhbGxzEh8KC2RuZF9lbmFibGVkGAkgASgIUgpkbmRFbmFibGVkEiQKDmRuZF9zdGFydF'
    '9ob3VyGAogASgFUgxkbmRTdGFydEhvdXISIAoMZG5kX2VuZF9ob3VyGAsgASgFUgpkbmRFbmRI'
    'b3VyEhkKCGRuZF9kYXlzGAwgAygFUgdkbmREYXlzEm8KEGNoYW5uZWxfc2V0dGluZ3MYDSADKA'
    'syRC5jaGlycC5hcHBfbm90aWZpY2F0aW9uLk5vdGlmaWNhdGlvblByZWZlcmVuY2VzLkNoYW5u'
    'ZWxTZXR0aW5nc0VudHJ5Ug9jaGFubmVsU2V0dGluZ3MadwoUQ2hhbm5lbFNldHRpbmdzRW50cn'
    'kSEAoDa2V5GAEgASgJUgNrZXkSSQoFdmFsdWUYAiABKAsyMy5jaGlycC5hcHBfbm90aWZpY2F0'
    'aW9uLkNoYW5uZWxOb3RpZmljYXRpb25TZXR0aW5nc1IFdmFsdWU6AjgB');

@$core.Deprecated('Use channelNotificationSettingsDescriptor instead')
const ChannelNotificationSettings$json = {
  '1': 'ChannelNotificationSettings',
  '2': [
    {'1': 'muted', '3': 1, '4': 1, '5': 8, '10': 'muted'},
    {'1': 'only_mentions', '3': 2, '4': 1, '5': 8, '10': 'onlyMentions'},
    {'1': 'notify_on_mentions', '3': 3, '4': 1, '5': 8, '10': 'notifyOnMentions'},
  ],
};

/// Descriptor for `ChannelNotificationSettings`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List channelNotificationSettingsDescriptor = $convert.base64Decode(
    'ChtDaGFubmVsTm90aWZpY2F0aW9uU2V0dGluZ3MSFAoFbXV0ZWQYASABKAhSBW11dGVkEiMKDW'
    '9ubHlfbWVudGlvbnMYAiABKAhSDG9ubHlNZW50aW9ucxIsChJub3RpZnlfb25fbWVudGlvbnMY'
    'AyABKAhSEG5vdGlmeU9uTWVudGlvbnM=');

@$core.Deprecated('Use setPreferencesRequestDescriptor instead')
const SetPreferencesRequest$json = {
  '1': 'SetPreferencesRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'preferences', '3': 2, '4': 1, '5': 11, '6': '.chirp.app_notification.NotificationPreferences', '10': 'preferences'},
  ],
};

/// Descriptor for `SetPreferencesRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setPreferencesRequestDescriptor = $convert.base64Decode(
    'ChVTZXRQcmVmZXJlbmNlc1JlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkElEKC3ByZW'
    'ZlcmVuY2VzGAIgASgLMi8uY2hpcnAuYXBwX25vdGlmaWNhdGlvbi5Ob3RpZmljYXRpb25QcmVm'
    'ZXJlbmNlc1ILcHJlZmVyZW5jZXM=');

@$core.Deprecated('Use setPreferencesResponseDescriptor instead')
const SetPreferencesResponse$json = {
  '1': 'SetPreferencesResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `SetPreferencesResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setPreferencesResponseDescriptor = $convert.base64Decode(
    'ChZTZXRQcmVmZXJlbmNlc1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1l');

@$core.Deprecated('Use getPreferencesRequestDescriptor instead')
const GetPreferencesRequest$json = {
  '1': 'GetPreferencesRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetPreferencesRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPreferencesRequestDescriptor = $convert.base64Decode(
    'ChVHZXRQcmVmZXJlbmNlc1JlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklk');

@$core.Deprecated('Use getPreferencesResponseDescriptor instead')
const GetPreferencesResponse$json = {
  '1': 'GetPreferencesResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'preferences', '3': 2, '4': 1, '5': 11, '6': '.chirp.app_notification.NotificationPreferences', '10': 'preferences'},
  ],
};

/// Descriptor for `GetPreferencesResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getPreferencesResponseDescriptor = $convert.base64Decode(
    'ChZHZXRQcmVmZXJlbmNlc1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlElEKC3ByZWZlcmVuY2VzGAIgASgLMi8uY2hpcnAuYXBwX25vdGlmaWNh'
    'dGlvbi5Ob3RpZmljYXRpb25QcmVmZXJlbmNlc1ILcHJlZmVyZW5jZXM=');

