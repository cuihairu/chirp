//
//  Generated code. Do not modify.
//  source: proto/auth.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use loginRequestDescriptor instead')
const LoginRequest$json = {
  '1': 'LoginRequest',
  '2': [
    {'1': 'token', '3': 1, '4': 1, '5': 9, '10': 'token'},
    {'1': 'device_id', '3': 2, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'platform', '3': 3, '4': 1, '5': 9, '10': 'platform'},
    {'1': 'supports_message_ack', '3': 4, '4': 1, '5': 8, '10': 'supportsMessageAck'},
  ],
};

/// Descriptor for `LoginRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List loginRequestDescriptor = $convert.base64Decode(
    'CgxMb2dpblJlcXVlc3QSFAoFdG9rZW4YASABKAlSBXRva2VuEhsKCWRldmljZV9pZBgCIAEoCV'
    'IIZGV2aWNlSWQSGgoIcGxhdGZvcm0YAyABKAlSCHBsYXRmb3JtEjAKFHN1cHBvcnRzX21lc3Nh'
    'Z2VfYWNrGAQgASgIUhJzdXBwb3J0c01lc3NhZ2VBY2s=');

@$core.Deprecated('Use kickNotifyDescriptor instead')
const KickNotify$json = {
  '1': 'KickNotify',
  '2': [
    {'1': 'reason', '3': 1, '4': 1, '5': 9, '10': 'reason'},
  ],
};

/// Descriptor for `KickNotify`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List kickNotifyDescriptor = $convert.base64Decode(
    'CgpLaWNrTm90aWZ5EhYKBnJlYXNvbhgBIAEoCVIGcmVhc29u');

@$core.Deprecated('Use loginResponseDescriptor instead')
const LoginResponse$json = {
  '1': 'LoginResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'session_id', '3': 2, '4': 1, '5': 9, '10': 'sessionId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
    {'1': 'user_id', '3': 4, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'kick_previous', '3': 5, '4': 1, '5': 8, '10': 'kickPrevious'},
    {'1': 'kick', '3': 6, '4': 1, '5': 11, '6': '.chirp.auth.KickNotify', '10': 'kick'},
  ],
};

/// Descriptor for `LoginResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List loginResponseDescriptor = $convert.base64Decode(
    'Cg1Mb2dpblJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3JDb2RlUg'
    'Rjb2RlEh0KCnNlc3Npb25faWQYAiABKAlSCXNlc3Npb25JZBIfCgtzZXJ2ZXJfdGltZRgDIAEo'
    'A1IKc2VydmVyVGltZRIXCgd1c2VyX2lkGAQgASgJUgZ1c2VySWQSIwoNa2lja19wcmV2aW91cx'
    'gFIAEoCFIMa2lja1ByZXZpb3VzEioKBGtpY2sYBiABKAsyFi5jaGlycC5hdXRoLktpY2tOb3Rp'
    'ZnlSBGtpY2s=');

@$core.Deprecated('Use logoutRequestDescriptor instead')
const LogoutRequest$json = {
  '1': 'LogoutRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'session_id', '3': 2, '4': 1, '5': 9, '10': 'sessionId'},
  ],
};

/// Descriptor for `LogoutRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List logoutRequestDescriptor = $convert.base64Decode(
    'Cg1Mb2dvdXRSZXF1ZXN0EhcKB3VzZXJfaWQYASABKAlSBnVzZXJJZBIdCgpzZXNzaW9uX2lkGA'
    'IgASgJUglzZXNzaW9uSWQ=');

@$core.Deprecated('Use logoutResponseDescriptor instead')
const LogoutResponse$json = {
  '1': 'LogoutResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `LogoutResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List logoutResponseDescriptor = $convert.base64Decode(
    'Cg5Mb2dvdXRSZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm9yQ29kZV'
    'IEY29kZRIfCgtzZXJ2ZXJfdGltZRgCIAEoA1IKc2VydmVyVGltZQ==');

@$core.Deprecated('Use registerRequestDescriptor instead')
const RegisterRequest$json = {
  '1': 'RegisterRequest',
  '2': [
    {'1': 'username', '3': 1, '4': 1, '5': 9, '10': 'username'},
    {'1': 'email', '3': 2, '4': 1, '5': 9, '10': 'email'},
    {'1': 'password', '3': 3, '4': 1, '5': 9, '10': 'password'},
    {'1': 'display_name', '3': 4, '4': 1, '5': 9, '10': 'displayName'},
  ],
};

