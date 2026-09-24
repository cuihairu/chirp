//
//  Generated code. Do not modify.
//  source: proto/common.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use errorCodeDescriptor instead')
const ErrorCode$json = {
  '1': 'ErrorCode',
  '2': [
    {'1': 'OK', '2': 0},
    {'1': 'INTERNAL_ERROR', '2': 1},
    {'1': 'INVALID_PARAM', '2': 2},
    {'1': 'AUTH_FAILED', '2': 3},
    {'1': 'SESSION_EXPIRED', '2': 4},
    {'1': 'USER_NOT_FOUND', '2': 5},
    {'1': 'TARGET_OFFLINE', '2': 6},
    {'1': 'SERVER_UNAVAILABLE', '2': 7},
    {'1': 'RATE_LIMITED', '2': 8},
    {'1': 'VERSION_MISMATCH', '2': 9},
    {'1': 'WORD_FILTERED', '2': 10},
    {'1': 'CONTENT_TOO_LONG', '2': 11},
  ],
};

/// Descriptor for `ErrorCode`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List errorCodeDescriptor = $convert.base64Decode(
    'CglFcnJvckNvZGUSBgoCT0sQABISCg5JTlRFUk5BTF9FUlJPUhABEhEKDUlOVkFMSURfUEFSQU'
    '0QAhIPCgtBVVRIX0ZBSUxFRBADEhMKD1NFU1NJT05fRVhQSVJFRBAEEhIKDlVTRVJfTk9UX0ZP'
    'VU5EEAUSEgoOVEFSR0VUX09GRkxJTkUQBhIWChJTRVJWRVJfVU5BVkFJTEFCTEUQBxIQCgxSQV'
    'RFX0xJTUlURUQQCBIUChBWRVJTSU9OX01JU01BVENIEAkSEQoNV09SRF9GSUxURVJFRBAKEhQK'
    'EENPTlRFTlRfVE9PX0xPTkcQCw==');

@$core.Deprecated('Use emptyDescriptor instead')
const Empty$json = {
  '1': 'Empty',
};

/// Descriptor for `Empty`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List emptyDescriptor = $convert.base64Decode(
    'CgVFbXB0eQ==');

