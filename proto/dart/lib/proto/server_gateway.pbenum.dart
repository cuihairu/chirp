//
//  Generated code. Do not modify.
//  source: proto/server_gateway.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

/// Identity of an injected message's sender. Server-plane senders are not user
/// accounts; chat applies permission rules distinct from user accounts to them.
class SenderKind extends $pb.ProtobufEnum {
  static const SenderKind SENDER_UNKNOWN = SenderKind._(0, _omitEnumNames ? '' : 'SENDER_UNKNOWN');
  static const SenderKind SENDER_SYSTEM = SenderKind._(1, _omitEnumNames ? '' : 'SENDER_SYSTEM');
  static const SenderKind SENDER_NPC = SenderKind._(2, _omitEnumNames ? '' : 'SENDER_NPC');
  static const SenderKind SENDER_SERVICE = SenderKind._(3, _omitEnumNames ? '' : 'SENDER_SERVICE');

  static const $core.List<SenderKind> values = <SenderKind> [
    SENDER_UNKNOWN,
    SENDER_SYSTEM,
    SENDER_NPC,
    SENDER_SERVICE,
  ];

  static final $core.Map<$core.int, SenderKind> _byValue = $pb.ProtobufEnum.initByValue(values);
  static SenderKind? valueOf($core.int value) => _byValue[value];

  const SenderKind._($core.int v, $core.String n) : super(v, n);
}


const _omitEnumNames = $core.bool.fromEnvironment('protobuf.omit_enum_names');
