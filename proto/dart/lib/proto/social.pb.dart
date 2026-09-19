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

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'common.pbenum.dart' as $0;
import 'social.pbenum.dart';

export 'social.pbenum.dart';

/// Friend request
class FriendRequest extends $pb.GeneratedMessage {
  factory FriendRequest({
    $core.String? fromUserId,
    $core.String? toUserId,
    $core.String? message,
    $fixnum.Int64? timestamp,
    $core.String? requestId,
  }) {
    final $result = create();
    if (fromUserId != null) {
      $result.fromUserId = fromUserId;
    }
    if (toUserId != null) {
      $result.toUserId = toUserId;
    }
    if (message != null) {
      $result.message = message;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    if (requestId != null) {
      $result.requestId = requestId;
    }
    return $result;
  }
  FriendRequest._() : super();
  factory FriendRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FriendRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FriendRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'fromUserId')
    ..aOS(2, _omitFieldNames ? '' : 'toUserId')
    ..aOS(3, _omitFieldNames ? '' : 'message')
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..aOS(5, _omitFieldNames ? '' : 'requestId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FriendRequest clone() => FriendRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FriendRequest copyWith(void Function(FriendRequest) updates) => super.copyWith((message) => updates(message as FriendRequest)) as FriendRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FriendRequest create() => FriendRequest._();
  FriendRequest createEmptyInstance() => create();
  static $pb.PbList<FriendRequest> createRepeated() => $pb.PbList<FriendRequest>();
  @$core.pragma('dart2js:noInline')
  static FriendRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FriendRequest>(create);
  static FriendRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get fromUserId => $_getSZ(0);
  @$pb.TagNumber(1)
  set fromUserId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasFromUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearFromUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get toUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set toUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasToUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearToUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get message => $_getSZ(2);
  @$pb.TagNumber(3)
  set message($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMessage() => $_has(2);
  @$pb.TagNumber(3)
  void clearMessage() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);

  /// Filled by GET_PENDING_REQUESTS so the receiver can answer with
  /// FRIEND_REQUEST_ACTION; the notify (3022) carries the same id.
  @$pb.TagNumber(5)
  $core.String get requestId => $_getSZ(4);
  @$pb.TagNumber(5)
  set requestId($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasRequestId() => $_has(4);
  @$pb.TagNumber(5)
  void clearRequestId() => clearField(5);
}

/// Friend information
class FriendInfo extends $pb.GeneratedMessage {
  factory FriendInfo({
    $core.String? userId,
    $core.String? username,
    $core.String? avatarUrl,
    FriendStatus? status,
    $fixnum.Int64? addedAt,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (username != null) {
      $result.username = username;
    }
    if (avatarUrl != null) {
      $result.avatarUrl = avatarUrl;
    }
    if (status != null) {
      $result.status = status;
    }
    if (addedAt != null) {
      $result.addedAt = addedAt;
    }
    return $result;
  }
  FriendInfo._() : super();
  factory FriendInfo.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FriendInfo.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FriendInfo', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'username')
    ..aOS(3, _omitFieldNames ? '' : 'avatarUrl')
    ..e<FriendStatus>(4, _omitFieldNames ? '' : 'status', $pb.PbFieldType.OE, defaultOrMaker: FriendStatus.NONE, valueOf: FriendStatus.valueOf, enumValues: FriendStatus.values)
    ..aInt64(5, _omitFieldNames ? '' : 'addedAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FriendInfo clone() => FriendInfo()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FriendInfo copyWith(void Function(FriendInfo) updates) => super.copyWith((message) => updates(message as FriendInfo)) as FriendInfo;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FriendInfo create() => FriendInfo._();
  FriendInfo createEmptyInstance() => create();
  static $pb.PbList<FriendInfo> createRepeated() => $pb.PbList<FriendInfo>();
  @$core.pragma('dart2js:noInline')
  static FriendInfo getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FriendInfo>(create);
  static FriendInfo? _defaultInstance;

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
  $core.String get avatarUrl => $_getSZ(2);
  @$pb.TagNumber(3)
  set avatarUrl($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasAvatarUrl() => $_has(2);
  @$pb.TagNumber(3)
  void clearAvatarUrl() => clearField(3);

  @$pb.TagNumber(4)
  FriendStatus get status => $_getN(3);
  @$pb.TagNumber(4)
  set status(FriendStatus v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasStatus() => $_has(3);
  @$pb.TagNumber(4)
  void clearStatus() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get addedAt => $_getI64(4);
  @$pb.TagNumber(5)
  set addedAt($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasAddedAt() => $_has(4);
  @$pb.TagNumber(5)
  void clearAddedAt() => clearField(5);
}

/// Add friend request
class AddFriendRequest extends $pb.GeneratedMessage {
  factory AddFriendRequest({
    $core.String? userId,
    $core.String? targetUserId,
    $core.String? message,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    if (message != null) {
      $result.message = message;
    }
    return $result;
  }
  AddFriendRequest._() : super();
  factory AddFriendRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory AddFriendRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'AddFriendRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'targetUserId')
    ..aOS(3, _omitFieldNames ? '' : 'message')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  AddFriendRequest clone() => AddFriendRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  AddFriendRequest copyWith(void Function(AddFriendRequest) updates) => super.copyWith((message) => updates(message as AddFriendRequest)) as AddFriendRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AddFriendRequest create() => AddFriendRequest._();
  AddFriendRequest createEmptyInstance() => create();
  static $pb.PbList<AddFriendRequest> createRepeated() => $pb.PbList<AddFriendRequest>();
  @$core.pragma('dart2js:noInline')
  static AddFriendRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<AddFriendRequest>(create);
  static AddFriendRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get targetUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set targetUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTargetUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTargetUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get message => $_getSZ(2);
  @$pb.TagNumber(3)
  set message($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMessage() => $_has(2);
  @$pb.TagNumber(3)
  void clearMessage() => clearField(3);
}

class AddFriendResponse extends $pb.GeneratedMessage {
  factory AddFriendResponse({
    $0.ErrorCode? code,
    $core.String? requestId,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (requestId != null) {
      $result.requestId = requestId;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  AddFriendResponse._() : super();
  factory AddFriendResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory AddFriendResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'AddFriendResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'requestId')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  AddFriendResponse clone() => AddFriendResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  AddFriendResponse copyWith(void Function(AddFriendResponse) updates) => super.copyWith((message) => updates(message as AddFriendResponse)) as AddFriendResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AddFriendResponse create() => AddFriendResponse._();
  AddFriendResponse createEmptyInstance() => create();
  static $pb.PbList<AddFriendResponse> createRepeated() => $pb.PbList<AddFriendResponse>();
  @$core.pragma('dart2js:noInline')
  static AddFriendResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<AddFriendResponse>(create);
  static AddFriendResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get requestId => $_getSZ(1);
  @$pb.TagNumber(2)
  set requestId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRequestId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRequestId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Accept/decline friend request
class FriendRequestAction extends $pb.GeneratedMessage {
  factory FriendRequestAction({
    $core.String? userId,
    $core.String? requestId,
    $core.bool? accept,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (requestId != null) {
      $result.requestId = requestId;
    }
    if (accept != null) {
      $result.accept = accept;
    }
    return $result;
  }
  FriendRequestAction._() : super();
  factory FriendRequestAction.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FriendRequestAction.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FriendRequestAction', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'requestId')
    ..aOB(3, _omitFieldNames ? '' : 'accept')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FriendRequestAction clone() => FriendRequestAction()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FriendRequestAction copyWith(void Function(FriendRequestAction) updates) => super.copyWith((message) => updates(message as FriendRequestAction)) as FriendRequestAction;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FriendRequestAction create() => FriendRequestAction._();
  FriendRequestAction createEmptyInstance() => create();
  static $pb.PbList<FriendRequestAction> createRepeated() => $pb.PbList<FriendRequestAction>();
  @$core.pragma('dart2js:noInline')
  static FriendRequestAction getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FriendRequestAction>(create);
  static FriendRequestAction? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get requestId => $_getSZ(1);
  @$pb.TagNumber(2)
  set requestId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRequestId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRequestId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get accept => $_getBF(2);
  @$pb.TagNumber(3)
  set accept($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasAccept() => $_has(2);
  @$pb.TagNumber(3)
  void clearAccept() => clearField(3);
}

class FriendRequestActionResponse extends $pb.GeneratedMessage {
  factory FriendRequestActionResponse({
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
  FriendRequestActionResponse._() : super();
  factory FriendRequestActionResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FriendRequestActionResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FriendRequestActionResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FriendRequestActionResponse clone() => FriendRequestActionResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FriendRequestActionResponse copyWith(void Function(FriendRequestActionResponse) updates) => super.copyWith((message) => updates(message as FriendRequestActionResponse)) as FriendRequestActionResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FriendRequestActionResponse create() => FriendRequestActionResponse._();
  FriendRequestActionResponse createEmptyInstance() => create();
  static $pb.PbList<FriendRequestActionResponse> createRepeated() => $pb.PbList<FriendRequestActionResponse>();
  @$core.pragma('dart2js:noInline')
  static FriendRequestActionResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FriendRequestActionResponse>(create);
  static FriendRequestActionResponse? _defaultInstance;

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

/// Remove friend
class RemoveFriendRequest extends $pb.GeneratedMessage {
  factory RemoveFriendRequest({
    $core.String? userId,
    $core.String? friendUserId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (friendUserId != null) {
      $result.friendUserId = friendUserId;
    }
    return $result;
  }
  RemoveFriendRequest._() : super();
  factory RemoveFriendRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory RemoveFriendRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'RemoveFriendRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'friendUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  RemoveFriendRequest clone() => RemoveFriendRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  RemoveFriendRequest copyWith(void Function(RemoveFriendRequest) updates) => super.copyWith((message) => updates(message as RemoveFriendRequest)) as RemoveFriendRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RemoveFriendRequest create() => RemoveFriendRequest._();
  RemoveFriendRequest createEmptyInstance() => create();
  static $pb.PbList<RemoveFriendRequest> createRepeated() => $pb.PbList<RemoveFriendRequest>();
  @$core.pragma('dart2js:noInline')
  static RemoveFriendRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<RemoveFriendRequest>(create);
  static RemoveFriendRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get friendUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set friendUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFriendUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearFriendUserId() => clearField(2);
}

class RemoveFriendResponse extends $pb.GeneratedMessage {
  factory RemoveFriendResponse({
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
  RemoveFriendResponse._() : super();
  factory RemoveFriendResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory RemoveFriendResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'RemoveFriendResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  RemoveFriendResponse clone() => RemoveFriendResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  RemoveFriendResponse copyWith(void Function(RemoveFriendResponse) updates) => super.copyWith((message) => updates(message as RemoveFriendResponse)) as RemoveFriendResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RemoveFriendResponse create() => RemoveFriendResponse._();
  RemoveFriendResponse createEmptyInstance() => create();
  static $pb.PbList<RemoveFriendResponse> createRepeated() => $pb.PbList<RemoveFriendResponse>();
  @$core.pragma('dart2js:noInline')
  static RemoveFriendResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<RemoveFriendResponse>(create);
  static RemoveFriendResponse? _defaultInstance;

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

/// Get friend list
class GetFriendListRequest extends $pb.GeneratedMessage {
  factory GetFriendListRequest({
    $core.String? userId,
    $core.int? limit,
    $core.int? offset,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (limit != null) {
      $result.limit = limit;
    }
    if (offset != null) {
      $result.offset = offset;
    }
    return $result;
  }
  GetFriendListRequest._() : super();
  factory GetFriendListRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetFriendListRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetFriendListRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..a<$core.int>(2, _omitFieldNames ? '' : 'limit', $pb.PbFieldType.O3)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'offset', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetFriendListRequest clone() => GetFriendListRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetFriendListRequest copyWith(void Function(GetFriendListRequest) updates) => super.copyWith((message) => updates(message as GetFriendListRequest)) as GetFriendListRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetFriendListRequest create() => GetFriendListRequest._();
  GetFriendListRequest createEmptyInstance() => create();
  static $pb.PbList<GetFriendListRequest> createRepeated() => $pb.PbList<GetFriendListRequest>();
  @$core.pragma('dart2js:noInline')
  static GetFriendListRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetFriendListRequest>(create);
  static GetFriendListRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.int get limit => $_getIZ(1);
  @$pb.TagNumber(2)
  set limit($core.int v) { $_setSignedInt32(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasLimit() => $_has(1);
  @$pb.TagNumber(2)
  void clearLimit() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get offset => $_getIZ(2);
  @$pb.TagNumber(3)
  set offset($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasOffset() => $_has(2);
  @$pb.TagNumber(3)
  void clearOffset() => clearField(3);
}

class GetFriendListResponse extends $pb.GeneratedMessage {
  factory GetFriendListResponse({
    $0.ErrorCode? code,
    $core.Iterable<FriendInfo>? friends,
    $core.int? totalCount,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (friends != null) {
      $result.friends.addAll(friends);
    }
    if (totalCount != null) {
      $result.totalCount = totalCount;
    }
    return $result;
  }
  GetFriendListResponse._() : super();
  factory GetFriendListResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetFriendListResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetFriendListResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<FriendInfo>(2, _omitFieldNames ? '' : 'friends', $pb.PbFieldType.PM, subBuilder: FriendInfo.create)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'totalCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetFriendListResponse clone() => GetFriendListResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetFriendListResponse copyWith(void Function(GetFriendListResponse) updates) => super.copyWith((message) => updates(message as GetFriendListResponse)) as GetFriendListResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetFriendListResponse create() => GetFriendListResponse._();
  GetFriendListResponse createEmptyInstance() => create();
  static $pb.PbList<GetFriendListResponse> createRepeated() => $pb.PbList<GetFriendListResponse>();
  @$core.pragma('dart2js:noInline')
  static GetFriendListResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetFriendListResponse>(create);
  static GetFriendListResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<FriendInfo> get friends => $_getList(1);

  @$pb.TagNumber(3)
  $core.int get totalCount => $_getIZ(2);
  @$pb.TagNumber(3)
  set totalCount($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTotalCount() => $_has(2);
  @$pb.TagNumber(3)
  void clearTotalCount() => clearField(3);
}

/// Get pending friend requests
class GetPendingRequestsRequest extends $pb.GeneratedMessage {
  factory GetPendingRequestsRequest({
    $core.String? userId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetPendingRequestsRequest._() : super();
  factory GetPendingRequestsRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPendingRequestsRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPendingRequestsRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPendingRequestsRequest clone() => GetPendingRequestsRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPendingRequestsRequest copyWith(void Function(GetPendingRequestsRequest) updates) => super.copyWith((message) => updates(message as GetPendingRequestsRequest)) as GetPendingRequestsRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPendingRequestsRequest create() => GetPendingRequestsRequest._();
  GetPendingRequestsRequest createEmptyInstance() => create();
  static $pb.PbList<GetPendingRequestsRequest> createRepeated() => $pb.PbList<GetPendingRequestsRequest>();
  @$core.pragma('dart2js:noInline')
  static GetPendingRequestsRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPendingRequestsRequest>(create);
  static GetPendingRequestsRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);
}

class GetPendingRequestsResponse extends $pb.GeneratedMessage {
  factory GetPendingRequestsResponse({
    $0.ErrorCode? code,
    $core.Iterable<FriendRequest>? requests,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (requests != null) {
      $result.requests.addAll(requests);
    }
    return $result;
  }
  GetPendingRequestsResponse._() : super();
  factory GetPendingRequestsResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPendingRequestsResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPendingRequestsResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<FriendRequest>(2, _omitFieldNames ? '' : 'requests', $pb.PbFieldType.PM, subBuilder: FriendRequest.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPendingRequestsResponse clone() => GetPendingRequestsResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPendingRequestsResponse copyWith(void Function(GetPendingRequestsResponse) updates) => super.copyWith((message) => updates(message as GetPendingRequestsResponse)) as GetPendingRequestsResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPendingRequestsResponse create() => GetPendingRequestsResponse._();
  GetPendingRequestsResponse createEmptyInstance() => create();
  static $pb.PbList<GetPendingRequestsResponse> createRepeated() => $pb.PbList<GetPendingRequestsResponse>();
  @$core.pragma('dart2js:noInline')
  static GetPendingRequestsResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPendingRequestsResponse>(create);
  static GetPendingRequestsResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<FriendRequest> get requests => $_getList(1);
}

/// Block user
class BlockUserRequest extends $pb.GeneratedMessage {
  factory BlockUserRequest({
    $core.String? userId,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  BlockUserRequest._() : super();
  factory BlockUserRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BlockUserRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BlockUserRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BlockUserRequest clone() => BlockUserRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BlockUserRequest copyWith(void Function(BlockUserRequest) updates) => super.copyWith((message) => updates(message as BlockUserRequest)) as BlockUserRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BlockUserRequest create() => BlockUserRequest._();
  BlockUserRequest createEmptyInstance() => create();
  static $pb.PbList<BlockUserRequest> createRepeated() => $pb.PbList<BlockUserRequest>();
  @$core.pragma('dart2js:noInline')
  static BlockUserRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BlockUserRequest>(create);
  static BlockUserRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get targetUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set targetUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTargetUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTargetUserId() => clearField(2);
}

class BlockUserResponse extends $pb.GeneratedMessage {
  factory BlockUserResponse({
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
  BlockUserResponse._() : super();
  factory BlockUserResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BlockUserResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BlockUserResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BlockUserResponse clone() => BlockUserResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BlockUserResponse copyWith(void Function(BlockUserResponse) updates) => super.copyWith((message) => updates(message as BlockUserResponse)) as BlockUserResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BlockUserResponse create() => BlockUserResponse._();
  BlockUserResponse createEmptyInstance() => create();
  static $pb.PbList<BlockUserResponse> createRepeated() => $pb.PbList<BlockUserResponse>();
  @$core.pragma('dart2js:noInline')
  static BlockUserResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BlockUserResponse>(create);
  static BlockUserResponse? _defaultInstance;

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

/// Unblock user
class UnblockUserRequest extends $pb.GeneratedMessage {
  factory UnblockUserRequest({
    $core.String? userId,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  UnblockUserRequest._() : super();
  factory UnblockUserRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnblockUserRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnblockUserRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnblockUserRequest clone() => UnblockUserRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnblockUserRequest copyWith(void Function(UnblockUserRequest) updates) => super.copyWith((message) => updates(message as UnblockUserRequest)) as UnblockUserRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnblockUserRequest create() => UnblockUserRequest._();
  UnblockUserRequest createEmptyInstance() => create();
  static $pb.PbList<UnblockUserRequest> createRepeated() => $pb.PbList<UnblockUserRequest>();
  @$core.pragma('dart2js:noInline')
  static UnblockUserRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnblockUserRequest>(create);
  static UnblockUserRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get targetUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set targetUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTargetUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTargetUserId() => clearField(2);
}

class UnblockUserResponse extends $pb.GeneratedMessage {
  factory UnblockUserResponse({
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
  UnblockUserResponse._() : super();
  factory UnblockUserResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnblockUserResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnblockUserResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnblockUserResponse clone() => UnblockUserResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnblockUserResponse copyWith(void Function(UnblockUserResponse) updates) => super.copyWith((message) => updates(message as UnblockUserResponse)) as UnblockUserResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnblockUserResponse create() => UnblockUserResponse._();
  UnblockUserResponse createEmptyInstance() => create();
  static $pb.PbList<UnblockUserResponse> createRepeated() => $pb.PbList<UnblockUserResponse>();
  @$core.pragma('dart2js:noInline')
  static UnblockUserResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnblockUserResponse>(create);
  static UnblockUserResponse? _defaultInstance;

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

/// Get blocked list
class GetBlockedListRequest extends $pb.GeneratedMessage {
  factory GetBlockedListRequest({
    $core.String? userId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetBlockedListRequest._() : super();
  factory GetBlockedListRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetBlockedListRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetBlockedListRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetBlockedListRequest clone() => GetBlockedListRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetBlockedListRequest copyWith(void Function(GetBlockedListRequest) updates) => super.copyWith((message) => updates(message as GetBlockedListRequest)) as GetBlockedListRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetBlockedListRequest create() => GetBlockedListRequest._();
  GetBlockedListRequest createEmptyInstance() => create();
  static $pb.PbList<GetBlockedListRequest> createRepeated() => $pb.PbList<GetBlockedListRequest>();
  @$core.pragma('dart2js:noInline')
  static GetBlockedListRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetBlockedListRequest>(create);
  static GetBlockedListRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);
}

class GetBlockedListResponse extends $pb.GeneratedMessage {
  factory GetBlockedListResponse({
    $0.ErrorCode? code,
    $core.Iterable<$core.String>? blockedUserIds,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (blockedUserIds != null) {
      $result.blockedUserIds.addAll(blockedUserIds);
    }
    return $result;
  }
  GetBlockedListResponse._() : super();
  factory GetBlockedListResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetBlockedListResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetBlockedListResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pPS(2, _omitFieldNames ? '' : 'blockedUserIds')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetBlockedListResponse clone() => GetBlockedListResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetBlockedListResponse copyWith(void Function(GetBlockedListResponse) updates) => super.copyWith((message) => updates(message as GetBlockedListResponse)) as GetBlockedListResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetBlockedListResponse create() => GetBlockedListResponse._();
  GetBlockedListResponse createEmptyInstance() => create();
  static $pb.PbList<GetBlockedListResponse> createRepeated() => $pb.PbList<GetBlockedListResponse>();
  @$core.pragma('dart2js:noInline')
  static GetBlockedListResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetBlockedListResponse>(create);
  static GetBlockedListResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<$core.String> get blockedUserIds => $_getList(1);
}

/// Set presence
class SetPresenceRequest extends $pb.GeneratedMessage {
  factory SetPresenceRequest({
    $core.String? userId,
    PresenceStatus? status,
    $core.String? statusMessage,
    $core.Map<$core.String, $core.String>? metadata,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (status != null) {
      $result.status = status;
    }
    if (statusMessage != null) {
      $result.statusMessage = statusMessage;
    }
    if (metadata != null) {
      $result.metadata.addAll(metadata);
    }
    return $result;
  }
  SetPresenceRequest._() : super();
  factory SetPresenceRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetPresenceRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetPresenceRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..e<PresenceStatus>(2, _omitFieldNames ? '' : 'status', $pb.PbFieldType.OE, defaultOrMaker: PresenceStatus.OFFLINE, valueOf: PresenceStatus.valueOf, enumValues: PresenceStatus.values)
    ..aOS(3, _omitFieldNames ? '' : 'statusMessage')
    ..m<$core.String, $core.String>(4, _omitFieldNames ? '' : 'metadata', entryClassName: 'SetPresenceRequest.MetadataEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.social'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetPresenceRequest clone() => SetPresenceRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetPresenceRequest copyWith(void Function(SetPresenceRequest) updates) => super.copyWith((message) => updates(message as SetPresenceRequest)) as SetPresenceRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetPresenceRequest create() => SetPresenceRequest._();
  SetPresenceRequest createEmptyInstance() => create();
  static $pb.PbList<SetPresenceRequest> createRepeated() => $pb.PbList<SetPresenceRequest>();
  @$core.pragma('dart2js:noInline')
  static SetPresenceRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetPresenceRequest>(create);
  static SetPresenceRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  PresenceStatus get status => $_getN(1);
  @$pb.TagNumber(2)
  set status(PresenceStatus v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasStatus() => $_has(1);
  @$pb.TagNumber(2)
  void clearStatus() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get statusMessage => $_getSZ(2);
  @$pb.TagNumber(3)
  set statusMessage($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasStatusMessage() => $_has(2);
  @$pb.TagNumber(3)
  void clearStatusMessage() => clearField(3);

  @$pb.TagNumber(4)
  $core.Map<$core.String, $core.String> get metadata => $_getMap(3);
}

class SetPresenceResponse extends $pb.GeneratedMessage {
  factory SetPresenceResponse({
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
  SetPresenceResponse._() : super();
  factory SetPresenceResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetPresenceResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetPresenceResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetPresenceResponse clone() => SetPresenceResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetPresenceResponse copyWith(void Function(SetPresenceResponse) updates) => super.copyWith((message) => updates(message as SetPresenceResponse)) as SetPresenceResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetPresenceResponse create() => SetPresenceResponse._();
  SetPresenceResponse createEmptyInstance() => create();
  static $pb.PbList<SetPresenceResponse> createRepeated() => $pb.PbList<SetPresenceResponse>();
  @$core.pragma('dart2js:noInline')
  static SetPresenceResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetPresenceResponse>(create);
  static SetPresenceResponse? _defaultInstance;

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

/// Presence information
class PresenceInfo extends $pb.GeneratedMessage {
  factory PresenceInfo({
    $core.String? userId,
    PresenceStatus? status,
    $core.String? statusMessage,
    $fixnum.Int64? lastSeen,
    $core.Map<$core.String, $core.String>? metadata,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (status != null) {
      $result.status = status;
    }
    if (statusMessage != null) {
      $result.statusMessage = statusMessage;
    }
    if (lastSeen != null) {
      $result.lastSeen = lastSeen;
    }
    if (metadata != null) {
      $result.metadata.addAll(metadata);
    }
    return $result;
  }
  PresenceInfo._() : super();
  factory PresenceInfo.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PresenceInfo.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PresenceInfo', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..e<PresenceStatus>(2, _omitFieldNames ? '' : 'status', $pb.PbFieldType.OE, defaultOrMaker: PresenceStatus.OFFLINE, valueOf: PresenceStatus.valueOf, enumValues: PresenceStatus.values)
    ..aOS(3, _omitFieldNames ? '' : 'statusMessage')
    ..aInt64(4, _omitFieldNames ? '' : 'lastSeen')
    ..m<$core.String, $core.String>(5, _omitFieldNames ? '' : 'metadata', entryClassName: 'PresenceInfo.MetadataEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.social'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PresenceInfo clone() => PresenceInfo()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PresenceInfo copyWith(void Function(PresenceInfo) updates) => super.copyWith((message) => updates(message as PresenceInfo)) as PresenceInfo;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PresenceInfo create() => PresenceInfo._();
  PresenceInfo createEmptyInstance() => create();
  static $pb.PbList<PresenceInfo> createRepeated() => $pb.PbList<PresenceInfo>();
  @$core.pragma('dart2js:noInline')
  static PresenceInfo getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PresenceInfo>(create);
  static PresenceInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  PresenceStatus get status => $_getN(1);
  @$pb.TagNumber(2)
  set status(PresenceStatus v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasStatus() => $_has(1);
  @$pb.TagNumber(2)
  void clearStatus() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get statusMessage => $_getSZ(2);
  @$pb.TagNumber(3)
  set statusMessage($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasStatusMessage() => $_has(2);
  @$pb.TagNumber(3)
  void clearStatusMessage() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get lastSeen => $_getI64(3);
  @$pb.TagNumber(4)
  set lastSeen($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasLastSeen() => $_has(3);
  @$pb.TagNumber(4)
  void clearLastSeen() => clearField(4);

  @$pb.TagNumber(5)
  $core.Map<$core.String, $core.String> get metadata => $_getMap(4);
}

/// Get presence of users
class GetPresenceRequest extends $pb.GeneratedMessage {
  factory GetPresenceRequest({
    $core.Iterable<$core.String>? userIds,
  }) {
    final $result = create();
    if (userIds != null) {
      $result.userIds.addAll(userIds);
    }
    return $result;
  }
  GetPresenceRequest._() : super();
  factory GetPresenceRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPresenceRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPresenceRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..pPS(1, _omitFieldNames ? '' : 'userIds')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPresenceRequest clone() => GetPresenceRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPresenceRequest copyWith(void Function(GetPresenceRequest) updates) => super.copyWith((message) => updates(message as GetPresenceRequest)) as GetPresenceRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPresenceRequest create() => GetPresenceRequest._();
  GetPresenceRequest createEmptyInstance() => create();
  static $pb.PbList<GetPresenceRequest> createRepeated() => $pb.PbList<GetPresenceRequest>();
  @$core.pragma('dart2js:noInline')
  static GetPresenceRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPresenceRequest>(create);
  static GetPresenceRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.String> get userIds => $_getList(0);
}

class GetPresenceResponse extends $pb.GeneratedMessage {
  factory GetPresenceResponse({
    $0.ErrorCode? code,
    $core.Iterable<PresenceInfo>? presences,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (presences != null) {
      $result.presences.addAll(presences);
    }
    return $result;
  }
  GetPresenceResponse._() : super();
  factory GetPresenceResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPresenceResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPresenceResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<PresenceInfo>(2, _omitFieldNames ? '' : 'presences', $pb.PbFieldType.PM, subBuilder: PresenceInfo.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPresenceResponse clone() => GetPresenceResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPresenceResponse copyWith(void Function(GetPresenceResponse) updates) => super.copyWith((message) => updates(message as GetPresenceResponse)) as GetPresenceResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPresenceResponse create() => GetPresenceResponse._();
  GetPresenceResponse createEmptyInstance() => create();
  static $pb.PbList<GetPresenceResponse> createRepeated() => $pb.PbList<GetPresenceResponse>();
  @$core.pragma('dart2js:noInline')
  static GetPresenceResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPresenceResponse>(create);
  static GetPresenceResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<PresenceInfo> get presences => $_getList(1);
}

/// Presence notification (broadcast to friends)
class PresenceNotify extends $pb.GeneratedMessage {
  factory PresenceNotify({
    $core.String? userId,
    PresenceStatus? status,
    $core.String? statusMessage,
    $fixnum.Int64? timestamp,
    $core.Map<$core.String, $core.String>? metadata,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (status != null) {
      $result.status = status;
    }
    if (statusMessage != null) {
      $result.statusMessage = statusMessage;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    if (metadata != null) {
      $result.metadata.addAll(metadata);
    }
    return $result;
  }
  PresenceNotify._() : super();
  factory PresenceNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PresenceNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PresenceNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..e<PresenceStatus>(2, _omitFieldNames ? '' : 'status', $pb.PbFieldType.OE, defaultOrMaker: PresenceStatus.OFFLINE, valueOf: PresenceStatus.valueOf, enumValues: PresenceStatus.values)
    ..aOS(3, _omitFieldNames ? '' : 'statusMessage')
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..m<$core.String, $core.String>(5, _omitFieldNames ? '' : 'metadata', entryClassName: 'PresenceNotify.MetadataEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.social'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PresenceNotify clone() => PresenceNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PresenceNotify copyWith(void Function(PresenceNotify) updates) => super.copyWith((message) => updates(message as PresenceNotify)) as PresenceNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PresenceNotify create() => PresenceNotify._();
  PresenceNotify createEmptyInstance() => create();
  static $pb.PbList<PresenceNotify> createRepeated() => $pb.PbList<PresenceNotify>();
  @$core.pragma('dart2js:noInline')
  static PresenceNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PresenceNotify>(create);
  static PresenceNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  PresenceStatus get status => $_getN(1);
  @$pb.TagNumber(2)
  set status(PresenceStatus v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasStatus() => $_has(1);
  @$pb.TagNumber(2)
  void clearStatus() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get statusMessage => $_getSZ(2);
  @$pb.TagNumber(3)
  set statusMessage($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasStatusMessage() => $_has(2);
  @$pb.TagNumber(3)
  void clearStatusMessage() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);

  @$pb.TagNumber(5)
  $core.Map<$core.String, $core.String> get metadata => $_getMap(4);
}

/// Friend request notification (sent to target user)
class FriendRequestNotify extends $pb.GeneratedMessage {
  factory FriendRequestNotify({
    $core.String? requestId,
    $core.String? fromUserId,
    $core.String? fromUsername,
    $core.String? message,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (requestId != null) {
      $result.requestId = requestId;
    }
    if (fromUserId != null) {
      $result.fromUserId = fromUserId;
    }
    if (fromUsername != null) {
      $result.fromUsername = fromUsername;
    }
    if (message != null) {
      $result.message = message;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  FriendRequestNotify._() : super();
  factory FriendRequestNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FriendRequestNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FriendRequestNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'requestId')
    ..aOS(2, _omitFieldNames ? '' : 'fromUserId')
    ..aOS(3, _omitFieldNames ? '' : 'fromUsername')
    ..aOS(4, _omitFieldNames ? '' : 'message')
    ..aInt64(5, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FriendRequestNotify clone() => FriendRequestNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FriendRequestNotify copyWith(void Function(FriendRequestNotify) updates) => super.copyWith((message) => updates(message as FriendRequestNotify)) as FriendRequestNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FriendRequestNotify create() => FriendRequestNotify._();
  FriendRequestNotify createEmptyInstance() => create();
  static $pb.PbList<FriendRequestNotify> createRepeated() => $pb.PbList<FriendRequestNotify>();
  @$core.pragma('dart2js:noInline')
  static FriendRequestNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FriendRequestNotify>(create);
  static FriendRequestNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get requestId => $_getSZ(0);
  @$pb.TagNumber(1)
  set requestId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRequestId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRequestId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get fromUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set fromUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFromUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearFromUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get fromUsername => $_getSZ(2);
  @$pb.TagNumber(3)
  set fromUsername($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasFromUsername() => $_has(2);
  @$pb.TagNumber(3)
  void clearFromUsername() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get message => $_getSZ(3);
  @$pb.TagNumber(4)
  set message($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasMessage() => $_has(3);
  @$pb.TagNumber(4)
  void clearMessage() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get timestamp => $_getI64(4);
  @$pb.TagNumber(5)
  set timestamp($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasTimestamp() => $_has(4);
  @$pb.TagNumber(5)
  void clearTimestamp() => clearField(5);
}

/// Friend request accepted notification
class FriendAcceptedNotify extends $pb.GeneratedMessage {
  factory FriendAcceptedNotify({
    $core.String? userId,
    $core.String? username,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (username != null) {
      $result.username = username;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  FriendAcceptedNotify._() : super();
  factory FriendAcceptedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FriendAcceptedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FriendAcceptedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'username')
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FriendAcceptedNotify clone() => FriendAcceptedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FriendAcceptedNotify copyWith(void Function(FriendAcceptedNotify) updates) => super.copyWith((message) => updates(message as FriendAcceptedNotify)) as FriendAcceptedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FriendAcceptedNotify create() => FriendAcceptedNotify._();
  FriendAcceptedNotify createEmptyInstance() => create();
  static $pb.PbList<FriendAcceptedNotify> createRepeated() => $pb.PbList<FriendAcceptedNotify>();
  @$core.pragma('dart2js:noInline')
  static FriendAcceptedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FriendAcceptedNotify>(create);
  static FriendAcceptedNotify? _defaultInstance;

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
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

/// Friend removed notification
class FriendRemovedNotify extends $pb.GeneratedMessage {
  factory FriendRemovedNotify({
    $core.String? userId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  FriendRemovedNotify._() : super();
  factory FriendRemovedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FriendRemovedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FriendRemovedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aInt64(2, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FriendRemovedNotify clone() => FriendRemovedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FriendRemovedNotify copyWith(void Function(FriendRemovedNotify) updates) => super.copyWith((message) => updates(message as FriendRemovedNotify)) as FriendRemovedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FriendRemovedNotify create() => FriendRemovedNotify._();
  FriendRemovedNotify createEmptyInstance() => create();
  static $pb.PbList<FriendRemovedNotify> createRepeated() => $pb.PbList<FriendRemovedNotify>();
  @$core.pragma('dart2js:noInline')
  static FriendRemovedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FriendRemovedNotify>(create);
  static FriendRemovedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get timestamp => $_getI64(1);
  @$pb.TagNumber(2)
  set timestamp($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTimestamp() => $_has(1);
  @$pb.TagNumber(2)
  void clearTimestamp() => clearField(2);
}

/// ---------------------------------------------------------------------------
/// Server-side storage snapshots. These are Redis VALUES (one key per user,
/// e.g. "chirp:social:friends:<uid>"), never wire messages. Keeping them
/// separate from the response messages lets the storage schema evolve without
/// touching the client-facing protocol: a friend is stored as just an id set
/// (the server holds no profiles), and pending requests keep both sides of
/// each request keyed by request_id (duplicated across the two users'
/// snapshots; the loader dedupes by request_id).
class StoredFriendList extends $pb.GeneratedMessage {
  factory StoredFriendList({
    $core.Iterable<$core.String>? friendUserIds,
  }) {
    final $result = create();
    if (friendUserIds != null) {
      $result.friendUserIds.addAll(friendUserIds);
    }
    return $result;
  }
  StoredFriendList._() : super();
  factory StoredFriendList.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredFriendList.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredFriendList', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..pPS(1, _omitFieldNames ? '' : 'friendUserIds')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredFriendList clone() => StoredFriendList()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredFriendList copyWith(void Function(StoredFriendList) updates) => super.copyWith((message) => updates(message as StoredFriendList)) as StoredFriendList;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredFriendList create() => StoredFriendList._();
  StoredFriendList createEmptyInstance() => create();
  static $pb.PbList<StoredFriendList> createRepeated() => $pb.PbList<StoredFriendList>();
  @$core.pragma('dart2js:noInline')
  static StoredFriendList getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredFriendList>(create);
  static StoredFriendList? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.String> get friendUserIds => $_getList(0);
}

class StoredPendingRequests extends $pb.GeneratedMessage {
  factory StoredPendingRequests({
    $core.Map<$core.String, FriendRequest>? requests,
  }) {
    final $result = create();
    if (requests != null) {
      $result.requests.addAll(requests);
    }
    return $result;
  }
  StoredPendingRequests._() : super();
  factory StoredPendingRequests.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredPendingRequests.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredPendingRequests', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..m<$core.String, FriendRequest>(1, _omitFieldNames ? '' : 'requests', entryClassName: 'StoredPendingRequests.RequestsEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OM, valueCreator: FriendRequest.create, valueDefaultOrMaker: FriendRequest.getDefault, packageName: const $pb.PackageName('chirp.social'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredPendingRequests clone() => StoredPendingRequests()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredPendingRequests copyWith(void Function(StoredPendingRequests) updates) => super.copyWith((message) => updates(message as StoredPendingRequests)) as StoredPendingRequests;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredPendingRequests create() => StoredPendingRequests._();
  StoredPendingRequests createEmptyInstance() => create();
  static $pb.PbList<StoredPendingRequests> createRepeated() => $pb.PbList<StoredPendingRequests>();
  @$core.pragma('dart2js:noInline')
  static StoredPendingRequests getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredPendingRequests>(create);
  static StoredPendingRequests? _defaultInstance;

  @$pb.TagNumber(1)
  $core.Map<$core.String, FriendRequest> get requests => $_getMap(0);
}

class StoredBlockedList extends $pb.GeneratedMessage {
  factory StoredBlockedList({
    $core.Iterable<$core.String>? blockedUserIds,
  }) {
    final $result = create();
    if (blockedUserIds != null) {
      $result.blockedUserIds.addAll(blockedUserIds);
    }
    return $result;
  }
  StoredBlockedList._() : super();
  factory StoredBlockedList.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredBlockedList.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredBlockedList', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.social'), createEmptyInstance: create)
    ..pPS(1, _omitFieldNames ? '' : 'blockedUserIds')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredBlockedList clone() => StoredBlockedList()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredBlockedList copyWith(void Function(StoredBlockedList) updates) => super.copyWith((message) => updates(message as StoredBlockedList)) as StoredBlockedList;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredBlockedList create() => StoredBlockedList._();
  StoredBlockedList createEmptyInstance() => create();
  static $pb.PbList<StoredBlockedList> createRepeated() => $pb.PbList<StoredBlockedList>();
  @$core.pragma('dart2js:noInline')
  static StoredBlockedList getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredBlockedList>(create);
  static StoredBlockedList? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.String> get blockedUserIds => $_getList(0);
}


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
