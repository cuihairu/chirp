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

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'common.pbenum.dart' as $0;
import 'voice.pbenum.dart';

export 'voice.pbenum.dart';

/// Create voice room
class CreateRoomRequest extends $pb.GeneratedMessage {
  factory CreateRoomRequest({
    $core.String? userId,
    RoomType? roomType,
    $core.String? roomName,
    $core.int? maxParticipants,
    $core.Map<$core.String, $core.String>? metadata,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (roomType != null) {
      $result.roomType = roomType;
    }
    if (roomName != null) {
      $result.roomName = roomName;
    }
    if (maxParticipants != null) {
      $result.maxParticipants = maxParticipants;
    }
    if (metadata != null) {
      $result.metadata.addAll(metadata);
    }
    return $result;
  }
  CreateRoomRequest._() : super();
  factory CreateRoomRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateRoomRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateRoomRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..e<RoomType>(2, _omitFieldNames ? '' : 'roomType', $pb.PbFieldType.OE, defaultOrMaker: RoomType.PEER_TO_PEER, valueOf: RoomType.valueOf, enumValues: RoomType.values)
    ..aOS(3, _omitFieldNames ? '' : 'roomName')
    ..a<$core.int>(4, _omitFieldNames ? '' : 'maxParticipants', $pb.PbFieldType.O3)
    ..m<$core.String, $core.String>(5, _omitFieldNames ? '' : 'metadata', entryClassName: 'CreateRoomRequest.MetadataEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.voice'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateRoomRequest clone() => CreateRoomRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateRoomRequest copyWith(void Function(CreateRoomRequest) updates) => super.copyWith((message) => updates(message as CreateRoomRequest)) as CreateRoomRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateRoomRequest create() => CreateRoomRequest._();
  CreateRoomRequest createEmptyInstance() => create();
  static $pb.PbList<CreateRoomRequest> createRepeated() => $pb.PbList<CreateRoomRequest>();
  @$core.pragma('dart2js:noInline')
  static CreateRoomRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateRoomRequest>(create);
  static CreateRoomRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  RoomType get roomType => $_getN(1);
  @$pb.TagNumber(2)
  set roomType(RoomType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomType() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomType() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get roomName => $_getSZ(2);
  @$pb.TagNumber(3)
  set roomName($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasRoomName() => $_has(2);
  @$pb.TagNumber(3)
  void clearRoomName() => clearField(3);

  @$pb.TagNumber(4)
  $core.int get maxParticipants => $_getIZ(3);
  @$pb.TagNumber(4)
  set maxParticipants($core.int v) { $_setSignedInt32(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasMaxParticipants() => $_has(3);
  @$pb.TagNumber(4)
  void clearMaxParticipants() => clearField(4);

  @$pb.TagNumber(5)
  $core.Map<$core.String, $core.String> get metadata => $_getMap(4);
}

class CreateRoomResponse extends $pb.GeneratedMessage {
  factory CreateRoomResponse({
    $0.ErrorCode? code,
    $core.String? roomId,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  CreateRoomResponse._() : super();
  factory CreateRoomResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateRoomResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateRoomResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateRoomResponse clone() => CreateRoomResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateRoomResponse copyWith(void Function(CreateRoomResponse) updates) => super.copyWith((message) => updates(message as CreateRoomResponse)) as CreateRoomResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateRoomResponse create() => CreateRoomResponse._();
  CreateRoomResponse createEmptyInstance() => create();
  static $pb.PbList<CreateRoomResponse> createRepeated() => $pb.PbList<CreateRoomResponse>();
  @$core.pragma('dart2js:noInline')
  static CreateRoomResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateRoomResponse>(create);
  static CreateRoomResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// TURN/STUN access for WebRTC gathering, handed out with the join response
/// (coturn REST short-term credentials; empty username/credential = STUN only).
class IceServer extends $pb.GeneratedMessage {
  factory IceServer({
    $core.Iterable<$core.String>? urls,
    $core.String? username,
    $core.String? credential,
  }) {
    final $result = create();
    if (urls != null) {
      $result.urls.addAll(urls);
    }
    if (username != null) {
      $result.username = username;
    }
    if (credential != null) {
      $result.credential = credential;
    }
    return $result;
  }
  IceServer._() : super();
  factory IceServer.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory IceServer.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'IceServer', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..pPS(1, _omitFieldNames ? '' : 'urls')
    ..aOS(2, _omitFieldNames ? '' : 'username')
    ..aOS(3, _omitFieldNames ? '' : 'credential')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  IceServer clone() => IceServer()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  IceServer copyWith(void Function(IceServer) updates) => super.copyWith((message) => updates(message as IceServer)) as IceServer;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static IceServer create() => IceServer._();
  IceServer createEmptyInstance() => create();
  static $pb.PbList<IceServer> createRepeated() => $pb.PbList<IceServer>();
  @$core.pragma('dart2js:noInline')
  static IceServer getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<IceServer>(create);
  static IceServer? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.String> get urls => $_getList(0);

  @$pb.TagNumber(2)
  $core.String get username => $_getSZ(1);
  @$pb.TagNumber(2)
  set username($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUsername() => $_has(1);
  @$pb.TagNumber(2)
  void clearUsername() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get credential => $_getSZ(2);
  @$pb.TagNumber(3)
  set credential($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasCredential() => $_has(2);
  @$pb.TagNumber(3)
  void clearCredential() => clearField(3);
}

/// Join voice room
class JoinRoomRequest extends $pb.GeneratedMessage {
  factory JoinRoomRequest({
    $core.String? userId,
    $core.String? roomId,
    $core.String? sdpOffer,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (sdpOffer != null) {
      $result.sdpOffer = sdpOffer;
    }
    return $result;
  }
  JoinRoomRequest._() : super();
  factory JoinRoomRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory JoinRoomRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'JoinRoomRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..aOS(3, _omitFieldNames ? '' : 'sdpOffer')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  JoinRoomRequest clone() => JoinRoomRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  JoinRoomRequest copyWith(void Function(JoinRoomRequest) updates) => super.copyWith((message) => updates(message as JoinRoomRequest)) as JoinRoomRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static JoinRoomRequest create() => JoinRoomRequest._();
  JoinRoomRequest createEmptyInstance() => create();
  static $pb.PbList<JoinRoomRequest> createRepeated() => $pb.PbList<JoinRoomRequest>();
  @$core.pragma('dart2js:noInline')
  static JoinRoomRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<JoinRoomRequest>(create);
  static JoinRoomRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get sdpOffer => $_getSZ(2);
  @$pb.TagNumber(3)
  set sdpOffer($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasSdpOffer() => $_has(2);
  @$pb.TagNumber(3)
  void clearSdpOffer() => clearField(3);
}

class JoinRoomResponse extends $pb.GeneratedMessage {
  factory JoinRoomResponse({
    $0.ErrorCode? code,
    $core.String? roomId,
    $core.String? sdpAnswer,
    $core.Iterable<$core.String>? participantIds,
    $fixnum.Int64? serverTime,
    $core.Iterable<IceServer>? iceServers,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (sdpAnswer != null) {
      $result.sdpAnswer = sdpAnswer;
    }
    if (participantIds != null) {
      $result.participantIds.addAll(participantIds);
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    if (iceServers != null) {
      $result.iceServers.addAll(iceServers);
    }
    return $result;
  }
  JoinRoomResponse._() : super();
  factory JoinRoomResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory JoinRoomResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'JoinRoomResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..aOS(3, _omitFieldNames ? '' : 'sdpAnswer')
    ..pPS(4, _omitFieldNames ? '' : 'participantIds')
    ..aInt64(5, _omitFieldNames ? '' : 'serverTime')
    ..pc<IceServer>(6, _omitFieldNames ? '' : 'iceServers', $pb.PbFieldType.PM, subBuilder: IceServer.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  JoinRoomResponse clone() => JoinRoomResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  JoinRoomResponse copyWith(void Function(JoinRoomResponse) updates) => super.copyWith((message) => updates(message as JoinRoomResponse)) as JoinRoomResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static JoinRoomResponse create() => JoinRoomResponse._();
  JoinRoomResponse createEmptyInstance() => create();
  static $pb.PbList<JoinRoomResponse> createRepeated() => $pb.PbList<JoinRoomResponse>();
  @$core.pragma('dart2js:noInline')
  static JoinRoomResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<JoinRoomResponse>(create);
  static JoinRoomResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get sdpAnswer => $_getSZ(2);
  @$pb.TagNumber(3)
  set sdpAnswer($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasSdpAnswer() => $_has(2);
  @$pb.TagNumber(3)
  void clearSdpAnswer() => clearField(3);

  @$pb.TagNumber(4)
  $core.List<$core.String> get participantIds => $_getList(3);

  @$pb.TagNumber(5)
  $fixnum.Int64 get serverTime => $_getI64(4);
  @$pb.TagNumber(5)
  set serverTime($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasServerTime() => $_has(4);
  @$pb.TagNumber(5)
  void clearServerTime() => clearField(5);

  @$pb.TagNumber(6)
  $core.List<IceServer> get iceServers => $_getList(5);
}

/// Leave voice room
class LeaveRoomRequest extends $pb.GeneratedMessage {
  factory LeaveRoomRequest({
    $core.String? userId,
    $core.String? roomId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    return $result;
  }
  LeaveRoomRequest._() : super();
  factory LeaveRoomRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory LeaveRoomRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'LeaveRoomRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  LeaveRoomRequest clone() => LeaveRoomRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  LeaveRoomRequest copyWith(void Function(LeaveRoomRequest) updates) => super.copyWith((message) => updates(message as LeaveRoomRequest)) as LeaveRoomRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static LeaveRoomRequest create() => LeaveRoomRequest._();
  LeaveRoomRequest createEmptyInstance() => create();
  static $pb.PbList<LeaveRoomRequest> createRepeated() => $pb.PbList<LeaveRoomRequest>();
  @$core.pragma('dart2js:noInline')
  static LeaveRoomRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<LeaveRoomRequest>(create);
  static LeaveRoomRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);
}

class LeaveRoomResponse extends $pb.GeneratedMessage {
  factory LeaveRoomResponse({
    $0.ErrorCode? code,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  LeaveRoomResponse._() : super();
  factory LeaveRoomResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory LeaveRoomResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'LeaveRoomResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  LeaveRoomResponse clone() => LeaveRoomResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  LeaveRoomResponse copyWith(void Function(LeaveRoomResponse) updates) => super.copyWith((message) => updates(message as LeaveRoomResponse)) as LeaveRoomResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static LeaveRoomResponse create() => LeaveRoomResponse._();
  LeaveRoomResponse createEmptyInstance() => create();
  static $pb.PbList<LeaveRoomResponse> createRepeated() => $pb.PbList<LeaveRoomResponse>();
  @$core.pragma('dart2js:noInline')
  static LeaveRoomResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<LeaveRoomResponse>(create);
  static LeaveRoomResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get serverTime => $_getI64(1);
  @$pb.TagNumber(2)
  set serverTime($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasServerTime() => $_has(1);
  @$pb.TagNumber(2)
  void clearServerTime() => clearField(2);
}

/// WebRTC signaling messages
class IceCandidate extends $pb.GeneratedMessage {
  factory IceCandidate({
    $core.String? candidate,
    $core.String? sdpMid,
    $core.int? sdpMlineIndex,
  }) {
    final $result = create();
    if (candidate != null) {
      $result.candidate = candidate;
    }
    if (sdpMid != null) {
      $result.sdpMid = sdpMid;
    }
    if (sdpMlineIndex != null) {
      $result.sdpMlineIndex = sdpMlineIndex;
    }
    return $result;
  }
  IceCandidate._() : super();
  factory IceCandidate.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory IceCandidate.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'IceCandidate', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'candidate')
    ..aOS(2, _omitFieldNames ? '' : 'sdpMid')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'sdpMlineIndex', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  IceCandidate clone() => IceCandidate()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  IceCandidate copyWith(void Function(IceCandidate) updates) => super.copyWith((message) => updates(message as IceCandidate)) as IceCandidate;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static IceCandidate create() => IceCandidate._();
  IceCandidate createEmptyInstance() => create();
  static $pb.PbList<IceCandidate> createRepeated() => $pb.PbList<IceCandidate>();
  @$core.pragma('dart2js:noInline')
  static IceCandidate getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<IceCandidate>(create);
  static IceCandidate? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get candidate => $_getSZ(0);
  @$pb.TagNumber(1)
  set candidate($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasCandidate() => $_has(0);
  @$pb.TagNumber(1)
  void clearCandidate() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get sdpMid => $_getSZ(1);
  @$pb.TagNumber(2)
  set sdpMid($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSdpMid() => $_has(1);
  @$pb.TagNumber(2)
  void clearSdpMid() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get sdpMlineIndex => $_getIZ(2);
  @$pb.TagNumber(3)
  set sdpMlineIndex($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasSdpMlineIndex() => $_has(2);
  @$pb.TagNumber(3)
  void clearSdpMlineIndex() => clearField(3);
}

class IceCandidateMessage extends $pb.GeneratedMessage {
  factory IceCandidateMessage({
    $core.String? roomId,
    $core.String? fromUserId,
    $core.String? toUserId,
    IceCandidate? candidate,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (fromUserId != null) {
      $result.fromUserId = fromUserId;
    }
    if (toUserId != null) {
      $result.toUserId = toUserId;
    }
    if (candidate != null) {
      $result.candidate = candidate;
    }
    return $result;
  }
  IceCandidateMessage._() : super();
  factory IceCandidateMessage.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory IceCandidateMessage.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'IceCandidateMessage', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..aOS(2, _omitFieldNames ? '' : 'fromUserId')
    ..aOS(3, _omitFieldNames ? '' : 'toUserId')
    ..aOM<IceCandidate>(4, _omitFieldNames ? '' : 'candidate', subBuilder: IceCandidate.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  IceCandidateMessage clone() => IceCandidateMessage()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  IceCandidateMessage copyWith(void Function(IceCandidateMessage) updates) => super.copyWith((message) => updates(message as IceCandidateMessage)) as IceCandidateMessage;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static IceCandidateMessage create() => IceCandidateMessage._();
  IceCandidateMessage createEmptyInstance() => create();
  static $pb.PbList<IceCandidateMessage> createRepeated() => $pb.PbList<IceCandidateMessage>();
  @$core.pragma('dart2js:noInline')
  static IceCandidateMessage getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<IceCandidateMessage>(create);
  static IceCandidateMessage? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get fromUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set fromUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFromUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearFromUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get toUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set toUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasToUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearToUserId() => clearField(3);

  @$pb.TagNumber(4)
  IceCandidate get candidate => $_getN(3);
  @$pb.TagNumber(4)
  set candidate(IceCandidate v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasCandidate() => $_has(3);
  @$pb.TagNumber(4)
  void clearCandidate() => clearField(4);
  @$pb.TagNumber(4)
  IceCandidate ensureCandidate() => $_ensure(3);
}

class SdpOfferMessage extends $pb.GeneratedMessage {
  factory SdpOfferMessage({
    $core.String? roomId,
    $core.String? fromUserId,
    $core.String? toUserId,
    $core.String? sdpOffer,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (fromUserId != null) {
      $result.fromUserId = fromUserId;
    }
    if (toUserId != null) {
      $result.toUserId = toUserId;
    }
    if (sdpOffer != null) {
      $result.sdpOffer = sdpOffer;
    }
    return $result;
  }
  SdpOfferMessage._() : super();
  factory SdpOfferMessage.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SdpOfferMessage.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SdpOfferMessage', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..aOS(2, _omitFieldNames ? '' : 'fromUserId')
    ..aOS(3, _omitFieldNames ? '' : 'toUserId')
    ..aOS(4, _omitFieldNames ? '' : 'sdpOffer')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SdpOfferMessage clone() => SdpOfferMessage()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SdpOfferMessage copyWith(void Function(SdpOfferMessage) updates) => super.copyWith((message) => updates(message as SdpOfferMessage)) as SdpOfferMessage;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SdpOfferMessage create() => SdpOfferMessage._();
  SdpOfferMessage createEmptyInstance() => create();
  static $pb.PbList<SdpOfferMessage> createRepeated() => $pb.PbList<SdpOfferMessage>();
  @$core.pragma('dart2js:noInline')
  static SdpOfferMessage getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SdpOfferMessage>(create);
  static SdpOfferMessage? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get fromUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set fromUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFromUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearFromUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get toUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set toUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasToUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearToUserId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get sdpOffer => $_getSZ(3);
  @$pb.TagNumber(4)
  set sdpOffer($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasSdpOffer() => $_has(3);
  @$pb.TagNumber(4)
  void clearSdpOffer() => clearField(4);
}

class SdpAnswerMessage extends $pb.GeneratedMessage {
  factory SdpAnswerMessage({
    $core.String? roomId,
    $core.String? fromUserId,
    $core.String? toUserId,
    $core.String? sdpAnswer,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (fromUserId != null) {
      $result.fromUserId = fromUserId;
    }
    if (toUserId != null) {
      $result.toUserId = toUserId;
    }
    if (sdpAnswer != null) {
      $result.sdpAnswer = sdpAnswer;
    }
    return $result;
  }
  SdpAnswerMessage._() : super();
  factory SdpAnswerMessage.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SdpAnswerMessage.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SdpAnswerMessage', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..aOS(2, _omitFieldNames ? '' : 'fromUserId')
    ..aOS(3, _omitFieldNames ? '' : 'toUserId')
    ..aOS(4, _omitFieldNames ? '' : 'sdpAnswer')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SdpAnswerMessage clone() => SdpAnswerMessage()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SdpAnswerMessage copyWith(void Function(SdpAnswerMessage) updates) => super.copyWith((message) => updates(message as SdpAnswerMessage)) as SdpAnswerMessage;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SdpAnswerMessage create() => SdpAnswerMessage._();
  SdpAnswerMessage createEmptyInstance() => create();
  static $pb.PbList<SdpAnswerMessage> createRepeated() => $pb.PbList<SdpAnswerMessage>();
  @$core.pragma('dart2js:noInline')
  static SdpAnswerMessage getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SdpAnswerMessage>(create);
  static SdpAnswerMessage? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get fromUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set fromUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFromUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearFromUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get toUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set toUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasToUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearToUserId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get sdpAnswer => $_getSZ(3);
  @$pb.TagNumber(4)
  set sdpAnswer($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasSdpAnswer() => $_has(3);
  @$pb.TagNumber(4)
  void clearSdpAnswer() => clearField(4);
}

/// Room participant info
class ParticipantInfo extends $pb.GeneratedMessage {
  factory ParticipantInfo({
    $core.String? userId,
    $core.String? username,
    ParticipantState? state,
    $fixnum.Int64? joinedAt,
    $core.bool? isSpeaking,
    $core.bool? muted,
    $core.bool? deafened,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (username != null) {
      $result.username = username;
    }
    if (state != null) {
      $result.state = state;
    }
    if (joinedAt != null) {
      $result.joinedAt = joinedAt;
    }
    if (isSpeaking != null) {
      $result.isSpeaking = isSpeaking;
    }
    if (muted != null) {
      $result.muted = muted;
    }
    if (deafened != null) {
      $result.deafened = deafened;
    }
    return $result;
  }
  ParticipantInfo._() : super();
  factory ParticipantInfo.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ParticipantInfo.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ParticipantInfo', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'username')
    ..e<ParticipantState>(3, _omitFieldNames ? '' : 'state', $pb.PbFieldType.OE, defaultOrMaker: ParticipantState.JOINING, valueOf: ParticipantState.valueOf, enumValues: ParticipantState.values)
    ..aInt64(4, _omitFieldNames ? '' : 'joinedAt')
    ..aOB(5, _omitFieldNames ? '' : 'isSpeaking')
    ..aOB(6, _omitFieldNames ? '' : 'muted')
    ..aOB(7, _omitFieldNames ? '' : 'deafened')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ParticipantInfo clone() => ParticipantInfo()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ParticipantInfo copyWith(void Function(ParticipantInfo) updates) => super.copyWith((message) => updates(message as ParticipantInfo)) as ParticipantInfo;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ParticipantInfo create() => ParticipantInfo._();
  ParticipantInfo createEmptyInstance() => create();
  static $pb.PbList<ParticipantInfo> createRepeated() => $pb.PbList<ParticipantInfo>();
  @$core.pragma('dart2js:noInline')
  static ParticipantInfo getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ParticipantInfo>(create);
  static ParticipantInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get username => $_getSZ(1);
  @$pb.TagNumber(2)
  set username($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUsername() => $_has(1);
  @$pb.TagNumber(2)
  void clearUsername() => clearField(2);

  @$pb.TagNumber(3)
  ParticipantState get state => $_getN(2);
  @$pb.TagNumber(3)
  set state(ParticipantState v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasState() => $_has(2);
  @$pb.TagNumber(3)
  void clearState() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get joinedAt => $_getI64(3);
  @$pb.TagNumber(4)
  set joinedAt($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasJoinedAt() => $_has(3);
  @$pb.TagNumber(4)
  void clearJoinedAt() => clearField(4);

  @$pb.TagNumber(5)
  $core.bool get isSpeaking => $_getBF(4);
  @$pb.TagNumber(5)
  set isSpeaking($core.bool v) { $_setBool(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasIsSpeaking() => $_has(4);
  @$pb.TagNumber(5)
  void clearIsSpeaking() => clearField(5);

  /// Independent mute flags; `state` is derived from them
  /// (DEAFENED > MUTED > CONNECTED) so unmuting the mic while deafened
  /// keeps the participant DEAFENED.
  @$pb.TagNumber(6)
  $core.bool get muted => $_getBF(5);
  @$pb.TagNumber(6)
  set muted($core.bool v) { $_setBool(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasMuted() => $_has(5);
  @$pb.TagNumber(6)
  void clearMuted() => clearField(6);

  @$pb.TagNumber(7)
  $core.bool get deafened => $_getBF(6);
  @$pb.TagNumber(7)
  set deafened($core.bool v) { $_setBool(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasDeafened() => $_has(6);
  @$pb.TagNumber(7)
  void clearDeafened() => clearField(7);
}

/// Get room info
class GetRoomInfoRequest extends $pb.GeneratedMessage {
  factory GetRoomInfoRequest({
    $core.String? roomId,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    return $result;
  }
  GetRoomInfoRequest._() : super();
  factory GetRoomInfoRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetRoomInfoRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetRoomInfoRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetRoomInfoRequest clone() => GetRoomInfoRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetRoomInfoRequest copyWith(void Function(GetRoomInfoRequest) updates) => super.copyWith((message) => updates(message as GetRoomInfoRequest)) as GetRoomInfoRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetRoomInfoRequest create() => GetRoomInfoRequest._();
  GetRoomInfoRequest createEmptyInstance() => create();
  static $pb.PbList<GetRoomInfoRequest> createRepeated() => $pb.PbList<GetRoomInfoRequest>();
  @$core.pragma('dart2js:noInline')
  static GetRoomInfoRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetRoomInfoRequest>(create);
  static GetRoomInfoRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);
}

class GetRoomInfoResponse extends $pb.GeneratedMessage {
  factory GetRoomInfoResponse({
    $0.ErrorCode? code,
    $core.String? roomId,
    $core.String? roomName,
    RoomType? roomType,
    $core.Iterable<ParticipantInfo>? participants,
    $core.int? maxParticipants,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (roomName != null) {
      $result.roomName = roomName;
    }
    if (roomType != null) {
      $result.roomType = roomType;
    }
    if (participants != null) {
      $result.participants.addAll(participants);
    }
    if (maxParticipants != null) {
      $result.maxParticipants = maxParticipants;
    }
    return $result;
  }
  GetRoomInfoResponse._() : super();
  factory GetRoomInfoResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetRoomInfoResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetRoomInfoResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..aOS(3, _omitFieldNames ? '' : 'roomName')
    ..e<RoomType>(4, _omitFieldNames ? '' : 'roomType', $pb.PbFieldType.OE, defaultOrMaker: RoomType.PEER_TO_PEER, valueOf: RoomType.valueOf, enumValues: RoomType.values)
    ..pc<ParticipantInfo>(5, _omitFieldNames ? '' : 'participants', $pb.PbFieldType.PM, subBuilder: ParticipantInfo.create)
    ..a<$core.int>(6, _omitFieldNames ? '' : 'maxParticipants', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetRoomInfoResponse clone() => GetRoomInfoResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetRoomInfoResponse copyWith(void Function(GetRoomInfoResponse) updates) => super.copyWith((message) => updates(message as GetRoomInfoResponse)) as GetRoomInfoResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetRoomInfoResponse create() => GetRoomInfoResponse._();
  GetRoomInfoResponse createEmptyInstance() => create();
  static $pb.PbList<GetRoomInfoResponse> createRepeated() => $pb.PbList<GetRoomInfoResponse>();
  @$core.pragma('dart2js:noInline')
  static GetRoomInfoResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetRoomInfoResponse>(create);
  static GetRoomInfoResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get roomName => $_getSZ(2);
  @$pb.TagNumber(3)
  set roomName($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasRoomName() => $_has(2);
  @$pb.TagNumber(3)
  void clearRoomName() => clearField(3);

  @$pb.TagNumber(4)
  RoomType get roomType => $_getN(3);
  @$pb.TagNumber(4)
  set roomType(RoomType v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasRoomType() => $_has(3);
  @$pb.TagNumber(4)
  void clearRoomType() => clearField(4);

  @$pb.TagNumber(5)
  $core.List<ParticipantInfo> get participants => $_getList(4);

  @$pb.TagNumber(6)
  $core.int get maxParticipants => $_getIZ(5);
  @$pb.TagNumber(6)
  set maxParticipants($core.int v) { $_setSignedInt32(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasMaxParticipants() => $_has(5);
  @$pb.TagNumber(6)
  void clearMaxParticipants() => clearField(6);
}

/// Get user's current room
class GetUserRoomRequest extends $pb.GeneratedMessage {
  factory GetUserRoomRequest({
    $core.String? userId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetUserRoomRequest._() : super();
  factory GetUserRoomRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUserRoomRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUserRoomRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUserRoomRequest clone() => GetUserRoomRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUserRoomRequest copyWith(void Function(GetUserRoomRequest) updates) => super.copyWith((message) => updates(message as GetUserRoomRequest)) as GetUserRoomRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUserRoomRequest create() => GetUserRoomRequest._();
  GetUserRoomRequest createEmptyInstance() => create();
  static $pb.PbList<GetUserRoomRequest> createRepeated() => $pb.PbList<GetUserRoomRequest>();
  @$core.pragma('dart2js:noInline')
  static GetUserRoomRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUserRoomRequest>(create);
  static GetUserRoomRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);
}

class GetUserRoomResponse extends $pb.GeneratedMessage {
  factory GetUserRoomResponse({
    $0.ErrorCode? code,
    $core.String? roomId,
    ParticipantInfo? participant,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (participant != null) {
      $result.participant = participant;
    }
    return $result;
  }
  GetUserRoomResponse._() : super();
  factory GetUserRoomResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUserRoomResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUserRoomResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..aOM<ParticipantInfo>(3, _omitFieldNames ? '' : 'participant', subBuilder: ParticipantInfo.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUserRoomResponse clone() => GetUserRoomResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUserRoomResponse copyWith(void Function(GetUserRoomResponse) updates) => super.copyWith((message) => updates(message as GetUserRoomResponse)) as GetUserRoomResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUserRoomResponse create() => GetUserRoomResponse._();
  GetUserRoomResponse createEmptyInstance() => create();
  static $pb.PbList<GetUserRoomResponse> createRepeated() => $pb.PbList<GetUserRoomResponse>();
  @$core.pragma('dart2js:noInline')
  static GetUserRoomResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUserRoomResponse>(create);
  static GetUserRoomResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);

  @$pb.TagNumber(3)
  ParticipantInfo get participant => $_getN(2);
  @$pb.TagNumber(3)
  set participant(ParticipantInfo v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasParticipant() => $_has(2);
  @$pb.TagNumber(3)
  void clearParticipant() => clearField(3);
  @$pb.TagNumber(3)
  ParticipantInfo ensureParticipant() => $_ensure(2);
}

/// Mute/unmute microphone
class SetMuteRequest extends $pb.GeneratedMessage {
  factory SetMuteRequest({
    $core.String? userId,
    $core.String? roomId,
    $core.bool? muted,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (muted != null) {
      $result.muted = muted;
    }
    return $result;
  }
  SetMuteRequest._() : super();
  factory SetMuteRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetMuteRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetMuteRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..aOB(3, _omitFieldNames ? '' : 'muted')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetMuteRequest clone() => SetMuteRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetMuteRequest copyWith(void Function(SetMuteRequest) updates) => super.copyWith((message) => updates(message as SetMuteRequest)) as SetMuteRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetMuteRequest create() => SetMuteRequest._();
  SetMuteRequest createEmptyInstance() => create();
  static $pb.PbList<SetMuteRequest> createRepeated() => $pb.PbList<SetMuteRequest>();
  @$core.pragma('dart2js:noInline')
  static SetMuteRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetMuteRequest>(create);
  static SetMuteRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get muted => $_getBF(2);
  @$pb.TagNumber(3)
  set muted($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMuted() => $_has(2);
  @$pb.TagNumber(3)
  void clearMuted() => clearField(3);
}

class SetMuteResponse extends $pb.GeneratedMessage {
  factory SetMuteResponse({
    $0.ErrorCode? code,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  SetMuteResponse._() : super();
  factory SetMuteResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetMuteResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetMuteResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetMuteResponse clone() => SetMuteResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetMuteResponse copyWith(void Function(SetMuteResponse) updates) => super.copyWith((message) => updates(message as SetMuteResponse)) as SetMuteResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetMuteResponse create() => SetMuteResponse._();
  SetMuteResponse createEmptyInstance() => create();
  static $pb.PbList<SetMuteResponse> createRepeated() => $pb.PbList<SetMuteResponse>();
  @$core.pragma('dart2js:noInline')
  static SetMuteResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetMuteResponse>(create);
  static SetMuteResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get serverTime => $_getI64(1);
  @$pb.TagNumber(2)
  set serverTime($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasServerTime() => $_has(1);
  @$pb.TagNumber(2)
  void clearServerTime() => clearField(2);
}

/// Deafen/undeafen speaker
class SetDeafenRequest extends $pb.GeneratedMessage {
  factory SetDeafenRequest({
    $core.String? userId,
    $core.String? roomId,
    $core.bool? deafened,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (deafened != null) {
      $result.deafened = deafened;
    }
    return $result;
  }
  SetDeafenRequest._() : super();
  factory SetDeafenRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetDeafenRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetDeafenRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'roomId')
    ..aOB(3, _omitFieldNames ? '' : 'deafened')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetDeafenRequest clone() => SetDeafenRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetDeafenRequest copyWith(void Function(SetDeafenRequest) updates) => super.copyWith((message) => updates(message as SetDeafenRequest)) as SetDeafenRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetDeafenRequest create() => SetDeafenRequest._();
  SetDeafenRequest createEmptyInstance() => create();
  static $pb.PbList<SetDeafenRequest> createRepeated() => $pb.PbList<SetDeafenRequest>();
  @$core.pragma('dart2js:noInline')
  static SetDeafenRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetDeafenRequest>(create);
  static SetDeafenRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get roomId => $_getSZ(1);
  @$pb.TagNumber(2)
  set roomId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRoomId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRoomId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get deafened => $_getBF(2);
  @$pb.TagNumber(3)
  set deafened($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasDeafened() => $_has(2);
  @$pb.TagNumber(3)
  void clearDeafened() => clearField(3);
}

class SetDeafenResponse extends $pb.GeneratedMessage {
  factory SetDeafenResponse({
    $0.ErrorCode? code,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  SetDeafenResponse._() : super();
  factory SetDeafenResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetDeafenResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetDeafenResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetDeafenResponse clone() => SetDeafenResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetDeafenResponse copyWith(void Function(SetDeafenResponse) updates) => super.copyWith((message) => updates(message as SetDeafenResponse)) as SetDeafenResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetDeafenResponse create() => SetDeafenResponse._();
  SetDeafenResponse createEmptyInstance() => create();
  static $pb.PbList<SetDeafenResponse> createRepeated() => $pb.PbList<SetDeafenResponse>();
  @$core.pragma('dart2js:noInline')
  static SetDeafenResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetDeafenResponse>(create);
  static SetDeafenResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get serverTime => $_getI64(1);
  @$pb.TagNumber(2)
  set serverTime($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasServerTime() => $_has(1);
  @$pb.TagNumber(2)
  void clearServerTime() => clearField(2);
}

/// Notifications
class ParticipantJoinedNotify extends $pb.GeneratedMessage {
  factory ParticipantJoinedNotify({
    $core.String? roomId,
    ParticipantInfo? participant,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (participant != null) {
      $result.participant = participant;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ParticipantJoinedNotify._() : super();
  factory ParticipantJoinedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ParticipantJoinedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ParticipantJoinedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..aOM<ParticipantInfo>(2, _omitFieldNames ? '' : 'participant', subBuilder: ParticipantInfo.create)
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ParticipantJoinedNotify clone() => ParticipantJoinedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ParticipantJoinedNotify copyWith(void Function(ParticipantJoinedNotify) updates) => super.copyWith((message) => updates(message as ParticipantJoinedNotify)) as ParticipantJoinedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ParticipantJoinedNotify create() => ParticipantJoinedNotify._();
  ParticipantJoinedNotify createEmptyInstance() => create();
  static $pb.PbList<ParticipantJoinedNotify> createRepeated() => $pb.PbList<ParticipantJoinedNotify>();
  @$core.pragma('dart2js:noInline')
  static ParticipantJoinedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ParticipantJoinedNotify>(create);
  static ParticipantJoinedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);

  @$pb.TagNumber(2)
  ParticipantInfo get participant => $_getN(1);
  @$pb.TagNumber(2)
  set participant(ParticipantInfo v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasParticipant() => $_has(1);
  @$pb.TagNumber(2)
  void clearParticipant() => clearField(2);
  @$pb.TagNumber(2)
  ParticipantInfo ensureParticipant() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

class ParticipantLeftNotify extends $pb.GeneratedMessage {
  factory ParticipantLeftNotify({
    $core.String? roomId,
    $core.String? userId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ParticipantLeftNotify._() : super();
  factory ParticipantLeftNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ParticipantLeftNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ParticipantLeftNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ParticipantLeftNotify clone() => ParticipantLeftNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ParticipantLeftNotify copyWith(void Function(ParticipantLeftNotify) updates) => super.copyWith((message) => updates(message as ParticipantLeftNotify)) as ParticipantLeftNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ParticipantLeftNotify create() => ParticipantLeftNotify._();
  ParticipantLeftNotify createEmptyInstance() => create();
  static $pb.PbList<ParticipantLeftNotify> createRepeated() => $pb.PbList<ParticipantLeftNotify>();
  @$core.pragma('dart2js:noInline')
  static ParticipantLeftNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ParticipantLeftNotify>(create);
  static ParticipantLeftNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

class ParticipantStateChangedNotify extends $pb.GeneratedMessage {
  factory ParticipantStateChangedNotify({
    $core.String? roomId,
    $core.String? userId,
    ParticipantState? state,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (state != null) {
      $result.state = state;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ParticipantStateChangedNotify._() : super();
  factory ParticipantStateChangedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ParticipantStateChangedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ParticipantStateChangedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..e<ParticipantState>(3, _omitFieldNames ? '' : 'state', $pb.PbFieldType.OE, defaultOrMaker: ParticipantState.JOINING, valueOf: ParticipantState.valueOf, enumValues: ParticipantState.values)
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ParticipantStateChangedNotify clone() => ParticipantStateChangedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ParticipantStateChangedNotify copyWith(void Function(ParticipantStateChangedNotify) updates) => super.copyWith((message) => updates(message as ParticipantStateChangedNotify)) as ParticipantStateChangedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ParticipantStateChangedNotify create() => ParticipantStateChangedNotify._();
  ParticipantStateChangedNotify createEmptyInstance() => create();
  static $pb.PbList<ParticipantStateChangedNotify> createRepeated() => $pb.PbList<ParticipantStateChangedNotify>();
  @$core.pragma('dart2js:noInline')
  static ParticipantStateChangedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ParticipantStateChangedNotify>(create);
  static ParticipantStateChangedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  ParticipantState get state => $_getN(2);
  @$pb.TagNumber(3)
  set state(ParticipantState v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasState() => $_has(2);
  @$pb.TagNumber(3)
  void clearState() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);
}

class SpeakingNotify extends $pb.GeneratedMessage {
  factory SpeakingNotify({
    $core.String? roomId,
    $core.String? userId,
    $core.bool? speaking,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (roomId != null) {
      $result.roomId = roomId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (speaking != null) {
      $result.speaking = speaking;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  SpeakingNotify._() : super();
  factory SpeakingNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SpeakingNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SpeakingNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.voice'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'roomId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aOB(3, _omitFieldNames ? '' : 'speaking')
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SpeakingNotify clone() => SpeakingNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SpeakingNotify copyWith(void Function(SpeakingNotify) updates) => super.copyWith((message) => updates(message as SpeakingNotify)) as SpeakingNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SpeakingNotify create() => SpeakingNotify._();
  SpeakingNotify createEmptyInstance() => create();
  static $pb.PbList<SpeakingNotify> createRepeated() => $pb.PbList<SpeakingNotify>();
  @$core.pragma('dart2js:noInline')
  static SpeakingNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SpeakingNotify>(create);
  static SpeakingNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get roomId => $_getSZ(0);
  @$pb.TagNumber(1)
  set roomId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRoomId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRoomId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get speaking => $_getBF(2);
  @$pb.TagNumber(3)
  set speaking($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasSpeaking() => $_has(2);
  @$pb.TagNumber(3)
  void clearSpeaking() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);
}


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