/// Descriptor for `RegisterRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List registerRequestDescriptor = $convert.base64Decode(
    'Cg9SZWdpc3RlclJlcXVlc3QSGgoIdXNlcm5hbWUYASABKAlSCHVzZXJuYW1lEhQKBWVtYWlsGA'
    'IgASgJUgVlbWFpbBIaCghwYXNzd29yZBgDIAEoCVIIcGFzc3dvcmQSIQoMZGlzcGxheV9uYW1l'
    'GAQgASgJUgtkaXNwbGF5TmFtZQ==');

@$core.Deprecated('Use registerResponseDescriptor instead')
const RegisterResponse$json = {
  '1': 'RegisterResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
    {'1': 'error_message', '3': 4, '4': 1, '5': 9, '10': 'errorMessage'},
  ],
};

/// Descriptor for `RegisterResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List registerResponseDescriptor = $convert.base64Decode(
    'ChBSZWdpc3RlclJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3JDb2'
    'RlUgRjb2RlEhcKB3VzZXJfaWQYAiABKAlSBnVzZXJJZBIfCgtzZXJ2ZXJfdGltZRgDIAEoA1IK'
    'c2VydmVyVGltZRIjCg1lcnJvcl9tZXNzYWdlGAQgASgJUgxlcnJvck1lc3NhZ2U=');

@$core.Deprecated('Use passwordLoginRequestDescriptor instead')
const PasswordLoginRequest$json = {
  '1': 'PasswordLoginRequest',
  '2': [
    {'1': 'identifier', '3': 1, '4': 1, '5': 9, '10': 'identifier'},
    {'1': 'password', '3': 2, '4': 1, '5': 9, '10': 'password'},
    {'1': 'device_id', '3': 3, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'platform', '3': 4, '4': 1, '5': 9, '10': 'platform'},
  ],
};

/// Descriptor for `PasswordLoginRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List passwordLoginRequestDescriptor = $convert.base64Decode(
    'ChRQYXNzd29yZExvZ2luUmVxdWVzdBIeCgppZGVudGlmaWVyGAEgASgJUgppZGVudGlmaWVyEh'
    'oKCHBhc3N3b3JkGAIgASgJUghwYXNzd29yZBIbCglkZXZpY2VfaWQYAyABKAlSCGRldmljZUlk'
    'EhoKCHBsYXRmb3JtGAQgASgJUghwbGF0Zm9ybQ==');

@$core.Deprecated('Use passwordLoginResponseDescriptor instead')
const PasswordLoginResponse$json = {
  '1': 'PasswordLoginResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'user_id', '3': 2, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'username', '3': 3, '4': 1, '5': 9, '10': 'username'},
    {'1': 'session_id', '3': 4, '4': 1, '5': 9, '10': 'sessionId'},
    {'1': 'access_token', '3': 5, '4': 1, '5': 9, '10': 'accessToken'},
    {'1': 'refresh_token', '3': 6, '4': 1, '5': 9, '10': 'refreshToken'},
    {'1': 'access_token_expires_at', '3': 7, '4': 1, '5': 3, '10': 'accessTokenExpiresAt'},
    {'1': 'refresh_token_expires_at', '3': 8, '4': 1, '5': 3, '10': 'refreshTokenExpiresAt'},
    {'1': 'server_time', '3': 9, '4': 1, '5': 3, '10': 'serverTime'},
    {'1': 'kick_previous', '3': 10, '4': 1, '5': 8, '10': 'kickPrevious'},
    {'1': 'error_message', '3': 11, '4': 1, '5': 9, '10': 'errorMessage'},
  ],
};

/// Descriptor for `PasswordLoginResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List passwordLoginResponseDescriptor = $convert.base64Decode(
    'ChVQYXNzd29yZExvZ2luUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSFwoHdXNlcl9pZBgCIAEoCVIGdXNlcklkEhoKCHVzZXJuYW1lGAMgASgJ'
    'Ugh1c2VybmFtZRIdCgpzZXNzaW9uX2lkGAQgASgJUglzZXNzaW9uSWQSIQoMYWNjZXNzX3Rva2'
    'VuGAUgASgJUgthY2Nlc3NUb2tlbhIjCg1yZWZyZXNoX3Rva2VuGAYgASgJUgxyZWZyZXNoVG9r'
    'ZW4SNQoXYWNjZXNzX3Rva2VuX2V4cGlyZXNfYXQYByABKANSFGFjY2Vzc1Rva2VuRXhwaXJlc0'
    'F0EjcKGHJlZnJlc2hfdG9rZW5fZXhwaXJlc19hdBgIIAEoA1IVcmVmcmVzaFRva2VuRXhwaXJl'
    'c0F0Eh8KC3NlcnZlcl90aW1lGAkgASgDUgpzZXJ2ZXJUaW1lEiMKDWtpY2tfcHJldmlvdXMYCi'
    'ABKAhSDGtpY2tQcmV2aW91cxIjCg1lcnJvcl9tZXNzYWdlGAsgASgJUgxlcnJvck1lc3NhZ2U=');

