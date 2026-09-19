//
//  Generated code. Do not modify.
//  source: proto/chat.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

/// 聊天消息类型
class MsgType extends $pb.ProtobufEnum {
  static const MsgType TEXT = MsgType._(0, _omitEnumNames ? '' : 'TEXT');
  static const MsgType EMOJI = MsgType._(1, _omitEnumNames ? '' : 'EMOJI');
  static const MsgType VOICE = MsgType._(2, _omitEnumNames ? '' : 'VOICE');
  static const MsgType IMAGE = MsgType._(3, _omitEnumNames ? '' : 'IMAGE');
  static const MsgType SYSTEM = MsgType._(99, _omitEnumNames ? '' : 'SYSTEM');

  static const $core.List<MsgType> values = <MsgType> [
    TEXT,
    EMOJI,
    VOICE,
    IMAGE,
    SYSTEM,
  ];

  static final $core.Map<$core.int, MsgType> _byValue = $pb.ProtobufEnum.initByValue(values);
  static MsgType? valueOf($core.int value) => _byValue[value];

  const MsgType._($core.int v, $core.String n) : super(v, n);
}

/// 聊天频道类型
class ChannelType extends $pb.ProtobufEnum {
  static const ChannelType PRIVATE = ChannelType._(0, _omitEnumNames ? '' : 'PRIVATE');
  static const ChannelType TEAM = ChannelType._(1, _omitEnumNames ? '' : 'TEAM');
  static const ChannelType GUILD = ChannelType._(2, _omitEnumNames ? '' : 'GUILD');
  static const ChannelType WORLD = ChannelType._(3, _omitEnumNames ? '' : 'WORLD');

  static const $core.List<ChannelType> values = <ChannelType> [
    PRIVATE,
    TEAM,
    GUILD,
    WORLD,
  ];

  static final $core.Map<$core.int, ChannelType> _byValue = $pb.ProtobufEnum.initByValue(values);
  static ChannelType? valueOf($core.int value) => _byValue[value];

  const ChannelType._($core.int v, $core.String n) : super(v, n);
}

/// Group member role
class GroupMemberRole extends $pb.ProtobufEnum {
  static const GroupMemberRole MEMBER = GroupMemberRole._(0, _omitEnumNames ? '' : 'MEMBER');
  static const GroupMemberRole MODERATOR = GroupMemberRole._(1, _omitEnumNames ? '' : 'MODERATOR');
  static const GroupMemberRole ADMIN = GroupMemberRole._(2, _omitEnumNames ? '' : 'ADMIN');
  static const GroupMemberRole OWNER = GroupMemberRole._(3, _omitEnumNames ? '' : 'OWNER');

  static const $core.List<GroupMemberRole> values = <GroupMemberRole> [
    MEMBER,
    MODERATOR,
    ADMIN,
    OWNER,
  ];

  static final $core.Map<$core.int, GroupMemberRole> _byValue = $pb.ProtobufEnum.initByValue(values);
  static GroupMemberRole? valueOf($core.int value) => _byValue[value];

  const GroupMemberRole._($core.int v, $core.String n) : super(v, n);
}

/// Channel type (for hierarchical channels within groups/servers)
class ChannelKind extends $pb.ProtobufEnum {
  static const ChannelKind CHANNEL_KIND_TEXT = ChannelKind._(0, _omitEnumNames ? '' : 'CHANNEL_KIND_TEXT');
  static const ChannelKind CHANNEL_KIND_VOICE = ChannelKind._(1, _omitEnumNames ? '' : 'CHANNEL_KIND_VOICE');
  static const ChannelKind CHANNEL_KIND_ANNOUNCEMENT = ChannelKind._(2, _omitEnumNames ? '' : 'CHANNEL_KIND_ANNOUNCEMENT');
  static const ChannelKind CHANNEL_KIND_STAGE = ChannelKind._(3, _omitEnumNames ? '' : 'CHANNEL_KIND_STAGE');
  static const ChannelKind CHANNEL_KIND_FORUM = ChannelKind._(4, _omitEnumNames ? '' : 'CHANNEL_KIND_FORUM');

  static const $core.List<ChannelKind> values = <ChannelKind> [
    CHANNEL_KIND_TEXT,
    CHANNEL_KIND_VOICE,
    CHANNEL_KIND_ANNOUNCEMENT,
    CHANNEL_KIND_STAGE,
    CHANNEL_KIND_FORUM,
  ];

