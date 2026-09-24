//
//  Generated code. Do not modify.
//  source: proto/gateway.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'chat.pb.dart' as $1;
import 'common.pbenum.dart' as $0;
import 'gateway.pbenum.dart';

export 'gateway.pbenum.dart';

/// Universal Packet Envelope (Optional, if we want full protobuf wrap)
/// Usually we use Length-Prefixed + Raw Bytes for Body,
/// but for simplicity in some SDKs, a full envelope is used.
class Packet extends $pb.GeneratedMessage {
  factory Packet({
    MsgID? msgId,
    $fixnum.Int64? sequence,
    $core.List<$core.int>? body,
  }) {
    final $result = create();
    if (msgId != null) {
      $result.msgId = msgId;
    }
    if (sequence != null) {
      $result.sequence = sequence;
    }
    if (body != null) {
      $result.body = body;
    }
    return $result;
  }
  Packet._() : super();
  factory Packet.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory Packet.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'Packet', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.gateway'), createEmptyInstance: create)
    ..e<MsgID>(1, _omitFieldNames ? '' : 'msgId', $pb.PbFieldType.OE, defaultOrMaker: MsgID.UNKNOWN, valueOf: MsgID.valueOf, enumValues: MsgID.values)
    ..aInt64(2, _omitFieldNames ? '' : 'sequence')
    ..a<$core.List<$core.int>>(3, _omitFieldNames ? '' : 'body', $pb.PbFieldType.OY)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  Packet clone() => Packet()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  Packet copyWith(void Function(Packet) updates) => super.copyWith((message) => updates(message as Packet)) as Packet;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Packet create() => Packet._();
  Packet createEmptyInstance() => create();
  static $pb.PbList<Packet> createRepeated() => $pb.PbList<Packet>();
  @$core.pragma('dart2js:noInline')
  static Packet getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Packet>(create);
  static Packet? _defaultInstance;