@$core.Deprecated('Use refreshTokenRequestDescriptor instead')
const RefreshTokenRequest$json = {
  '1': 'RefreshTokenRequest',
  '2': [
    {'1': 'refresh_token', '3': 1, '4': 1, '5': 9, '10': 'refreshToken'},
  ],
};

/// Descriptor for `RefreshTokenRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List refreshTokenRequestDescriptor = $convert.base64Decode(
    'ChNSZWZyZXNoVG9rZW5SZXF1ZXN0EiMKDXJlZnJlc2hfdG9rZW4YASABKAlSDHJlZnJlc2hUb2'
    'tlbg==');

@$core.Deprecated('Use refreshTokenResponseDescriptor instead')
const RefreshTokenResponse$json = {
  '1': 'RefreshTokenResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'access_token', '3': 2, '4': 1, '5': 9, '10': 'accessToken'},
    {'1': 'access_token_expires_at', '3': 3, '4': 1, '5': 3, '10': 'accessTokenExpiresAt'},
    {'1': 'server_time', '3': 4, '4': 1, '5': 3, '10': 'serverTime'},
    {'1': 'error_message', '3': 5, '4': 1, '5': 9, '10': 'errorMessage'},
  ],
};

/// Descriptor for `RefreshTokenResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List refreshTokenResponseDescriptor = $convert.base64Decode(
    'ChRSZWZyZXNoVG9rZW5SZXNwb25zZRIrCgRjb2RlGAEgASgOMhcuY2hpcnAuY29tbW9uLkVycm'
    '9yQ29kZVIEY29kZRIhCgxhY2Nlc3NfdG9rZW4YAiABKAlSC2FjY2Vzc1Rva2VuEjUKF2FjY2Vz'
    'c190b2tlbl9leHBpcmVzX2F0GAMgASgDUhRhY2Nlc3NUb2tlbkV4cGlyZXNBdBIfCgtzZXJ2ZX'
    'JfdGltZRgEIAEoA1IKc2VydmVyVGltZRIjCg1lcnJvcl9tZXNzYWdlGAUgASgJUgxlcnJvck1l'
    'c3NhZ2U=');

@$core.Deprecated('Use getSessionsRequestDescriptor instead')
const GetSessionsRequest$json = {
  '1': 'GetSessionsRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
  ],
};

/// Descriptor for `GetSessionsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getSessionsRequestDescriptor = $convert.base64Decode(
    'ChJHZXRTZXNzaW9uc1JlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklk');

@$core.Deprecated('Use sessionInfoDescriptor instead')
const SessionInfo$json = {
  '1': 'SessionInfo',
  '2': [
    {'1': 'session_id', '3': 1, '4': 1, '5': 9, '10': 'sessionId'},
    {'1': 'device_id', '3': 2, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'platform', '3': 3, '4': 1, '5': 9, '10': 'platform'},
    {'1': 'created_at', '3': 4, '4': 1, '5': 3, '10': 'createdAt'},
    {'1': 'last_activity_at', '3': 5, '4': 1, '5': 3, '10': 'lastActivityAt'},
    {'1': 'is_current', '3': 6, '4': 1, '5': 8, '10': 'isCurrent'},
  ],
};

/// Descriptor for `SessionInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List sessionInfoDescriptor = $convert.base64Decode(
    'CgtTZXNzaW9uSW5mbxIdCgpzZXNzaW9uX2lkGAEgASgJUglzZXNzaW9uSWQSGwoJZGV2aWNlX2'
    'lkGAIgASgJUghkZXZpY2VJZBIaCghwbGF0Zm9ybRgDIAEoCVIIcGxhdGZvcm0SHQoKY3JlYXRl'
    'ZF9hdBgEIAEoA1IJY3JlYXRlZEF0EigKEGxhc3RfYWN0aXZpdHlfYXQYBSABKANSDmxhc3RBY3'
    'Rpdml0eUF0Eh0KCmlzX2N1cnJlbnQYBiABKAhSCWlzQ3VycmVudA==');

