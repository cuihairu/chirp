//
//  Generated code. Do not modify.
//  source: proto/social.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

/// User presence states
class PresenceStatus extends $pb.ProtobufEnum {
  static const PresenceStatus OFFLINE = PresenceStatus._(0, _omitEnumNames ? '' : 'OFFLINE');
  static const PresenceStatus ONLINE = PresenceStatus._(1, _omitEnumNames ? '' : 'ONLINE');
  static const PresenceStatus AWAY = PresenceStatus._(2, _omitEnumNames ? '' : 'AWAY');
  static const PresenceStatus DND = PresenceStatus._(3, _omitEnumNames ? '' : 'DND');
  static const PresenceStatus IN_GAME = PresenceStatus._(4, _omitEnumNames ? '' : 'IN_GAME');
  static const PresenceStatus IN_BATTLE = PresenceStatus._(5, _omitEnumNames ? '' : 'IN_BATTLE');

  static const $core.List<PresenceStatus> values = <PresenceStatus> [
    OFFLINE,
    ONLINE,
    AWAY,
    DND,
    IN_GAME,
    IN_BATTLE,
  ];

  static final $core.Map<$core.int, PresenceStatus> _byValue = $pb.ProtobufEnum.initByValue(values);
  static PresenceStatus? valueOf($core.int value) => _byValue[value];

  const PresenceStatus._($core.int v, $core.String n) : super(v, n);
}

/// Friend relationship
class FriendStatus extends $pb.ProtobufEnum {
  static const FriendStatus NONE = FriendStatus._(0, _omitEnumNames ? '' : 'NONE');
  static const FriendStatus PENDING = FriendStatus._(1, _omitEnumNames ? '' : 'PENDING');
  static const FriendStatus ACCEPTED = FriendStatus._(2, _omitEnumNames ? '' : 'ACCEPTED');
  static const FriendStatus BLOCKED = FriendStatus._(3, _omitEnumNames ? '' : 'BLOCKED');

  static const $core.List<FriendStatus> values = <FriendStatus> [
    NONE,
    PENDING,
    ACCEPTED,
    BLOCKED,
  ];

  static final $core.Map<$core.int, FriendStatus> _byValue = $pb.ProtobufEnum.initByValue(values);
  static FriendStatus? valueOf($core.int value) => _byValue[value];

  const FriendStatus._($core.int v, $core.String n) : super(v, n);
}


const _omitEnumNames = $core.bool.fromEnvironment('protobuf.omit_enum_names');
