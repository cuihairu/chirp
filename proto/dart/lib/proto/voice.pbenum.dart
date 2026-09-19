//
//  Generated code. Do not modify.
//  source: proto/voice.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

/// Voice room types
class RoomType extends $pb.ProtobufEnum {
  static const RoomType PEER_TO_PEER = RoomType._(0, _omitEnumNames ? '' : 'PEER_TO_PEER');
  static const RoomType GROUP = RoomType._(1, _omitEnumNames ? '' : 'GROUP');
  static const RoomType CHANNEL = RoomType._(2, _omitEnumNames ? '' : 'CHANNEL');

  static const $core.List<RoomType> values = <RoomType> [
    PEER_TO_PEER,
    GROUP,
    CHANNEL,
  ];

  static final $core.Map<$core.int, RoomType> _byValue = $pb.ProtobufEnum.initByValue(values);
  static RoomType? valueOf($core.int value) => _byValue[value];

  const RoomType._($core.int v, $core.String n) : super(v, n);
}

/// Participant state in voice room
class ParticipantState extends $pb.ProtobufEnum {
  static const ParticipantState JOINING = ParticipantState._(0, _omitEnumNames ? '' : 'JOINING');
  static const ParticipantState CONNECTED = ParticipantState._(1, _omitEnumNames ? '' : 'CONNECTED');
  static const ParticipantState MUTED = ParticipantState._(2, _omitEnumNames ? '' : 'MUTED');
  static const ParticipantState DEAFENED = ParticipantState._(3, _omitEnumNames ? '' : 'DEAFENED');
  static const ParticipantState DISCONNECTED = ParticipantState._(4, _omitEnumNames ? '' : 'DISCONNECTED');

  static const $core.List<ParticipantState> values = <ParticipantState> [
    JOINING,
    CONNECTED,
    MUTED,
    DEAFENED,
    DISCONNECTED,
  ];

  static final $core.Map<$core.int, ParticipantState> _byValue = $pb.ProtobufEnum.initByValue(values);
  static ParticipantState? valueOf($core.int value) => _byValue[value];

  const ParticipantState._($core.int v, $core.String n) : super(v, n);
}


const _omitEnumNames = $core.bool.fromEnvironment('protobuf.omit_enum_names');
