//
//  Generated code. Do not modify.
//  source: proto/app_notification.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

/// Notification priority
class NotificationPriority extends $pb.ProtobufEnum {
  static const NotificationPriority LOW = NotificationPriority._(0, _omitEnumNames ? '' : 'LOW');
  static const NotificationPriority NORMAL = NotificationPriority._(1, _omitEnumNames ? '' : 'NORMAL');
  static const NotificationPriority HIGH = NotificationPriority._(2, _omitEnumNames ? '' : 'HIGH');
  static const NotificationPriority URGENT = NotificationPriority._(3, _omitEnumNames ? '' : 'URGENT');

  static const $core.List<NotificationPriority> values = <NotificationPriority> [
    LOW,
    NORMAL,
    HIGH,
    URGENT,
  ];

  static final $core.Map<$core.int, NotificationPriority> _byValue = $pb.ProtobufEnum.initByValue(values);
  static NotificationPriority? valueOf($core.int value) => _byValue[value];

  const NotificationPriority._($core.int v, $core.String n) : super(v, n);
}

/// Notification type
class NotificationType extends $pb.ProtobufEnum {
  static const NotificationType MESSAGE = NotificationType._(0, _omitEnumNames ? '' : 'MESSAGE');
  static const NotificationType MENTION = NotificationType._(1, _omitEnumNames ? '' : 'MENTION');
  static const NotificationType REACTION = NotificationType._(2, _omitEnumNames ? '' : 'REACTION');
  static const NotificationType FRIEND_REQUEST = NotificationType._(3, _omitEnumNames ? '' : 'FRIEND_REQUEST');
  static const NotificationType FRIEND_ACCEPTED = NotificationType._(4, _omitEnumNames ? '' : 'FRIEND_ACCEPTED');
  static const NotificationType VOICE_CALL = NotificationType._(5, _omitEnumNames ? '' : 'VOICE_CALL');
  static const NotificationType VOICE_INVITE = NotificationType._(6, _omitEnumNames ? '' : 'VOICE_INVITE');
  static const NotificationType SYSTEM = NotificationType._(99, _omitEnumNames ? '' : 'SYSTEM');

  static const $core.List<NotificationType> values = <NotificationType> [
    MESSAGE,
    MENTION,
    REACTION,
    FRIEND_REQUEST,
    FRIEND_ACCEPTED,
    VOICE_CALL,
    VOICE_INVITE,
    SYSTEM,
  ];

  static final $core.Map<$core.int, NotificationType> _byValue = $pb.ProtobufEnum.initByValue(values);
  static NotificationType? valueOf($core.int value) => _byValue[value];

  const NotificationType._($core.int v, $core.String n) : super(v, n);
}


const _omitEnumNames = $core.bool.fromEnvironment('protobuf.omit_enum_names');
