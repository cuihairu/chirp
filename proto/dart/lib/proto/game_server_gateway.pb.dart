//
//  Generated code. Do not modify.
//  source: proto/game_server_gateway.proto
//
// @dart = 2.12

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_final_fields
// ignore_for_file: unnecessary_import, unnecessary_this, unused_import

import 'dart:core' as $core;

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'common.pbenum.dart' as $0;
import 'game_server_gateway.pbenum.dart';

export 'game_server_gateway.pbenum.dart';

class ServerAuthRequest extends $pb.GeneratedMessage {
  factory ServerAuthRequest({
    $core.String? serviceId,
    $core.String? secret,
    $core.int? protocolVersion,
  }) {
    final $result = create();
    if (serviceId != null) {
      $result.serviceId = serviceId;
    }
    if (secret != null) {
      $result.secret = secret;
    }
    if (protocolVersion != null) {
      $result.protocolVersion = protocolVersion;
    }
    return $result;
  }
  ServerAuthRequest._() : super();
  factory ServerAuthRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ServerAuthRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerAuthRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'serviceId')
    ..aOS(2, _omitFieldNames ? '' : 'secret')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'protocolVersion', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ServerAuthRequest clone() => ServerAuthRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ServerAuthRequest copyWith(void Function(ServerAuthRequest) updates) => super.copyWith((message) => updates(message as ServerAuthRequest)) as ServerAuthRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerAuthRequest create() => ServerAuthRequest._();
  ServerAuthRequest createEmptyInstance() => create();
  static $pb.PbList<ServerAuthRequest> createRepeated() => $pb.PbList<ServerAuthRequest>();
  @$core.pragma('dart2js:noInline')
  static ServerAuthRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ServerAuthRequest>(create);
  static ServerAuthRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get serviceId => $_getSZ(0);
  @$pb.TagNumber(1)
  set serviceId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasServiceId() => $_has(0);
  @$pb.TagNumber(1)
  void clearServiceId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get secret => $_getSZ(1);
  @$pb.TagNumber(2)
  set secret($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSecret() => $_has(1);
  @$pb.TagNumber(2)
  void clearSecret() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get protocolVersion => $_getIZ(2);
  @$pb.TagNumber(3)
  set protocolVersion($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasProtocolVersion() => $_has(2);
  @$pb.TagNumber(3)
  void clearProtocolVersion() => clearField(3);
}

class ServerAuthResponse extends $pb.GeneratedMessage {
  factory ServerAuthResponse({
    $0.ErrorCode? code,
    $fixnum.Int64? serverTimeMs,
    $core.int? heartbeatIntervalSeconds,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (serverTimeMs != null) {
      $result.serverTimeMs = serverTimeMs;
    }
    if (heartbeatIntervalSeconds != null) {
      $result.heartbeatIntervalSeconds = heartbeatIntervalSeconds;
    }
    return $result;
  }
  ServerAuthResponse._() : super();
  factory ServerAuthResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ServerAuthResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerAuthResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTimeMs')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'heartbeatIntervalSeconds', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ServerAuthResponse clone() => ServerAuthResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ServerAuthResponse copyWith(void Function(ServerAuthResponse) updates) => super.copyWith((message) => updates(message as ServerAuthResponse)) as ServerAuthResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerAuthResponse create() => ServerAuthResponse._();
  ServerAuthResponse createEmptyInstance() => create();
  static $pb.PbList<ServerAuthResponse> createRepeated() => $pb.PbList<ServerAuthResponse>();
  @$core.pragma('dart2js:noInline')
  static ServerAuthResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ServerAuthResponse>(create);
  static ServerAuthResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get serverTimeMs => $_getI64(1);
  @$pb.TagNumber(2)
  set serverTimeMs($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasServerTimeMs() => $_has(1);
  @$pb.TagNumber(2)
  void clearServerTimeMs() => clearField(2);

  /// Keepalive cadence the server assigns; connections silent for more than
  /// twice this interval are closed.
  @$pb.TagNumber(3)
  $core.int get heartbeatIntervalSeconds => $_getIZ(2);
  @$pb.TagNumber(3)
  set heartbeatIntervalSeconds($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasHeartbeatIntervalSeconds() => $_has(2);
  @$pb.TagNumber(3)
  void clearHeartbeatIntervalSeconds() => clearField(3);
}

class ServerHeartbeatPing extends $pb.GeneratedMessage {
  factory ServerHeartbeatPing({
    $fixnum.Int64? clientTimeMs,
  }) {
    final $result = create();
    if (clientTimeMs != null) {
      $result.clientTimeMs = clientTimeMs;
    }
    return $result;
  }
  ServerHeartbeatPing._() : super();
  factory ServerHeartbeatPing.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ServerHeartbeatPing.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerHeartbeatPing', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aInt64(1, _omitFieldNames ? '' : 'clientTimeMs')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ServerHeartbeatPing clone() => ServerHeartbeatPing()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ServerHeartbeatPing copyWith(void Function(ServerHeartbeatPing) updates) => super.copyWith((message) => updates(message as ServerHeartbeatPing)) as ServerHeartbeatPing;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerHeartbeatPing create() => ServerHeartbeatPing._();
  ServerHeartbeatPing createEmptyInstance() => create();
  static $pb.PbList<ServerHeartbeatPing> createRepeated() => $pb.PbList<ServerHeartbeatPing>();
  @$core.pragma('dart2js:noInline')
  static ServerHeartbeatPing getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ServerHeartbeatPing>(create);
  static ServerHeartbeatPing? _defaultInstance;

  @$pb.TagNumber(1)
  $fixnum.Int64 get clientTimeMs => $_getI64(0);
  @$pb.TagNumber(1)
  set clientTimeMs($fixnum.Int64 v) { $_setInt64(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasClientTimeMs() => $_has(0);
  @$pb.TagNumber(1)
  void clearClientTimeMs() => clearField(1);
}

class ServerHeartbeatPong extends $pb.GeneratedMessage {
  factory ServerHeartbeatPong({
    $fixnum.Int64? serverTimeMs,
  }) {
    final $result = create();
    if (serverTimeMs != null) {
      $result.serverTimeMs = serverTimeMs;
    }
    return $result;
  }
  ServerHeartbeatPong._() : super();
  factory ServerHeartbeatPong.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ServerHeartbeatPong.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerHeartbeatPong', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aInt64(1, _omitFieldNames ? '' : 'serverTimeMs')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ServerHeartbeatPong clone() => ServerHeartbeatPong()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ServerHeartbeatPong copyWith(void Function(ServerHeartbeatPong) updates) => super.copyWith((message) => updates(message as ServerHeartbeatPong)) as ServerHeartbeatPong;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerHeartbeatPong create() => ServerHeartbeatPong._();
  ServerHeartbeatPong createEmptyInstance() => create();
  static $pb.PbList<ServerHeartbeatPong> createRepeated() => $pb.PbList<ServerHeartbeatPong>();
  @$core.pragma('dart2js:noInline')
  static ServerHeartbeatPong getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ServerHeartbeatPong>(create);
  static ServerHeartbeatPong? _defaultInstance;

  @$pb.TagNumber(1)
  $fixnum.Int64 get serverTimeMs => $_getI64(0);
  @$pb.TagNumber(1)
  set serverTimeMs($fixnum.Int64 v) { $_setInt64(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasServerTimeMs() => $_has(0);
  @$pb.TagNumber(1)
  void clearServerTimeMs() => clearField(1);
}

/// Uplink: a trusted service asks chirp to deliver a message whose sender is
/// not a user. Routed to the chat service, which owns storage and delivery.
class MessageInjectRequest extends $pb.GeneratedMessage {
  factory MessageInjectRequest({
    $core.String? injectId,
    SenderKind? senderKind,
    $core.String? senderId,
    $core.int? channelType,
    $core.String? channelId,
    $core.String? receiverId,
    $core.List<$core.int>? content,
    $core.String? gameId,
  }) {
    final $result = create();
    if (injectId != null) {
      $result.injectId = injectId;
    }
    if (senderKind != null) {
      $result.senderKind = senderKind;
    }
    if (senderId != null) {
      $result.senderId = senderId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (receiverId != null) {
      $result.receiverId = receiverId;
    }
    if (content != null) {
      $result.content = content;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    return $result;
  }
  MessageInjectRequest._() : super();
  factory MessageInjectRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageInjectRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageInjectRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'injectId')
    ..e<SenderKind>(2, _omitFieldNames ? '' : 'senderKind', $pb.PbFieldType.OE, defaultOrMaker: SenderKind.SENDER_UNKNOWN, valueOf: SenderKind.valueOf, enumValues: SenderKind.values)
    ..aOS(3, _omitFieldNames ? '' : 'senderId')
    ..a<$core.int>(4, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.O3)
    ..aOS(5, _omitFieldNames ? '' : 'channelId')
    ..aOS(6, _omitFieldNames ? '' : 'receiverId')
    ..a<$core.List<$core.int>>(7, _omitFieldNames ? '' : 'content', $pb.PbFieldType.OY)
    ..aOS(8, _omitFieldNames ? '' : 'gameId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageInjectRequest clone() => MessageInjectRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageInjectRequest copyWith(void Function(MessageInjectRequest) updates) => super.copyWith((message) => updates(message as MessageInjectRequest)) as MessageInjectRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageInjectRequest create() => MessageInjectRequest._();
  MessageInjectRequest createEmptyInstance() => create();
  static $pb.PbList<MessageInjectRequest> createRepeated() => $pb.PbList<MessageInjectRequest>();
  @$core.pragma('dart2js:noInline')
  static MessageInjectRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageInjectRequest>(create);
  static MessageInjectRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get injectId => $_getSZ(0);
  @$pb.TagNumber(1)
  set injectId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasInjectId() => $_has(0);
  @$pb.TagNumber(1)
  void clearInjectId() => clearField(1);

  @$pb.TagNumber(2)
  SenderKind get senderKind => $_getN(1);
  @$pb.TagNumber(2)
  set senderKind(SenderKind v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasSenderKind() => $_has(1);
  @$pb.TagNumber(2)
  void clearSenderKind() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get senderId => $_getSZ(2);
  @$pb.TagNumber(3)
  set senderId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasSenderId() => $_has(2);
  @$pb.TagNumber(3)
  void clearSenderId() => clearField(3);

  @$pb.TagNumber(4)
  $core.int get channelType => $_getIZ(3);
  @$pb.TagNumber(4)
  set channelType($core.int v) { $_setSignedInt32(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasChannelType() => $_has(3);
  @$pb.TagNumber(4)
  void clearChannelType() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get channelId => $_getSZ(4);
  @$pb.TagNumber(5)
  set channelId($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasChannelId() => $_has(4);
  @$pb.TagNumber(5)
  void clearChannelId() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get receiverId => $_getSZ(5);
  @$pb.TagNumber(6)
  set receiverId($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasReceiverId() => $_has(5);
  @$pb.TagNumber(6)
  void clearReceiverId() => clearField(6);

  @$pb.TagNumber(7)
  $core.List<$core.int> get content => $_getN(6);
  @$pb.TagNumber(7)
  set content($core.List<$core.int> v) { $_setBytes(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasContent() => $_has(6);
  @$pb.TagNumber(7)
  void clearContent() => clearField(7);

  /// WP-8 slice 3 (fan-in): set together with a non-PRIVATE channel_type to
  /// fan the message out to every subscriber of (game_id, channel_id) as one
  /// SENDER_SERVICE private copy per subscriber (receiver_id = player_id).
  /// Empty keeps the direct injection semantics. Carrying game_id with
  /// channel_type PRIVATE is rejected (INVALID_PARAM): either a channel
  /// fan-out or a 1:1 inject, never an ambiguous both.
  @$pb.TagNumber(8)
  $core.String get gameId => $_getSZ(7);
  @$pb.TagNumber(8)
  set gameId($core.String v) { $_setString(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasGameId() => $_has(7);
  @$pb.TagNumber(8)
  void clearGameId() => clearField(8);
}

class MessageInjectResponse extends $pb.GeneratedMessage {
  factory MessageInjectResponse({
    $0.ErrorCode? code,
    $core.String? injectId,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (injectId != null) {
      $result.injectId = injectId;
    }
    return $result;
  }
  MessageInjectResponse._() : super();
  factory MessageInjectResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageInjectResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageInjectResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'injectId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageInjectResponse clone() => MessageInjectResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageInjectResponse copyWith(void Function(MessageInjectResponse) updates) => super.copyWith((message) => updates(message as MessageInjectResponse)) as MessageInjectResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageInjectResponse create() => MessageInjectResponse._();
  MessageInjectResponse createEmptyInstance() => create();
  static $pb.PbList<MessageInjectResponse> createRepeated() => $pb.PbList<MessageInjectResponse>();
  @$core.pragma('dart2js:noInline')
  static MessageInjectResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageInjectResponse>(create);
  static MessageInjectResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get injectId => $_getSZ(1);
  @$pb.TagNumber(2)
  set injectId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasInjectId() => $_has(1);
  @$pb.TagNumber(2)
  void clearInjectId() => clearField(2);
}

/// server_gateway -> chat (internal peer): the forwarded injection.
class InjectMessageNotify extends $pb.GeneratedMessage {
  factory InjectMessageNotify({
    MessageInjectRequest? message,
  }) {
    final $result = create();
    if (message != null) {
      $result.message = message;
    }
    return $result;
  }
  InjectMessageNotify._() : super();
  factory InjectMessageNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory InjectMessageNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InjectMessageNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOM<MessageInjectRequest>(1, _omitFieldNames ? '' : 'message', subBuilder: MessageInjectRequest.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  InjectMessageNotify clone() => InjectMessageNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  InjectMessageNotify copyWith(void Function(InjectMessageNotify) updates) => super.copyWith((message) => updates(message as InjectMessageNotify)) as InjectMessageNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InjectMessageNotify create() => InjectMessageNotify._();
  InjectMessageNotify createEmptyInstance() => create();
  static $pb.PbList<InjectMessageNotify> createRepeated() => $pb.PbList<InjectMessageNotify>();
  @$core.pragma('dart2js:noInline')
  static InjectMessageNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<InjectMessageNotify>(create);
  static InjectMessageNotify? _defaultInstance;

  @$pb.TagNumber(1)
  MessageInjectRequest get message => $_getN(0);
  @$pb.TagNumber(1)
  set message(MessageInjectRequest v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessage() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessage() => clearField(1);
  @$pb.TagNumber(1)
  MessageInjectRequest ensureMessage() => $_ensure(0);
}

/// Uplink: publish an event that must reach the target service reliably.
/// Events are queued per target while it is offline and redelivered on
/// reconnect until acknowledged (at-least-once).
class EventPublishRequest extends $pb.GeneratedMessage {
  factory EventPublishRequest({
    $core.String? eventId,
    $core.String? targetServiceId,
    $core.String? eventType,
    $core.List<$core.int>? payload,
  }) {
    final $result = create();
    if (eventId != null) {
      $result.eventId = eventId;
    }
    if (targetServiceId != null) {
      $result.targetServiceId = targetServiceId;
    }
    if (eventType != null) {
      $result.eventType = eventType;
    }
    if (payload != null) {
      $result.payload = payload;
    }
    return $result;
  }
  EventPublishRequest._() : super();
  factory EventPublishRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory EventPublishRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventPublishRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'eventId')
    ..aOS(2, _omitFieldNames ? '' : 'targetServiceId')
    ..aOS(3, _omitFieldNames ? '' : 'eventType')
    ..a<$core.List<$core.int>>(4, _omitFieldNames ? '' : 'payload', $pb.PbFieldType.OY)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  EventPublishRequest clone() => EventPublishRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  EventPublishRequest copyWith(void Function(EventPublishRequest) updates) => super.copyWith((message) => updates(message as EventPublishRequest)) as EventPublishRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static EventPublishRequest create() => EventPublishRequest._();
  EventPublishRequest createEmptyInstance() => create();
  static $pb.PbList<EventPublishRequest> createRepeated() => $pb.PbList<EventPublishRequest>();
  @$core.pragma('dart2js:noInline')
  static EventPublishRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<EventPublishRequest>(create);
  static EventPublishRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get eventId => $_getSZ(0);
  @$pb.TagNumber(1)
  set eventId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasEventId() => $_has(0);
  @$pb.TagNumber(1)
  void clearEventId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get targetServiceId => $_getSZ(1);
  @$pb.TagNumber(2)
  set targetServiceId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTargetServiceId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTargetServiceId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get eventType => $_getSZ(2);
  @$pb.TagNumber(3)
  set eventType($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasEventType() => $_has(2);
  @$pb.TagNumber(3)
  void clearEventType() => clearField(3);

  @$pb.TagNumber(4)
  $core.List<$core.int> get payload => $_getN(3);
  @$pb.TagNumber(4)
  set payload($core.List<$core.int> v) { $_setBytes(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasPayload() => $_has(3);
  @$pb.TagNumber(4)
  void clearPayload() => clearField(4);
}

class EventPublishResponse extends $pb.GeneratedMessage {
  factory EventPublishResponse({
    $0.ErrorCode? code,
    $core.String? eventId,
    $core.bool? queued,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (eventId != null) {
      $result.eventId = eventId;
    }
    if (queued != null) {
      $result.queued = queued;
    }
    return $result;
  }
  EventPublishResponse._() : super();
  factory EventPublishResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory EventPublishResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventPublishResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'eventId')
    ..aOB(3, _omitFieldNames ? '' : 'queued')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  EventPublishResponse clone() => EventPublishResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  EventPublishResponse copyWith(void Function(EventPublishResponse) updates) => super.copyWith((message) => updates(message as EventPublishResponse)) as EventPublishResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static EventPublishResponse create() => EventPublishResponse._();
  EventPublishResponse createEmptyInstance() => create();
  static $pb.PbList<EventPublishResponse> createRepeated() => $pb.PbList<EventPublishResponse>();
  @$core.pragma('dart2js:noInline')
  static EventPublishResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<EventPublishResponse>(create);
  static EventPublishResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get eventId => $_getSZ(1);
  @$pb.TagNumber(2)
  set eventId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasEventId() => $_has(1);
  @$pb.TagNumber(2)
  void clearEventId() => clearField(2);

  /// True when the target service was offline and the event was queued for
  /// later delivery.
  @$pb.TagNumber(3)
  $core.bool get queued => $_getBF(2);
  @$pb.TagNumber(3)
  set queued($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasQueued() => $_has(2);
  @$pb.TagNumber(3)
  void clearQueued() => clearField(3);
}

/// Downlink: an event delivered (or redelivered) to its target service.
class EventDeliverNotify extends $pb.GeneratedMessage {
  factory EventDeliverNotify({
    $core.String? eventId,
    $core.String? eventType,
    $core.List<$core.int>? payload,
    $fixnum.Int64? publishedAtMs,
    $core.int? attempt,
  }) {
    final $result = create();
    if (eventId != null) {
      $result.eventId = eventId;
    }
    if (eventType != null) {
      $result.eventType = eventType;
    }
    if (payload != null) {
      $result.payload = payload;
    }
    if (publishedAtMs != null) {
      $result.publishedAtMs = publishedAtMs;
    }
    if (attempt != null) {
      $result.attempt = attempt;
    }
    return $result;
  }
  EventDeliverNotify._() : super();
  factory EventDeliverNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory EventDeliverNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventDeliverNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'eventId')
    ..aOS(2, _omitFieldNames ? '' : 'eventType')
    ..a<$core.List<$core.int>>(3, _omitFieldNames ? '' : 'payload', $pb.PbFieldType.OY)
    ..aInt64(4, _omitFieldNames ? '' : 'publishedAtMs')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'attempt', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  EventDeliverNotify clone() => EventDeliverNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  EventDeliverNotify copyWith(void Function(EventDeliverNotify) updates) => super.copyWith((message) => updates(message as EventDeliverNotify)) as EventDeliverNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static EventDeliverNotify create() => EventDeliverNotify._();
  EventDeliverNotify createEmptyInstance() => create();
  static $pb.PbList<EventDeliverNotify> createRepeated() => $pb.PbList<EventDeliverNotify>();
  @$core.pragma('dart2js:noInline')
  static EventDeliverNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<EventDeliverNotify>(create);
  static EventDeliverNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get eventId => $_getSZ(0);
  @$pb.TagNumber(1)
  set eventId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasEventId() => $_has(0);
  @$pb.TagNumber(1)
  void clearEventId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get eventType => $_getSZ(1);
  @$pb.TagNumber(2)
  set eventType($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasEventType() => $_has(1);
  @$pb.TagNumber(2)
  void clearEventType() => clearField(2);

  @$pb.TagNumber(3)
  $core.List<$core.int> get payload => $_getN(2);
  @$pb.TagNumber(3)
  set payload($core.List<$core.int> v) { $_setBytes(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasPayload() => $_has(2);
  @$pb.TagNumber(3)
  void clearPayload() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get publishedAtMs => $_getI64(3);
  @$pb.TagNumber(4)
  set publishedAtMs($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasPublishedAtMs() => $_has(3);
  @$pb.TagNumber(4)
  void clearPublishedAtMs() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get attempt => $_getIZ(4);
  @$pb.TagNumber(5)
  set attempt($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasAttempt() => $_has(4);
  @$pb.TagNumber(5)
  void clearAttempt() => clearField(5);
}

class EventAckRequest extends $pb.GeneratedMessage {
  factory EventAckRequest({
    $core.Iterable<$core.String>? eventIds,
  }) {
    final $result = create();
    if (eventIds != null) {
      $result.eventIds.addAll(eventIds);
    }
    return $result;
  }
  EventAckRequest._() : super();
  factory EventAckRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory EventAckRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventAckRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..pPS(1, _omitFieldNames ? '' : 'eventIds')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  EventAckRequest clone() => EventAckRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  EventAckRequest copyWith(void Function(EventAckRequest) updates) => super.copyWith((message) => updates(message as EventAckRequest)) as EventAckRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static EventAckRequest create() => EventAckRequest._();
  EventAckRequest createEmptyInstance() => create();
  static $pb.PbList<EventAckRequest> createRepeated() => $pb.PbList<EventAckRequest>();
  @$core.pragma('dart2js:noInline')
  static EventAckRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<EventAckRequest>(create);
  static EventAckRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.String> get eventIds => $_getList(0);
}

class EventAckResponse extends $pb.GeneratedMessage {
  factory EventAckResponse({
    $0.ErrorCode? code,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    return $result;
  }
  EventAckResponse._() : super();
  factory EventAckResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory EventAckResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventAckResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  EventAckResponse clone() => EventAckResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  EventAckResponse copyWith(void Function(EventAckResponse) updates) => super.copyWith((message) => updates(message as EventAckResponse)) as EventAckResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static EventAckResponse create() => EventAckResponse._();
  EventAckResponse createEmptyInstance() => create();
  static $pb.PbList<EventAckResponse> createRepeated() => $pb.PbList<EventAckResponse>();
  @$core.pragma('dart2js:noInline')
  static EventAckResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<EventAckResponse>(create);
  static EventAckResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);
}

/// Persistence record (Redis, not a wire message).
class StoredIdentityBinding extends $pb.GeneratedMessage {
  factory StoredIdentityBinding({
    $core.String? bindingId,
    $core.String? playerId,
    $core.String? gameId,
    $core.String? gameUserId,
    $fixnum.Int64? boundAtMs,
  }) {
    final $result = create();
    if (bindingId != null) {
      $result.bindingId = bindingId;
    }
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (gameUserId != null) {
      $result.gameUserId = gameUserId;
    }
    if (boundAtMs != null) {
      $result.boundAtMs = boundAtMs;
    }
    return $result;
  }
  StoredIdentityBinding._() : super();
  factory StoredIdentityBinding.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredIdentityBinding.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredIdentityBinding', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'bindingId')
    ..aOS(2, _omitFieldNames ? '' : 'playerId')
    ..aOS(3, _omitFieldNames ? '' : 'gameId')
    ..aOS(4, _omitFieldNames ? '' : 'gameUserId')
    ..aInt64(5, _omitFieldNames ? '' : 'boundAtMs')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredIdentityBinding clone() => StoredIdentityBinding()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredIdentityBinding copyWith(void Function(StoredIdentityBinding) updates) => super.copyWith((message) => updates(message as StoredIdentityBinding)) as StoredIdentityBinding;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredIdentityBinding create() => StoredIdentityBinding._();
  StoredIdentityBinding createEmptyInstance() => create();
  static $pb.PbList<StoredIdentityBinding> createRepeated() => $pb.PbList<StoredIdentityBinding>();
  @$core.pragma('dart2js:noInline')
  static StoredIdentityBinding getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredIdentityBinding>(create);
  static StoredIdentityBinding? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get bindingId => $_getSZ(0);
  @$pb.TagNumber(1)
  set bindingId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasBindingId() => $_has(0);
  @$pb.TagNumber(1)
  void clearBindingId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get playerId => $_getSZ(1);
  @$pb.TagNumber(2)
  set playerId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPlayerId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPlayerId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get gameId => $_getSZ(2);
  @$pb.TagNumber(3)
  set gameId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasGameId() => $_has(2);
  @$pb.TagNumber(3)
  void clearGameId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get gameUserId => $_getSZ(3);
  @$pb.TagNumber(4)
  set gameUserId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasGameUserId() => $_has(3);
  @$pb.TagNumber(4)
  void clearGameUserId() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get boundAtMs => $_getI64(4);
  @$pb.TagNumber(5)
  set boundAtMs($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasBoundAtMs() => $_has(4);
  @$pb.TagNumber(5)
  void clearBoundAtMs() => clearField(5);
}

class BindPlayerIdentityRequest extends $pb.GeneratedMessage {
  factory BindPlayerIdentityRequest({
    $core.String? bindingId,
    $core.String? playerId,
    $core.String? gameId,
    $core.String? gameUserId,
  }) {
    final $result = create();
    if (bindingId != null) {
      $result.bindingId = bindingId;
    }
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (gameUserId != null) {
      $result.gameUserId = gameUserId;
    }
    return $result;
  }
  BindPlayerIdentityRequest._() : super();
  factory BindPlayerIdentityRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BindPlayerIdentityRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BindPlayerIdentityRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'bindingId')
    ..aOS(2, _omitFieldNames ? '' : 'playerId')
    ..aOS(3, _omitFieldNames ? '' : 'gameId')
    ..aOS(4, _omitFieldNames ? '' : 'gameUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BindPlayerIdentityRequest clone() => BindPlayerIdentityRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BindPlayerIdentityRequest copyWith(void Function(BindPlayerIdentityRequest) updates) => super.copyWith((message) => updates(message as BindPlayerIdentityRequest)) as BindPlayerIdentityRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BindPlayerIdentityRequest create() => BindPlayerIdentityRequest._();
  BindPlayerIdentityRequest createEmptyInstance() => create();
  static $pb.PbList<BindPlayerIdentityRequest> createRepeated() => $pb.PbList<BindPlayerIdentityRequest>();
  @$core.pragma('dart2js:noInline')
  static BindPlayerIdentityRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BindPlayerIdentityRequest>(create);
  static BindPlayerIdentityRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get bindingId => $_getSZ(0);
  @$pb.TagNumber(1)
  set bindingId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasBindingId() => $_has(0);
  @$pb.TagNumber(1)
  void clearBindingId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get playerId => $_getSZ(1);
  @$pb.TagNumber(2)
  set playerId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPlayerId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPlayerId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get gameId => $_getSZ(2);
  @$pb.TagNumber(3)
  set gameId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasGameId() => $_has(2);
  @$pb.TagNumber(3)
  void clearGameId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get gameUserId => $_getSZ(3);
  @$pb.TagNumber(4)
  set gameUserId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasGameUserId() => $_has(3);
  @$pb.TagNumber(4)
  void clearGameUserId() => clearField(4);
}

class BindPlayerIdentityResponse extends $pb.GeneratedMessage {
  factory BindPlayerIdentityResponse({
    $0.ErrorCode? code,
    $core.String? bindingId,
    $core.bool? existed,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (bindingId != null) {
      $result.bindingId = bindingId;
    }
    if (existed != null) {
      $result.existed = existed;
    }
    return $result;
  }
  BindPlayerIdentityResponse._() : super();
  factory BindPlayerIdentityResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BindPlayerIdentityResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BindPlayerIdentityResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'bindingId')
    ..aOB(3, _omitFieldNames ? '' : 'existed')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BindPlayerIdentityResponse clone() => BindPlayerIdentityResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BindPlayerIdentityResponse copyWith(void Function(BindPlayerIdentityResponse) updates) => super.copyWith((message) => updates(message as BindPlayerIdentityResponse)) as BindPlayerIdentityResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BindPlayerIdentityResponse create() => BindPlayerIdentityResponse._();
  BindPlayerIdentityResponse createEmptyInstance() => create();
  static $pb.PbList<BindPlayerIdentityResponse> createRepeated() => $pb.PbList<BindPlayerIdentityResponse>();
  @$core.pragma('dart2js:noInline')
  static BindPlayerIdentityResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BindPlayerIdentityResponse>(create);
  static BindPlayerIdentityResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get bindingId => $_getSZ(1);
  @$pb.TagNumber(2)
  set bindingId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasBindingId() => $_has(1);
  @$pb.TagNumber(2)
  void clearBindingId() => clearField(2);

  /// True when this exact binding was already stored (idempotent no-op).
  /// False with code=OK means newly bound; re-binding a game user to a
  /// different player under a new binding_id overwrites (the backend is
  /// the authority) and also reports OK.
  @$pb.TagNumber(3)
  $core.bool get existed => $_getBF(2);
  @$pb.TagNumber(3)
  set existed($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasExisted() => $_has(2);
  @$pb.TagNumber(3)
  void clearExisted() => clearField(3);
}

class UnbindPlayerIdentityRequest extends $pb.GeneratedMessage {
  factory UnbindPlayerIdentityRequest({
    $core.String? bindingId,
    $core.String? gameId,
    $core.String? gameUserId,
  }) {
    final $result = create();
    if (bindingId != null) {
      $result.bindingId = bindingId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (gameUserId != null) {
      $result.gameUserId = gameUserId;
    }
    return $result;
  }
  UnbindPlayerIdentityRequest._() : super();
  factory UnbindPlayerIdentityRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnbindPlayerIdentityRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnbindPlayerIdentityRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'bindingId')
    ..aOS(2, _omitFieldNames ? '' : 'gameId')
    ..aOS(3, _omitFieldNames ? '' : 'gameUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnbindPlayerIdentityRequest clone() => UnbindPlayerIdentityRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnbindPlayerIdentityRequest copyWith(void Function(UnbindPlayerIdentityRequest) updates) => super.copyWith((message) => updates(message as UnbindPlayerIdentityRequest)) as UnbindPlayerIdentityRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnbindPlayerIdentityRequest create() => UnbindPlayerIdentityRequest._();
  UnbindPlayerIdentityRequest createEmptyInstance() => create();
  static $pb.PbList<UnbindPlayerIdentityRequest> createRepeated() => $pb.PbList<UnbindPlayerIdentityRequest>();
  @$core.pragma('dart2js:noInline')
  static UnbindPlayerIdentityRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnbindPlayerIdentityRequest>(create);
  static UnbindPlayerIdentityRequest? _defaultInstance;

  /// Select exactly one: binding_id, or the pair (game_id, game_user_id).
  @$pb.TagNumber(1)
  $core.String get bindingId => $_getSZ(0);
  @$pb.TagNumber(1)
  set bindingId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasBindingId() => $_has(0);
  @$pb.TagNumber(1)
  void clearBindingId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get gameId => $_getSZ(1);
  @$pb.TagNumber(2)
  set gameId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGameId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGameId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get gameUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set gameUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasGameUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearGameUserId() => clearField(3);
}

class UnbindPlayerIdentityResponse extends $pb.GeneratedMessage {
  factory UnbindPlayerIdentityResponse({
    $0.ErrorCode? code,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    return $result;
  }
  UnbindPlayerIdentityResponse._() : super();
  factory UnbindPlayerIdentityResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnbindPlayerIdentityResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnbindPlayerIdentityResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnbindPlayerIdentityResponse clone() => UnbindPlayerIdentityResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnbindPlayerIdentityResponse copyWith(void Function(UnbindPlayerIdentityResponse) updates) => super.copyWith((message) => updates(message as UnbindPlayerIdentityResponse)) as UnbindPlayerIdentityResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnbindPlayerIdentityResponse create() => UnbindPlayerIdentityResponse._();
  UnbindPlayerIdentityResponse createEmptyInstance() => create();
  static $pb.PbList<UnbindPlayerIdentityResponse> createRepeated() => $pb.PbList<UnbindPlayerIdentityResponse>();
  @$core.pragma('dart2js:noInline')
  static UnbindPlayerIdentityResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnbindPlayerIdentityResponse>(create);
  static UnbindPlayerIdentityResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);
}

class GetPlayerIdentitiesRequest extends $pb.GeneratedMessage {
  factory GetPlayerIdentitiesRequest({
    $core.String? playerId,
  }) {
    final $result = create();
    if (playerId != null) {
      $result.playerId = playerId;
    }
    return $result;
  }
  GetPlayerIdentitiesRequest._() : super();
  factory GetPlayerIdentitiesRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPlayerIdentitiesRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPlayerIdentitiesRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'playerId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPlayerIdentitiesRequest clone() => GetPlayerIdentitiesRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPlayerIdentitiesRequest copyWith(void Function(GetPlayerIdentitiesRequest) updates) => super.copyWith((message) => updates(message as GetPlayerIdentitiesRequest)) as GetPlayerIdentitiesRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPlayerIdentitiesRequest create() => GetPlayerIdentitiesRequest._();
  GetPlayerIdentitiesRequest createEmptyInstance() => create();
  static $pb.PbList<GetPlayerIdentitiesRequest> createRepeated() => $pb.PbList<GetPlayerIdentitiesRequest>();
  @$core.pragma('dart2js:noInline')
  static GetPlayerIdentitiesRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPlayerIdentitiesRequest>(create);
  static GetPlayerIdentitiesRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get playerId => $_getSZ(0);
  @$pb.TagNumber(1)
  set playerId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPlayerId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPlayerId() => clearField(1);
}

class GetPlayerIdentitiesResponse extends $pb.GeneratedMessage {
  factory GetPlayerIdentitiesResponse({
    $0.ErrorCode? code,
    $core.Iterable<StoredIdentityBinding>? bindings,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (bindings != null) {
      $result.bindings.addAll(bindings);
    }
    return $result;
  }
  GetPlayerIdentitiesResponse._() : super();
  factory GetPlayerIdentitiesResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPlayerIdentitiesResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPlayerIdentitiesResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<StoredIdentityBinding>(2, _omitFieldNames ? '' : 'bindings', $pb.PbFieldType.PM, subBuilder: StoredIdentityBinding.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPlayerIdentitiesResponse clone() => GetPlayerIdentitiesResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPlayerIdentitiesResponse copyWith(void Function(GetPlayerIdentitiesResponse) updates) => super.copyWith((message) => updates(message as GetPlayerIdentitiesResponse)) as GetPlayerIdentitiesResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPlayerIdentitiesResponse create() => GetPlayerIdentitiesResponse._();
  GetPlayerIdentitiesResponse createEmptyInstance() => create();
  static $pb.PbList<GetPlayerIdentitiesResponse> createRepeated() => $pb.PbList<GetPlayerIdentitiesResponse>();
  @$core.pragma('dart2js:noInline')
  static GetPlayerIdentitiesResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPlayerIdentitiesResponse>(create);
  static GetPlayerIdentitiesResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<StoredIdentityBinding> get bindings => $_getList(1);
}

class ResolveGameUserRequest extends $pb.GeneratedMessage {
  factory ResolveGameUserRequest({
    $core.String? gameId,
    $core.String? gameUserId,
  }) {
    final $result = create();
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (gameUserId != null) {
      $result.gameUserId = gameUserId;
    }
    return $result;
  }
  ResolveGameUserRequest._() : super();
  factory ResolveGameUserRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ResolveGameUserRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ResolveGameUserRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'gameId')
    ..aOS(2, _omitFieldNames ? '' : 'gameUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ResolveGameUserRequest clone() => ResolveGameUserRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ResolveGameUserRequest copyWith(void Function(ResolveGameUserRequest) updates) => super.copyWith((message) => updates(message as ResolveGameUserRequest)) as ResolveGameUserRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ResolveGameUserRequest create() => ResolveGameUserRequest._();
  ResolveGameUserRequest createEmptyInstance() => create();
  static $pb.PbList<ResolveGameUserRequest> createRepeated() => $pb.PbList<ResolveGameUserRequest>();
  @$core.pragma('dart2js:noInline')
  static ResolveGameUserRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ResolveGameUserRequest>(create);
  static ResolveGameUserRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get gameId => $_getSZ(0);
  @$pb.TagNumber(1)
  set gameId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGameId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGameId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get gameUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set gameUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGameUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGameUserId() => clearField(2);
}

class ResolveGameUserResponse extends $pb.GeneratedMessage {
  factory ResolveGameUserResponse({
    $0.ErrorCode? code,
    $core.String? playerId,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (playerId != null) {
      $result.playerId = playerId;
    }
    return $result;
  }
  ResolveGameUserResponse._() : super();
  factory ResolveGameUserResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ResolveGameUserResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ResolveGameUserResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'playerId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ResolveGameUserResponse clone() => ResolveGameUserResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ResolveGameUserResponse copyWith(void Function(ResolveGameUserResponse) updates) => super.copyWith((message) => updates(message as ResolveGameUserResponse)) as ResolveGameUserResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ResolveGameUserResponse create() => ResolveGameUserResponse._();
  ResolveGameUserResponse createEmptyInstance() => create();
  static $pb.PbList<ResolveGameUserResponse> createRepeated() => $pb.PbList<ResolveGameUserResponse>();
  @$core.pragma('dart2js:noInline')
  static ResolveGameUserResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ResolveGameUserResponse>(create);
  static ResolveGameUserResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  /// Empty when code=OK but the game user is unbound.
  @$pb.TagNumber(2)
  $core.String get playerId => $_getSZ(1);
  @$pb.TagNumber(2)
  set playerId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPlayerId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPlayerId() => clearField(2);
}

/// Persistence record (Redis, not a wire message).
class StoredChannelSubscription extends $pb.GeneratedMessage {
  factory StoredChannelSubscription({
    $core.String? subscriptionId,
    $core.String? playerId,
    $core.String? gameId,
    $core.String? channelId,
    $fixnum.Int64? subscribedAtMs,
  }) {
    final $result = create();
    if (subscriptionId != null) {
      $result.subscriptionId = subscriptionId;
    }
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (subscribedAtMs != null) {
      $result.subscribedAtMs = subscribedAtMs;
    }
    return $result;
  }
  StoredChannelSubscription._() : super();
  factory StoredChannelSubscription.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredChannelSubscription.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredChannelSubscription', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'subscriptionId')
    ..aOS(2, _omitFieldNames ? '' : 'playerId')
    ..aOS(3, _omitFieldNames ? '' : 'gameId')
    ..aOS(4, _omitFieldNames ? '' : 'channelId')
    ..aInt64(5, _omitFieldNames ? '' : 'subscribedAtMs')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredChannelSubscription clone() => StoredChannelSubscription()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredChannelSubscription copyWith(void Function(StoredChannelSubscription) updates) => super.copyWith((message) => updates(message as StoredChannelSubscription)) as StoredChannelSubscription;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredChannelSubscription create() => StoredChannelSubscription._();
  StoredChannelSubscription createEmptyInstance() => create();
  static $pb.PbList<StoredChannelSubscription> createRepeated() => $pb.PbList<StoredChannelSubscription>();
  @$core.pragma('dart2js:noInline')
  static StoredChannelSubscription getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredChannelSubscription>(create);
  static StoredChannelSubscription? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get subscriptionId => $_getSZ(0);
  @$pb.TagNumber(1)
  set subscriptionId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasSubscriptionId() => $_has(0);
  @$pb.TagNumber(1)
  void clearSubscriptionId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get playerId => $_getSZ(1);
  @$pb.TagNumber(2)
  set playerId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPlayerId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPlayerId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get gameId => $_getSZ(2);
  @$pb.TagNumber(3)
  set gameId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasGameId() => $_has(2);
  @$pb.TagNumber(3)
  void clearGameId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get channelId => $_getSZ(3);
  @$pb.TagNumber(4)
  set channelId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasChannelId() => $_has(3);
  @$pb.TagNumber(4)
  void clearChannelId() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get subscribedAtMs => $_getI64(4);
  @$pb.TagNumber(5)
  set subscribedAtMs($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasSubscribedAtMs() => $_has(4);
  @$pb.TagNumber(5)
  void clearSubscribedAtMs() => clearField(5);
}

class SubscribePlayerChannelRequest extends $pb.GeneratedMessage {
  factory SubscribePlayerChannelRequest({
    $core.String? subscriptionId,
    $core.String? playerId,
    $core.String? gameId,
    $core.String? channelId,
  }) {
    final $result = create();
    if (subscriptionId != null) {
      $result.subscriptionId = subscriptionId;
    }
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    return $result;
  }
  SubscribePlayerChannelRequest._() : super();
  factory SubscribePlayerChannelRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SubscribePlayerChannelRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SubscribePlayerChannelRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'subscriptionId')
    ..aOS(2, _omitFieldNames ? '' : 'playerId')
    ..aOS(3, _omitFieldNames ? '' : 'gameId')
    ..aOS(4, _omitFieldNames ? '' : 'channelId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SubscribePlayerChannelRequest clone() => SubscribePlayerChannelRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SubscribePlayerChannelRequest copyWith(void Function(SubscribePlayerChannelRequest) updates) => super.copyWith((message) => updates(message as SubscribePlayerChannelRequest)) as SubscribePlayerChannelRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SubscribePlayerChannelRequest create() => SubscribePlayerChannelRequest._();
  SubscribePlayerChannelRequest createEmptyInstance() => create();
  static $pb.PbList<SubscribePlayerChannelRequest> createRepeated() => $pb.PbList<SubscribePlayerChannelRequest>();
  @$core.pragma('dart2js:noInline')
  static SubscribePlayerChannelRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SubscribePlayerChannelRequest>(create);
  static SubscribePlayerChannelRequest? _defaultInstance;

  /// Caller-supplied idempotency key; empty means the server mints one
  /// (the player self-service path through app_gateway always mints).
  @$pb.TagNumber(1)
  $core.String get subscriptionId => $_getSZ(0);
  @$pb.TagNumber(1)
  set subscriptionId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasSubscriptionId() => $_has(0);
  @$pb.TagNumber(1)
  void clearSubscriptionId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get playerId => $_getSZ(1);
  @$pb.TagNumber(2)
  set playerId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPlayerId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPlayerId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get gameId => $_getSZ(2);
  @$pb.TagNumber(3)
  set gameId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasGameId() => $_has(2);
  @$pb.TagNumber(3)
  void clearGameId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get channelId => $_getSZ(3);
  @$pb.TagNumber(4)
  set channelId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasChannelId() => $_has(3);
  @$pb.TagNumber(4)
  void clearChannelId() => clearField(4);
}

class SubscribePlayerChannelResponse extends $pb.GeneratedMessage {
  factory SubscribePlayerChannelResponse({
    $0.ErrorCode? code,
    $core.String? subscriptionId,
    $core.bool? existed,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (subscriptionId != null) {
      $result.subscriptionId = subscriptionId;
    }
    if (existed != null) {
      $result.existed = existed;
    }
    return $result;
  }
  SubscribePlayerChannelResponse._() : super();
  factory SubscribePlayerChannelResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SubscribePlayerChannelResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SubscribePlayerChannelResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'subscriptionId')
    ..aOB(3, _omitFieldNames ? '' : 'existed')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SubscribePlayerChannelResponse clone() => SubscribePlayerChannelResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SubscribePlayerChannelResponse copyWith(void Function(SubscribePlayerChannelResponse) updates) => super.copyWith((message) => updates(message as SubscribePlayerChannelResponse)) as SubscribePlayerChannelResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SubscribePlayerChannelResponse create() => SubscribePlayerChannelResponse._();
  SubscribePlayerChannelResponse createEmptyInstance() => create();
  static $pb.PbList<SubscribePlayerChannelResponse> createRepeated() => $pb.PbList<SubscribePlayerChannelResponse>();
  @$core.pragma('dart2js:noInline')
  static SubscribePlayerChannelResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SubscribePlayerChannelResponse>(create);
  static SubscribePlayerChannelResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  /// Echoed or minted id of the subscription now holding the tuple.
  @$pb.TagNumber(2)
  $core.String get subscriptionId => $_getSZ(1);
  @$pb.TagNumber(2)
  set subscriptionId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSubscriptionId() => $_has(1);
  @$pb.TagNumber(2)
  void clearSubscriptionId() => clearField(2);

  /// True when the tuple was already subscribed (idempotent no-op).
  @$pb.TagNumber(3)
  $core.bool get existed => $_getBF(2);
  @$pb.TagNumber(3)
  set existed($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasExisted() => $_has(2);
  @$pb.TagNumber(3)
  void clearExisted() => clearField(3);
}

class UnsubscribePlayerChannelRequest extends $pb.GeneratedMessage {
  factory UnsubscribePlayerChannelRequest({
    $core.String? subscriptionId,
    $core.String? playerId,
    $core.String? gameId,
    $core.String? channelId,
  }) {
    final $result = create();
    if (subscriptionId != null) {
      $result.subscriptionId = subscriptionId;
    }
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    return $result;
  }
  UnsubscribePlayerChannelRequest._() : super();
  factory UnsubscribePlayerChannelRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnsubscribePlayerChannelRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnsubscribePlayerChannelRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'subscriptionId')
    ..aOS(2, _omitFieldNames ? '' : 'playerId')
    ..aOS(3, _omitFieldNames ? '' : 'gameId')
    ..aOS(4, _omitFieldNames ? '' : 'channelId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnsubscribePlayerChannelRequest clone() => UnsubscribePlayerChannelRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnsubscribePlayerChannelRequest copyWith(void Function(UnsubscribePlayerChannelRequest) updates) => super.copyWith((message) => updates(message as UnsubscribePlayerChannelRequest)) as UnsubscribePlayerChannelRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnsubscribePlayerChannelRequest create() => UnsubscribePlayerChannelRequest._();
  UnsubscribePlayerChannelRequest createEmptyInstance() => create();
  static $pb.PbList<UnsubscribePlayerChannelRequest> createRepeated() => $pb.PbList<UnsubscribePlayerChannelRequest>();
  @$core.pragma('dart2js:noInline')
  static UnsubscribePlayerChannelRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnsubscribePlayerChannelRequest>(create);
  static UnsubscribePlayerChannelRequest? _defaultInstance;

  /// Select exactly one: subscription_id, or the full triple
  /// (player_id, game_id, channel_id).
  @$pb.TagNumber(1)
  $core.String get subscriptionId => $_getSZ(0);
  @$pb.TagNumber(1)
  set subscriptionId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasSubscriptionId() => $_has(0);
  @$pb.TagNumber(1)
  void clearSubscriptionId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get playerId => $_getSZ(1);
  @$pb.TagNumber(2)
  set playerId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPlayerId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPlayerId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get gameId => $_getSZ(2);
  @$pb.TagNumber(3)
  set gameId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasGameId() => $_has(2);
  @$pb.TagNumber(3)
  void clearGameId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get channelId => $_getSZ(3);
  @$pb.TagNumber(4)
  set channelId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasChannelId() => $_has(3);
  @$pb.TagNumber(4)
  void clearChannelId() => clearField(4);
}

class UnsubscribePlayerChannelResponse extends $pb.GeneratedMessage {
  factory UnsubscribePlayerChannelResponse({
    $0.ErrorCode? code,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    return $result;
  }
  UnsubscribePlayerChannelResponse._() : super();
  factory UnsubscribePlayerChannelResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnsubscribePlayerChannelResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnsubscribePlayerChannelResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnsubscribePlayerChannelResponse clone() => UnsubscribePlayerChannelResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnsubscribePlayerChannelResponse copyWith(void Function(UnsubscribePlayerChannelResponse) updates) => super.copyWith((message) => updates(message as UnsubscribePlayerChannelResponse)) as UnsubscribePlayerChannelResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnsubscribePlayerChannelResponse create() => UnsubscribePlayerChannelResponse._();
  UnsubscribePlayerChannelResponse createEmptyInstance() => create();
  static $pb.PbList<UnsubscribePlayerChannelResponse> createRepeated() => $pb.PbList<UnsubscribePlayerChannelResponse>();
  @$core.pragma('dart2js:noInline')
  static UnsubscribePlayerChannelResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnsubscribePlayerChannelResponse>(create);
  static UnsubscribePlayerChannelResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);
}

class GetPlayerSubscriptionsRequest extends $pb.GeneratedMessage {
  factory GetPlayerSubscriptionsRequest({
    $core.String? playerId,
    $core.String? gameId,
  }) {
    final $result = create();
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    return $result;
  }
  GetPlayerSubscriptionsRequest._() : super();
  factory GetPlayerSubscriptionsRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPlayerSubscriptionsRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPlayerSubscriptionsRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'playerId')
    ..aOS(2, _omitFieldNames ? '' : 'gameId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPlayerSubscriptionsRequest clone() => GetPlayerSubscriptionsRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPlayerSubscriptionsRequest copyWith(void Function(GetPlayerSubscriptionsRequest) updates) => super.copyWith((message) => updates(message as GetPlayerSubscriptionsRequest)) as GetPlayerSubscriptionsRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPlayerSubscriptionsRequest create() => GetPlayerSubscriptionsRequest._();
  GetPlayerSubscriptionsRequest createEmptyInstance() => create();
  static $pb.PbList<GetPlayerSubscriptionsRequest> createRepeated() => $pb.PbList<GetPlayerSubscriptionsRequest>();
  @$core.pragma('dart2js:noInline')
  static GetPlayerSubscriptionsRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPlayerSubscriptionsRequest>(create);
  static GetPlayerSubscriptionsRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get playerId => $_getSZ(0);
  @$pb.TagNumber(1)
  set playerId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPlayerId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPlayerId() => clearField(1);

  /// Optional filter: only subscriptions of this game when set.
  @$pb.TagNumber(2)
  $core.String get gameId => $_getSZ(1);
  @$pb.TagNumber(2)
  set gameId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGameId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGameId() => clearField(2);
}

class GetPlayerSubscriptionsResponse extends $pb.GeneratedMessage {
  factory GetPlayerSubscriptionsResponse({
    $0.ErrorCode? code,
    $core.Iterable<StoredChannelSubscription>? subscriptions,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (subscriptions != null) {
      $result.subscriptions.addAll(subscriptions);
    }
    return $result;
  }
  GetPlayerSubscriptionsResponse._() : super();
  factory GetPlayerSubscriptionsResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPlayerSubscriptionsResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPlayerSubscriptionsResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<StoredChannelSubscription>(2, _omitFieldNames ? '' : 'subscriptions', $pb.PbFieldType.PM, subBuilder: StoredChannelSubscription.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPlayerSubscriptionsResponse clone() => GetPlayerSubscriptionsResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPlayerSubscriptionsResponse copyWith(void Function(GetPlayerSubscriptionsResponse) updates) => super.copyWith((message) => updates(message as GetPlayerSubscriptionsResponse)) as GetPlayerSubscriptionsResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPlayerSubscriptionsResponse create() => GetPlayerSubscriptionsResponse._();
  GetPlayerSubscriptionsResponse createEmptyInstance() => create();
  static $pb.PbList<GetPlayerSubscriptionsResponse> createRepeated() => $pb.PbList<GetPlayerSubscriptionsResponse>();
  @$core.pragma('dart2js:noInline')
  static GetPlayerSubscriptionsResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPlayerSubscriptionsResponse>(create);
  static GetPlayerSubscriptionsResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<StoredChannelSubscription> get subscriptions => $_getList(1);
}

/// Persistence record (Redis, not a wire message).
class StoredUnreadEntry extends $pb.GeneratedMessage {
  factory StoredUnreadEntry({
    $core.String? playerId,
    $core.String? gameId,
    $core.String? channelId,
    $core.int? unreadCount,
  }) {
    final $result = create();
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (unreadCount != null) {
      $result.unreadCount = unreadCount;
    }
    return $result;
  }
  StoredUnreadEntry._() : super();
  factory StoredUnreadEntry.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredUnreadEntry.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredUnreadEntry', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'playerId')
    ..aOS(2, _omitFieldNames ? '' : 'gameId')
    ..aOS(3, _omitFieldNames ? '' : 'channelId')
    ..a<$core.int>(4, _omitFieldNames ? '' : 'unreadCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredUnreadEntry clone() => StoredUnreadEntry()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredUnreadEntry copyWith(void Function(StoredUnreadEntry) updates) => super.copyWith((message) => updates(message as StoredUnreadEntry)) as StoredUnreadEntry;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredUnreadEntry create() => StoredUnreadEntry._();
  StoredUnreadEntry createEmptyInstance() => create();
  static $pb.PbList<StoredUnreadEntry> createRepeated() => $pb.PbList<StoredUnreadEntry>();
  @$core.pragma('dart2js:noInline')
  static StoredUnreadEntry getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredUnreadEntry>(create);
  static StoredUnreadEntry? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get playerId => $_getSZ(0);
  @$pb.TagNumber(1)
  set playerId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPlayerId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPlayerId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get gameId => $_getSZ(1);
  @$pb.TagNumber(2)
  set gameId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGameId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGameId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get channelId => $_getSZ(2);
  @$pb.TagNumber(3)
  set channelId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelId() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelId() => clearField(3);

  @$pb.TagNumber(4)
  $core.int get unreadCount => $_getIZ(3);
  @$pb.TagNumber(4)
  set unreadCount($core.int v) { $_setSignedInt32(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasUnreadCount() => $_has(3);
  @$pb.TagNumber(4)
  void clearUnreadCount() => clearField(4);
}

class MarkChannelsReadRequest extends $pb.GeneratedMessage {
  factory MarkChannelsReadRequest({
    $core.String? playerId,
    $core.String? gameId,
    $core.String? channelId,
  }) {
    final $result = create();
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    return $result;
  }
  MarkChannelsReadRequest._() : super();
  factory MarkChannelsReadRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MarkChannelsReadRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MarkChannelsReadRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'playerId')
    ..aOS(2, _omitFieldNames ? '' : 'gameId')
    ..aOS(3, _omitFieldNames ? '' : 'channelId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MarkChannelsReadRequest clone() => MarkChannelsReadRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MarkChannelsReadRequest copyWith(void Function(MarkChannelsReadRequest) updates) => super.copyWith((message) => updates(message as MarkChannelsReadRequest)) as MarkChannelsReadRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MarkChannelsReadRequest create() => MarkChannelsReadRequest._();
  MarkChannelsReadRequest createEmptyInstance() => create();
  static $pb.PbList<MarkChannelsReadRequest> createRepeated() => $pb.PbList<MarkChannelsReadRequest>();
  @$core.pragma('dart2js:noInline')
  static MarkChannelsReadRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MarkChannelsReadRequest>(create);
  static MarkChannelsReadRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get playerId => $_getSZ(0);
  @$pb.TagNumber(1)
  set playerId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPlayerId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPlayerId() => clearField(1);

  /// Layered selector: channel_id set (game_id required) clears that one
  /// channel; only game_id clears every channel of that game; both empty
  /// clears everything the player has.
  @$pb.TagNumber(2)
  $core.String get gameId => $_getSZ(1);
  @$pb.TagNumber(2)
  set gameId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGameId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGameId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get channelId => $_getSZ(2);
  @$pb.TagNumber(3)
  set channelId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelId() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelId() => clearField(3);
}

class MarkChannelsReadResponse extends $pb.GeneratedMessage {
  factory MarkChannelsReadResponse({
    $0.ErrorCode? code,
    $core.int? cleared,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (cleared != null) {
      $result.cleared = cleared;
    }
    return $result;
  }
  MarkChannelsReadResponse._() : super();
  factory MarkChannelsReadResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MarkChannelsReadResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MarkChannelsReadResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..a<$core.int>(2, _omitFieldNames ? '' : 'cleared', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MarkChannelsReadResponse clone() => MarkChannelsReadResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MarkChannelsReadResponse copyWith(void Function(MarkChannelsReadResponse) updates) => super.copyWith((message) => updates(message as MarkChannelsReadResponse)) as MarkChannelsReadResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MarkChannelsReadResponse create() => MarkChannelsReadResponse._();
  MarkChannelsReadResponse createEmptyInstance() => create();
  static $pb.PbList<MarkChannelsReadResponse> createRepeated() => $pb.PbList<MarkChannelsReadResponse>();
  @$core.pragma('dart2js:noInline')
  static MarkChannelsReadResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MarkChannelsReadResponse>(create);
  static MarkChannelsReadResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  /// Number of ledger entries removed (0 for an unknown target — marking
  /// read is idempotent).
  @$pb.TagNumber(2)
  $core.int get cleared => $_getIZ(1);
  @$pb.TagNumber(2)
  set cleared($core.int v) { $_setSignedInt32(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasCleared() => $_has(1);
  @$pb.TagNumber(2)
  void clearCleared() => clearField(2);
}

class UnreadSummaryEntry extends $pb.GeneratedMessage {
  factory UnreadSummaryEntry({
    $core.String? gameId,
    $core.String? channelId,
    $core.int? unreadCount,
  }) {
    final $result = create();
    if (gameId != null) {
      $result.gameId = gameId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (unreadCount != null) {
      $result.unreadCount = unreadCount;
    }
    return $result;
  }
  UnreadSummaryEntry._() : super();
  factory UnreadSummaryEntry.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnreadSummaryEntry.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnreadSummaryEntry', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'gameId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'unreadCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnreadSummaryEntry clone() => UnreadSummaryEntry()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnreadSummaryEntry copyWith(void Function(UnreadSummaryEntry) updates) => super.copyWith((message) => updates(message as UnreadSummaryEntry)) as UnreadSummaryEntry;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnreadSummaryEntry create() => UnreadSummaryEntry._();
  UnreadSummaryEntry createEmptyInstance() => create();
  static $pb.PbList<UnreadSummaryEntry> createRepeated() => $pb.PbList<UnreadSummaryEntry>();
  @$core.pragma('dart2js:noInline')
  static UnreadSummaryEntry getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnreadSummaryEntry>(create);
  static UnreadSummaryEntry? _defaultInstance;

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
  $core.int get unreadCount => $_getIZ(2);
  @$pb.TagNumber(3)
  set unreadCount($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasUnreadCount() => $_has(2);
  @$pb.TagNumber(3)
  void clearUnreadCount() => clearField(3);
}

class GetUnreadSummaryRequest extends $pb.GeneratedMessage {
  factory GetUnreadSummaryRequest({
    $core.String? playerId,
    $core.String? gameId,
  }) {
    final $result = create();
    if (playerId != null) {
      $result.playerId = playerId;
    }
    if (gameId != null) {
      $result.gameId = gameId;
    }
    return $result;
  }
  GetUnreadSummaryRequest._() : super();
  factory GetUnreadSummaryRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUnreadSummaryRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUnreadSummaryRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'playerId')
    ..aOS(2, _omitFieldNames ? '' : 'gameId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUnreadSummaryRequest clone() => GetUnreadSummaryRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUnreadSummaryRequest copyWith(void Function(GetUnreadSummaryRequest) updates) => super.copyWith((message) => updates(message as GetUnreadSummaryRequest)) as GetUnreadSummaryRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUnreadSummaryRequest create() => GetUnreadSummaryRequest._();
  GetUnreadSummaryRequest createEmptyInstance() => create();
  static $pb.PbList<GetUnreadSummaryRequest> createRepeated() => $pb.PbList<GetUnreadSummaryRequest>();
  @$core.pragma('dart2js:noInline')
  static GetUnreadSummaryRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUnreadSummaryRequest>(create);
  static GetUnreadSummaryRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get playerId => $_getSZ(0);
  @$pb.TagNumber(1)
  set playerId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPlayerId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPlayerId() => clearField(1);

  /// Optional filter: only entries of this game when set.
  @$pb.TagNumber(2)
  $core.String get gameId => $_getSZ(1);
  @$pb.TagNumber(2)
  set gameId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGameId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGameId() => clearField(2);
}

class GetUnreadSummaryResponse extends $pb.GeneratedMessage {
  factory GetUnreadSummaryResponse({
    $0.ErrorCode? code,
    $core.Iterable<UnreadSummaryEntry>? entries,
    $core.int? totalUnread,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (entries != null) {
      $result.entries.addAll(entries);
    }
    if (totalUnread != null) {
      $result.totalUnread = totalUnread;
    }
    return $result;
  }
  GetUnreadSummaryResponse._() : super();
  factory GetUnreadSummaryResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUnreadSummaryResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUnreadSummaryResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.game_server_gateway'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<UnreadSummaryEntry>(2, _omitFieldNames ? '' : 'entries', $pb.PbFieldType.PM, subBuilder: UnreadSummaryEntry.create)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'totalUnread', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUnreadSummaryResponse clone() => GetUnreadSummaryResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUnreadSummaryResponse copyWith(void Function(GetUnreadSummaryResponse) updates) => super.copyWith((message) => updates(message as GetUnreadSummaryResponse)) as GetUnreadSummaryResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUnreadSummaryResponse create() => GetUnreadSummaryResponse._();
  GetUnreadSummaryResponse createEmptyInstance() => create();
  static $pb.PbList<GetUnreadSummaryResponse> createRepeated() => $pb.PbList<GetUnreadSummaryResponse>();
  @$core.pragma('dart2js:noInline')
  static GetUnreadSummaryResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUnreadSummaryResponse>(create);
  static GetUnreadSummaryResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  /// One entry per (game, channel) with a nonzero counter, ordered by
  /// (game_id, channel_id).
  @$pb.TagNumber(2)
  $core.List<UnreadSummaryEntry> get entries => $_getList(1);

  /// Sum of the returned entries' counters (after the filter).
  @$pb.TagNumber(3)
  $core.int get totalUnread => $_getIZ(2);
  @$pb.TagNumber(3)
  set totalUnread($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTotalUnread() => $_has(2);
  @$pb.TagNumber(3)
  void clearTotalUnread() => clearField(3);
}


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
