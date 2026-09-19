//
//  Generated code. Do not modify.
//  source: proto/common.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

class ErrorCode extends $pb.ProtobufEnum {
  static const ErrorCode OK = ErrorCode._(0, _omitEnumNames ? '' : 'OK');
  static const ErrorCode INTERNAL_ERROR = ErrorCode._(1, _omitEnumNames ? '' : 'INTERNAL_ERROR');
  static const ErrorCode INVALID_PARAM = ErrorCode._(2, _omitEnumNames ? '' : 'INVALID_PARAM');
  static const ErrorCode AUTH_FAILED = ErrorCode._(3, _omitEnumNames ? '' : 'AUTH_FAILED');
  static const ErrorCode SESSION_EXPIRED = ErrorCode._(4, _omitEnumNames ? '' : 'SESSION_EXPIRED');
  static const ErrorCode USER_NOT_FOUND = ErrorCode._(5, _omitEnumNames ? '' : 'USER_NOT_FOUND');
  static const ErrorCode TARGET_OFFLINE = ErrorCode._(6, _omitEnumNames ? '' : 'TARGET_OFFLINE');
  static const ErrorCode SERVER_UNAVAILABLE = ErrorCode._(7, _omitEnumNames ? '' : 'SERVER_UNAVAILABLE');
  static const ErrorCode RATE_LIMITED = ErrorCode._(8, _omitEnumNames ? '' : 'RATE_LIMITED');

  static const $core.List<ErrorCode> values = <ErrorCode> [
    OK,
    INTERNAL_ERROR,
    INVALID_PARAM,
    AUTH_FAILED,
    SESSION_EXPIRED,
    USER_NOT_FOUND,
    TARGET_OFFLINE,
    SERVER_UNAVAILABLE,
    RATE_LIMITED,
  ];

  static final $core.Map<$core.int, ErrorCode> _byValue = $pb.ProtobufEnum.initByValue(values);
  static ErrorCode? valueOf($core.int value) => _byValue[value];

  const ErrorCode._($core.int v, $core.String n) : super(v, n);
}


const _omitEnumNames = $core.bool.fromEnvironment('protobuf.omit_enum_names');