  @$pb.TagNumber(1)
  MsgID get msgId => $_getN(0);
  @$pb.TagNumber(1)
  set msgId(MsgID v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasMsgId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMsgId() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get sequence => $_getI64(1);
  @$pb.TagNumber(2)
  set sequence($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSequence() => $_has(1);
  @$pb.TagNumber(2)
  void clearSequence() => clearField(2);

  @$pb.TagNumber(3)
  $core.List<$core.int> get body => $_getN(2);
  @$pb.TagNumber(3)
  set body($core.List<$core.int> v) { $_setBytes(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasBody() => $_has(2);
  @$pb.TagNumber(3)
  void clearBody() => clearField(3);
}

class HeartbeatPing extends $pb.GeneratedMessage {
  factory HeartbeatPing({
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  HeartbeatPing._() : super();
  factory HeartbeatPing.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory HeartbeatPing.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'HeartbeatPing', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.gateway'), createEmptyInstance: create)
    ..aInt64(1, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  HeartbeatPing clone() => HeartbeatPing()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  HeartbeatPing copyWith(void Function(HeartbeatPing) updates) => super.copyWith((message) => updates(message as HeartbeatPing)) as HeartbeatPing;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static HeartbeatPing create() => HeartbeatPing._();
  HeartbeatPing createEmptyInstance() => create();
  static $pb.PbList<HeartbeatPing> createRepeated() => $pb.PbList<HeartbeatPing>();
  @$core.pragma('dart2js:noInline')
  static HeartbeatPing getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<HeartbeatPing>(create);
  static HeartbeatPing? _defaultInstance;

  @$pb.TagNumber(1)
  $fixnum.Int64 get timestamp => $_getI64(0);
  @$pb.TagNumber(1)
  set timestamp($fixnum.Int64 v) { $_setInt64(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasTimestamp() => $_has(0);
  @$pb.TagNumber(1)
  void clearTimestamp() => clearField(1);
}

class HeartbeatPong extends $pb.GeneratedMessage {
  factory HeartbeatPong({
    $fixnum.Int64? timestamp,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  HeartbeatPong._() : super();
  factory HeartbeatPong.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory HeartbeatPong.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'HeartbeatPong', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.gateway'), createEmptyInstance: create)
    ..aInt64(1, _omitFieldNames ? '' : 'timestamp')
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  HeartbeatPong clone() => HeartbeatPong()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  HeartbeatPong copyWith(void Function(HeartbeatPong) updates) => super.copyWith((message) => updates(message as HeartbeatPong)) as HeartbeatPong;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static HeartbeatPong create() => HeartbeatPong._();
  HeartbeatPong createEmptyInstance() => create();
  static $pb.PbList<HeartbeatPong> createRepeated() => $pb.PbList<HeartbeatPong>();
  @$core.pragma('dart2js:noInline')
  static HeartbeatPong getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<HeartbeatPong>(create);
  static HeartbeatPong? _defaultInstance;

  @$pb.TagNumber(1)
  $fixnum.Int64 get timestamp => $_getI64(0);
  @$pb.TagNumber(1)
  set timestamp($fixnum.Int64 v) { $_setInt64(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasTimestamp() => $_has(0);
  @$pb.TagNumber(1)
  void clearTimestamp() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get serverTime => $_getI64(1);
  @$pb.TagNumber(2)
  set serverTime($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasServerTime() => $_has(1);
  @$pb.TagNumber(2)
  void clearServerTime() => clearField(2);
}

class PeerRegisterReq extends $pb.GeneratedMessage {
  factory PeerRegisterReq({
    $core.String? serviceId,
    $core.String? serviceSecret,
    $core.int? protocolVersion,
    $core.String? gameId,
    $core.Iterable<PeerCapability>? supportedFeatures,
  }) {
    final $result = create();
    if (serviceId != null) {
      $result.serviceId = serviceId;
    }
    if (serviceSecret != null) {
      $result.serviceSecret = serviceSecret;
    }
    if (protocolVersion != null) {
      $result.protocolVersion = protocolVersion;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (supportedFeatures != null) {
      $result.supportedFeatures.addAll(supportedFeatures);
    }
    return $result;
  }
  PeerRegisterReq._() : super();
  factory PeerRegisterReq.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PeerRegisterReq.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PeerRegisterReq', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'serviceId')
    ..aOS(2, _omitFieldNames ? '' : 'serviceSecret')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'protocolVersion', $pb.PbFieldType.O3)
    ..aOS(4, _omitFieldNames ? '' : 'gameId')
    ..pc<PeerCapability>(5, _omitFieldNames ? '' : 'supportedFeatures', $pb.PbFieldType.KE, valueOf: PeerCapability.valueOf, enumValues: PeerCapability.values, defaultEnumValue: PeerCapability.RELAY_READ_RECEIPTS)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PeerRegisterReq clone() => PeerRegisterReq()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PeerRegisterReq copyWith(void Function(PeerRegisterReq) updates) => super.copyWith((message) => updates(message as PeerRegisterReq)) as PeerRegisterReq;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PeerRegisterReq create() => PeerRegisterReq._();
  PeerRegisterReq createEmptyInstance() => create();
  static $pb.PbList<PeerRegisterReq> createRepeated() => $pb.PbList<PeerRegisterReq>();
  @$core.pragma('dart2js:noInline')
  static PeerRegisterReq getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PeerRegisterReq>(create);
  static PeerRegisterReq? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get serviceId => $_getSZ(0);
  @$pb.TagNumber(1)
  set serviceId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasServiceId() => $_has(0);
  @$pb.TagNumber(1)
  void clearServiceId() => clearField(1);

  /// same id displaces the first connection
  @$pb.TagNumber(2)
  $core.String get serviceSecret => $_getSZ(1);
  @$pb.TagNumber(2)
  set serviceSecret($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasServiceSecret() => $_has(1);
  @$pb.TagNumber(2)
  void clearServiceSecret() => clearField(2);

  /// the hub's --allowed_peers list)
  @$pb.TagNumber(3)
  $core.int get protocolVersion => $_getIZ(2);
  @$pb.TagNumber(3)
  set protocolVersion($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasProtocolVersion() => $_has(2);
  @$pb.TagNumber(3)
  void clearProtocolVersion() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get gameId => $_getSZ(3);
  @$pb.TagNumber(4)
  set gameId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasGameId() => $_has(3);
  @$pb.TagNumber(4)
  void clearGameId() => clearField(4);

  /// contain ':' (it prefixes "<game_id>:" on the
  /// hub side)
  @$pb.TagNumber(5)
  $core.List<PeerCapability> get supportedFeatures => $_getList(4);
}

class PeerRegisterResp extends $pb.GeneratedMessage {
  factory PeerRegisterResp({
    $0.ErrorCode? code,
    $core.int? protocolVersion,
    $core.int? minVersion,
    $core.int? heartbeatIntervalSeconds,
    $core.Iterable<PeerCapability>? supportedFeatures,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (protocolVersion != null) {
      $result.protocolVersion = protocolVersion;
    }
    if (minVersion != null) {
      $result.minVersion = minVersion;
    }
    if (heartbeatIntervalSeconds != null) {
      $result.heartbeatIntervalSeconds = heartbeatIntervalSeconds;
    }
    if (supportedFeatures != null) {
      $result.supportedFeatures.addAll(supportedFeatures);
    }
    return $result;
  }
  PeerRegisterResp._() : super();
  factory PeerRegisterResp.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PeerRegisterResp.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PeerRegisterResp', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..a<$core.int>(2, _omitFieldNames ? '' : 'protocolVersion', $pb.PbFieldType.O3)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'minVersion', $pb.PbFieldType.O3)
    ..a<$core.int>(4, _omitFieldNames ? '' : 'heartbeatIntervalSeconds', $pb.PbFieldType.O3)
    ..pc<PeerCapability>(5, _omitFieldNames ? '' : 'supportedFeatures', $pb.PbFieldType.KE, valueOf: PeerCapability.valueOf, enumValues: PeerCapability.values, defaultEnumValue: PeerCapability.RELAY_READ_RECEIPTS)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PeerRegisterResp clone() => PeerRegisterResp()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PeerRegisterResp copyWith(void Function(PeerRegisterResp) updates) => super.copyWith((message) => updates(message as PeerRegisterResp)) as PeerRegisterResp;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PeerRegisterResp create() => PeerRegisterResp._();
  PeerRegisterResp createEmptyInstance() => create();
  static $pb.PbList<PeerRegisterResp> createRepeated() => $pb.PbList<PeerRegisterResp>();
  @$core.pragma('dart2js:noInline')
  static PeerRegisterResp getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PeerRegisterResp>(create);
  static PeerRegisterResp? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  /// VERSION_MISMATCH (below min version)
  @$pb.TagNumber(2)
  $core.int get protocolVersion => $_getIZ(1);
  @$pb.TagNumber(2)
  set protocolVersion($core.int v) { $_setSignedInt32(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasProtocolVersion() => $_has(1);
  @$pb.TagNumber(2)
  void clearProtocolVersion() => clearField(2);

  /// current version (mismatch)
  @$pb.TagNumber(3)
  $core.int get minVersion => $_getIZ(2);
  @$pb.TagNumber(3)
  set minVersion($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMinVersion() => $_has(2);
  @$pb.TagNumber(3)
  void clearMinVersion() => clearField(3);

  @$pb.TagNumber(4)
  $core.int get heartbeatIntervalSeconds => $_getIZ(3);
  @$pb.TagNumber(4)
  set heartbeatIntervalSeconds($core.int v) { $_setSignedInt32(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasHeartbeatIntervalSeconds() => $_has(3);
  @$pb.TagNumber(4)
  void clearHeartbeatIntervalSeconds() => clearField(4);

  /// stays silent for ~2x is dropped
  @$pb.TagNumber(5)
  $core.List<PeerCapability> get supportedFeatures => $_getList(4);
}

/// Spoke -> hub: one game-side channel message, fanned out by the hub to every
/// subscribed App player as an injected private copy named
/// "<game_id>:<channel_id>". The message body is the game-side ChatMessage
/// verbatim; channel_id stays bare (the hub adds the prefix).
class ChannelMessageNotify extends $pb.GeneratedMessage {
  factory ChannelMessageNotify({
    $core.String? gameId,
    $core.String? channelId,
    $1.ChatMessage? message,
  }) {
    final $result = create();
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (message != null) {
      $result.message = message;
    }
    return $result;
  }
  ChannelMessageNotify._() : super();
  factory ChannelMessageNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelMessageNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelMessageNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'gameId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..aOM<$1.ChatMessage>(3, _omitFieldNames ? '' : 'message', subBuilder: $1.ChatMessage.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelMessageNotify clone() => ChannelMessageNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelMessageNotify copyWith(void Function(ChannelMessageNotify) updates) => super.copyWith((message) => updates(message as ChannelMessageNotify)) as ChannelMessageNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelMessageNotify create() => ChannelMessageNotify._();
  ChannelMessageNotify createEmptyInstance() => create();
  static $pb.PbList<ChannelMessageNotify> createRepeated() => $pb.PbList<ChannelMessageNotify>();
  @$core.pragma('dart2js:noInline')
  static ChannelMessageNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelMessageNotify>(create);
  static ChannelMessageNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get gameId => $_getSZ(0);
  @$pb.TagNumber(1)
  set gameId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGameId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGameId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  $1.ChatMessage get message => $_getN(2);
  @$pb.TagNumber(3)
  set message($1.ChatMessage v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasMessage() => $_has(2);
  @$pb.TagNumber(3)
  void clearMessage() => clearField(3);
  @$pb.TagNumber(3)
  $1.ChatMessage ensureMessage() => $_ensure(2);
}

/// Hub -> spoke: an App player's reply to a "<game_id>:<channel_id>" channel,
/// injected into the game-side channel as a normal message. The spoke never
/// sees the App player's player_id - the hub resolves it to a game_user_id
/// before sending.
class PeerInjectMessageNotify extends $pb.GeneratedMessage {
  factory PeerInjectMessageNotify({
    $core.String? channelId,
    $core.String? senderId,
    $core.List<$core.int>? content,
    $core.String? clientMsgId,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (senderId != null) {
      $result.senderId = senderId;
    }
    if (content != null) {
      $result.content = content;
    }
    if (clientMsgId != null) {
      $result.clientMsgId = clientMsgId;
    }
    return $result;
  }
  PeerInjectMessageNotify._() : super();
  factory PeerInjectMessageNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PeerInjectMessageNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PeerInjectMessageNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..aOS(2, _omitFieldNames ? '' : 'senderId')
    ..a<$core.List<$core.int>>(3, _omitFieldNames ? '' : 'content', $pb.PbFieldType.OY)
    ..aOS(4, _omitFieldNames ? '' : 'clientMsgId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PeerInjectMessageNotify clone() => PeerInjectMessageNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PeerInjectMessageNotify copyWith(void Function(PeerInjectMessageNotify) updates) => super.copyWith((message) => updates(message as PeerInjectMessageNotify)) as PeerInjectMessageNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PeerInjectMessageNotify create() => PeerInjectMessageNotify._();
  PeerInjectMessageNotify createEmptyInstance() => create();
  static $pb.PbList<PeerInjectMessageNotify> createRepeated() => $pb.PbList<PeerInjectMessageNotify>();
  @$core.pragma('dart2js:noInline')
  static PeerInjectMessageNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PeerInjectMessageNotify>(create);
  static PeerInjectMessageNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get senderId => $_getSZ(1);
  @$pb.TagNumber(2)
  set senderId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSenderId() => $_has(1);
  @$pb.TagNumber(2)
  void clearSenderId() => clearField(2);

  @$pb.TagNumber(3)
  $core.List<$core.int> get content => $_getN(2);
  @$pb.TagNumber(3)
  set content($core.List<$core.int> v) { $_setBytes(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasContent() => $_has(2);
  @$pb.TagNumber(3)
  void clearContent() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get clientMsgId => $_getSZ(3);
  @$pb.TagNumber(4)
  set clientMsgId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasClientMsgId() => $_has(3);
  @$pb.TagNumber(4)
  void clearClientMsgId() => clearField(4);
}


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
