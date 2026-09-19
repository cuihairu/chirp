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

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'common.pbenum.dart' as $0;
import 'server_gateway.pbenum.dart';

export 'server_gateway.pbenum.dart';

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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerAuthRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerAuthResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerHeartbeatPing', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ServerHeartbeatPong', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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
    return $result;
  }
  MessageInjectRequest._() : super();
  factory MessageInjectRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageInjectRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageInjectRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'injectId')
    ..e<SenderKind>(2, _omitFieldNames ? '' : 'senderKind', $pb.PbFieldType.OE, defaultOrMaker: SenderKind.SENDER_UNKNOWN, valueOf: SenderKind.valueOf, enumValues: SenderKind.values)
    ..aOS(3, _omitFieldNames ? '' : 'senderId')
    ..a<$core.int>(4, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.O3)
    ..aOS(5, _omitFieldNames ? '' : 'channelId')
    ..aOS(6, _omitFieldNames ? '' : 'receiverId')
    ..a<$core.List<$core.int>>(7, _omitFieldNames ? '' : 'content', $pb.PbFieldType.OY)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageInjectResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InjectMessageNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventPublishRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventPublishResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventDeliverNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventAckRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EventAckResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.server_gateway'), createEmptyInstance: create)
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


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