  static final $core.Map<$core.int, ChannelKind> _byValue = $pb.ProtobufEnum.initByValue(values);
  static ChannelKind? valueOf($core.int value) => _byValue[value];

  const ChannelKind._($core.int v, $core.String n) : super(v, n);
}

/// Channel permission override
class PermissionType extends $pb.ProtobufEnum {
  static const PermissionType PERMISSION_TYPE_ROLE = PermissionType._(0, _omitEnumNames ? '' : 'PERMISSION_TYPE_ROLE');
  static const PermissionType PERMISSION_TYPE_USER = PermissionType._(1, _omitEnumNames ? '' : 'PERMISSION_TYPE_USER');

  static const $core.List<PermissionType> values = <PermissionType> [
    PERMISSION_TYPE_ROLE,
    PERMISSION_TYPE_USER,
  ];

  static final $core.Map<$core.int, PermissionType> _byValue = $pb.ProtobufEnum.initByValue(values);
  static PermissionType? valueOf($core.int value) => _byValue[value];

  const PermissionType._($core.int v, $core.String n) : super(v, n);
}

class PermissionOverride extends $pb.ProtobufEnum {
  static const PermissionOverride INHERIT = PermissionOverride._(0, _omitEnumNames ? '' : 'INHERIT');
  static const PermissionOverride ALLOW = PermissionOverride._(1, _omitEnumNames ? '' : 'ALLOW');
  static const PermissionOverride DENY = PermissionOverride._(2, _omitEnumNames ? '' : 'DENY');

  static const $core.List<PermissionOverride> values = <PermissionOverride> [
    INHERIT,
    ALLOW,
    DENY,
  ];

  static final $core.Map<$core.int, PermissionOverride> _byValue = $pb.ProtobufEnum.initByValue(values);
  static PermissionOverride? valueOf($core.int value) => _byValue[value];

  const PermissionOverride._($core.int v, $core.String n) : super(v, n);
}

/// Mention type
class MentionType extends $pb.ProtobufEnum {
  static const MentionType MENTION_TYPE_USER = MentionType._(0, _omitEnumNames ? '' : 'MENTION_TYPE_USER');
  static const MentionType MENTION_TYPE_ROLE = MentionType._(1, _omitEnumNames ? '' : 'MENTION_TYPE_ROLE');
  static const MentionType MENTION_TYPE_CHANNEL = MentionType._(2, _omitEnumNames ? '' : 'MENTION_TYPE_CHANNEL');
  static const MentionType MENTION_TYPE_EVERYONE = MentionType._(3, _omitEnumNames ? '' : 'MENTION_TYPE_EVERYONE');
  static const MentionType MENTION_TYPE_HERE = MentionType._(4, _omitEnumNames ? '' : 'MENTION_TYPE_HERE');

  static const $core.List<MentionType> values = <MentionType> [
    MENTION_TYPE_USER,
    MENTION_TYPE_ROLE,
    MENTION_TYPE_CHANNEL,
    MENTION_TYPE_EVERYONE,
    MENTION_TYPE_HERE,
  ];

  static final $core.Map<$core.int, MentionType> _byValue = $pb.ProtobufEnum.initByValue(values);
  static MentionType? valueOf($core.int value) => _byValue[value];

  const MentionType._($core.int v, $core.String n) : super(v, n);
}

class DeliveryStatus_Status extends $pb.ProtobufEnum {
  static const DeliveryStatus_Status PENDING = DeliveryStatus_Status._(0, _omitEnumNames ? '' : 'PENDING');
  static const DeliveryStatus_Status DELIVERED = DeliveryStatus_Status._(1, _omitEnumNames ? '' : 'DELIVERED');
  static const DeliveryStatus_Status FAILED = DeliveryStatus_Status._(2, _omitEnumNames ? '' : 'FAILED');
  static const DeliveryStatus_Status ACKNOWLEDGED = DeliveryStatus_Status._(3, _omitEnumNames ? '' : 'ACKNOWLEDGED');

  static const $core.List<DeliveryStatus_Status> values = <DeliveryStatus_Status> [
    PENDING,
    DELIVERED,
    FAILED,
    ACKNOWLEDGED,
  ];

  static final $core.Map<$core.int, DeliveryStatus_Status> _byValue = $pb.ProtobufEnum.initByValue(values);
  static DeliveryStatus_Status? valueOf($core.int value) => _byValue[value];

  const DeliveryStatus_Status._($core.int v, $core.String n) : super(v, n);
}


const _omitEnumNames = $core.bool.fromEnvironment('protobuf.omit_enum_names');