@$core.Deprecated('Use getSessionsResponseDescriptor instead')
const GetSessionsResponse$json = {
  '1': 'GetSessionsResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'sessions', '3': 2, '4': 3, '5': 11, '6': '.chirp.auth.SessionInfo', '10': 'sessions'},
    {'1': 'server_time', '3': 3, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `GetSessionsResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getSessionsResponseDescriptor = $convert.base64Decode(
    'ChNHZXRTZXNzaW9uc1Jlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRXJyb3'
    'JDb2RlUgRjb2RlEjMKCHNlc3Npb25zGAIgAygLMhcuY2hpcnAuYXV0aC5TZXNzaW9uSW5mb1II'
    'c2Vzc2lvbnMSHwoLc2VydmVyX3RpbWUYAyABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use revokeSessionRequestDescriptor instead')
const RevokeSessionRequest$json = {
  '1': 'RevokeSessionRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'session_id', '3': 2, '4': 1, '5': 9, '10': 'sessionId'},
  ],
};

/// Descriptor for `RevokeSessionRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List revokeSessionRequestDescriptor = $convert.base64Decode(
    'ChRSZXZva2VTZXNzaW9uUmVxdWVzdBIXCgd1c2VyX2lkGAEgASgJUgZ1c2VySWQSHQoKc2Vzc2'
    'lvbl9pZBgCIAEoCVIJc2Vzc2lvbklk');

@$core.Deprecated('Use revokeSessionResponseDescriptor instead')
const RevokeSessionResponse$json = {
  '1': 'RevokeSessionResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
  ],
};

/// Descriptor for `RevokeSessionResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List revokeSessionResponseDescriptor = $convert.base64Decode(
    'ChVSZXZva2VTZXNzaW9uUmVzcG9uc2USKwoEY29kZRgBIAEoDjIXLmNoaXJwLmNvbW1vbi5Fcn'
    'JvckNvZGVSBGNvZGUSHwoLc2VydmVyX3RpbWUYAiABKANSCnNlcnZlclRpbWU=');

@$core.Deprecated('Use changePasswordRequestDescriptor instead')
const ChangePasswordRequest$json = {
  '1': 'ChangePasswordRequest',
  '2': [
    {'1': 'user_id', '3': 1, '4': 1, '5': 9, '10': 'userId'},
    {'1': 'old_password', '3': 2, '4': 1, '5': 9, '10': 'oldPassword'},
    {'1': 'new_password', '3': 3, '4': 1, '5': 9, '10': 'newPassword'},
  ],
};

/// Descriptor for `ChangePasswordRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List changePasswordRequestDescriptor = $convert.base64Decode(
    'ChVDaGFuZ2VQYXNzd29yZFJlcXVlc3QSFwoHdXNlcl9pZBgBIAEoCVIGdXNlcklkEiEKDG9sZF'
    '9wYXNzd29yZBgCIAEoCVILb2xkUGFzc3dvcmQSIQoMbmV3X3Bhc3N3b3JkGAMgASgJUgtuZXdQ'
    'YXNzd29yZA==');

@$core.Deprecated('Use changePasswordResponseDescriptor instead')
const ChangePasswordResponse$json = {
  '1': 'ChangePasswordResponse',
  '2': [
    {'1': 'code', '3': 1, '4': 1, '5': 14, '6': '.chirp.common.ErrorCode', '10': 'code'},
    {'1': 'server_time', '3': 2, '4': 1, '5': 3, '10': 'serverTime'},
    {'1': 'error_message', '3': 3, '4': 1, '5': 9, '10': 'errorMessage'},
  ],
};

/// Descriptor for `ChangePasswordResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List changePasswordResponseDescriptor = $convert.base64Decode(
    'ChZDaGFuZ2VQYXNzd29yZFJlc3BvbnNlEisKBGNvZGUYASABKA4yFy5jaGlycC5jb21tb24uRX'
    'Jyb3JDb2RlUgRjb2RlEh8KC3NlcnZlcl90aW1lGAIgASgDUgpzZXJ2ZXJUaW1lEiMKDWVycm9y'
    'X21lc3NhZ2UYAyABKAlSDGVycm9yTWVzc2FnZQ==');

