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

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'app_notification.pbenum.dart';
import 'common.pbenum.dart' as $0;

export 'app_notification.pbenum.dart';

/// Push notification request
class PushNotificationRequest extends $pb.GeneratedMessage {
  factory PushNotificationRequest({
    $core.String? userId,
    NotificationType? type,
    NotificationPriority? priority,
    $core.String? title,
    $core.String? body,
    $core.String? icon,
    $core.String? image,
    $core.String? sound,
    $core.String? tag,
    $core.Map<$core.String, $core.String>? data,
    $core.int? badge,
    $core.String? clickAction,
    $fixnum.Int64? ttlMs,
    $core.bool? collapseKey,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (type != null) {
      $result.type = type;
    }
    if (priority != null) {
      $result.priority = priority;
    }
    if (title != null) {
      $result.title = title;
    }
    if (body != null) {
      $result.body = body;
    }
    if (icon != null) {
      $result.icon = icon;
    }
    if (image != null) {
      $result.image = image;
    }
    if (sound != null) {
      $result.sound = sound;
    }
    if (tag != null) {
      $result.tag = tag;
    }
    if (data != null) {
      $result.data.addAll(data);
    }
    if (badge != null) {
      $result.badge = badge;
    }
    if (clickAction != null) {
      $result.clickAction = clickAction;
    }
    if (ttlMs != null) {
      $result.ttlMs = ttlMs;
    }
    if (collapseKey != null) {
      $result.collapseKey = collapseKey;
    }
    return $result;
  }
  PushNotificationRequest._() : super();
  factory PushNotificationRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PushNotificationRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PushNotificationRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..e<NotificationType>(2, _omitFieldNames ? '' : 'type', $pb.PbFieldType.OE, defaultOrMaker: NotificationType.MESSAGE, valueOf: NotificationType.valueOf, enumValues: NotificationType.values)
    ..e<NotificationPriority>(3, _omitFieldNames ? '' : 'priority', $pb.PbFieldType.OE, defaultOrMaker: NotificationPriority.LOW, valueOf: NotificationPriority.valueOf, enumValues: NotificationPriority.values)
    ..aOS(4, _omitFieldNames ? '' : 'title')
    ..aOS(5, _omitFieldNames ? '' : 'body')
    ..aOS(6, _omitFieldNames ? '' : 'icon')
    ..aOS(7, _omitFieldNames ? '' : 'image')
    ..aOS(8, _omitFieldNames ? '' : 'sound')
    ..aOS(9, _omitFieldNames ? '' : 'tag')
    ..m<$core.String, $core.String>(10, _omitFieldNames ? '' : 'data', entryClassName: 'PushNotificationRequest.DataEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.app_notification'))
    ..a<$core.int>(11, _omitFieldNames ? '' : 'badge', $pb.PbFieldType.O3)
    ..aOS(12, _omitFieldNames ? '' : 'clickAction')
    ..aInt64(13, _omitFieldNames ? '' : 'ttlMs')
    ..aOB(14, _omitFieldNames ? '' : 'collapseKey')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PushNotificationRequest clone() => PushNotificationRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PushNotificationRequest copyWith(void Function(PushNotificationRequest) updates) => super.copyWith((message) => updates(message as PushNotificationRequest)) as PushNotificationRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PushNotificationRequest create() => PushNotificationRequest._();
  PushNotificationRequest createEmptyInstance() => create();
  static $pb.PbList<PushNotificationRequest> createRepeated() => $pb.PbList<PushNotificationRequest>();
  @$core.pragma('dart2js:noInline')
  static PushNotificationRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PushNotificationRequest>(create);
  static PushNotificationRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  NotificationType get type => $_getN(1);
  @$pb.TagNumber(2)
  set type(NotificationType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasType() => $_has(1);
  @$pb.TagNumber(2)
  void clearType() => clearField(2);

  @$pb.TagNumber(3)
  NotificationPriority get priority => $_getN(2);
  @$pb.TagNumber(3)
  set priority(NotificationPriority v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasPriority() => $_has(2);
  @$pb.TagNumber(3)
  void clearPriority() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get title => $_getSZ(3);
  @$pb.TagNumber(4)
  set title($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTitle() => $_has(3);
  @$pb.TagNumber(4)
  void clearTitle() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get body => $_getSZ(4);
  @$pb.TagNumber(5)
  set body($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasBody() => $_has(4);
  @$pb.TagNumber(5)
  void clearBody() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get icon => $_getSZ(5);
  @$pb.TagNumber(6)
  set icon($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasIcon() => $_has(5);
  @$pb.TagNumber(6)
  void clearIcon() => clearField(6);

  @$pb.TagNumber(7)
  $core.String get image => $_getSZ(6);
  @$pb.TagNumber(7)
  set image($core.String v) { $_setString(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasImage() => $_has(6);
  @$pb.TagNumber(7)
  void clearImage() => clearField(7);

  @$pb.TagNumber(8)
  $core.String get sound => $_getSZ(7);
  @$pb.TagNumber(8)
  set sound($core.String v) { $_setString(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasSound() => $_has(7);
  @$pb.TagNumber(8)
  void clearSound() => clearField(8);

  @$pb.TagNumber(9)
  $core.String get tag => $_getSZ(8);
  @$pb.TagNumber(9)
  set tag($core.String v) { $_setString(8, v); }
  @$pb.TagNumber(9)
  $core.bool hasTag() => $_has(8);
  @$pb.TagNumber(9)
  void clearTag() => clearField(9);

  @$pb.TagNumber(10)
  $core.Map<$core.String, $core.String> get data => $_getMap(9);

  @$pb.TagNumber(11)
  $core.int get badge => $_getIZ(10);
  @$pb.TagNumber(11)
  set badge($core.int v) { $_setSignedInt32(10, v); }
  @$pb.TagNumber(11)
  $core.bool hasBadge() => $_has(10);
  @$pb.TagNumber(11)
  void clearBadge() => clearField(11);

  @$pb.TagNumber(12)
  $core.String get clickAction => $_getSZ(11);
  @$pb.TagNumber(12)
  set clickAction($core.String v) { $_setString(11, v); }
  @$pb.TagNumber(12)
  $core.bool hasClickAction() => $_has(11);
  @$pb.TagNumber(12)
  void clearClickAction() => clearField(12);

  @$pb.TagNumber(13)
  $fixnum.Int64 get ttlMs => $_getI64(12);
  @$pb.TagNumber(13)
  set ttlMs($fixnum.Int64 v) { $_setInt64(12, v); }
  @$pb.TagNumber(13)
  $core.bool hasTtlMs() => $_has(12);
  @$pb.TagNumber(13)
  void clearTtlMs() => clearField(13);

  @$pb.TagNumber(14)
  $core.bool get collapseKey => $_getBF(13);
  @$pb.TagNumber(14)
  set collapseKey($core.bool v) { $_setBool(13, v); }
  @$pb.TagNumber(14)
  $core.bool hasCollapseKey() => $_has(13);
  @$pb.TagNumber(14)
  void clearCollapseKey() => clearField(14);
}

/// Push notification response
class PushNotificationResponse extends $pb.GeneratedMessage {
  factory PushNotificationResponse({
    $0.ErrorCode? code,
    $core.String? notificationId,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (notificationId != null) {
      $result.notificationId = notificationId;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  PushNotificationResponse._() : super();
  factory PushNotificationResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PushNotificationResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PushNotificationResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'notificationId')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PushNotificationResponse clone() => PushNotificationResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PushNotificationResponse copyWith(void Function(PushNotificationResponse) updates) => super.copyWith((message) => updates(message as PushNotificationResponse)) as PushNotificationResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PushNotificationResponse create() => PushNotificationResponse._();
  PushNotificationResponse createEmptyInstance() => create();
  static $pb.PbList<PushNotificationResponse> createRepeated() => $pb.PbList<PushNotificationResponse>();
  @$core.pragma('dart2js:noInline')
  static PushNotificationResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PushNotificationResponse>(create);
  static PushNotificationResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get notificationId => $_getSZ(1);
  @$pb.TagNumber(2)
  set notificationId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasNotificationId() => $_has(1);
  @$pb.TagNumber(2)
  void clearNotificationId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Register device request
class RegisterDeviceRequest extends $pb.GeneratedMessage {
  factory RegisterDeviceRequest({
    $core.String? userId,
    $core.String? deviceId,
    $core.String? platform,
    $core.String? fcmToken,
    $core.String? apnsToken,
    $core.String? pushKitToken,
    $core.String? appVersion,
    $core.String? osVersion,
    $core.String? deviceName,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (deviceId != null) {
      $result.deviceId = deviceId;
    }
    if (platform != null) {
      $result.platform = platform;
    }
    if (fcmToken != null) {
      $result.fcmToken = fcmToken;
    }
    if (apnsToken != null) {
      $result.apnsToken = apnsToken;
    }
    if (pushKitToken != null) {
      $result.pushKitToken = pushKitToken;
    }
    if (appVersion != null) {
      $result.appVersion = appVersion;
    }
    if (osVersion != null) {
      $result.osVersion = osVersion;
    }
    if (deviceName != null) {
      $result.deviceName = deviceName;
    }
    return $result;
  }
  RegisterDeviceRequest._() : super();
  factory RegisterDeviceRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory RegisterDeviceRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'RegisterDeviceRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'deviceId')
    ..aOS(3, _omitFieldNames ? '' : 'platform')
    ..aOS(4, _omitFieldNames ? '' : 'fcmToken')
    ..aOS(5, _omitFieldNames ? '' : 'apnsToken')
    ..aOS(6, _omitFieldNames ? '' : 'pushKitToken')
    ..aOS(7, _omitFieldNames ? '' : 'appVersion')
    ..aOS(8, _omitFieldNames ? '' : 'osVersion')
    ..aOS(9, _omitFieldNames ? '' : 'deviceName')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  RegisterDeviceRequest clone() => RegisterDeviceRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  RegisterDeviceRequest copyWith(void Function(RegisterDeviceRequest) updates) => super.copyWith((message) => updates(message as RegisterDeviceRequest)) as RegisterDeviceRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RegisterDeviceRequest create() => RegisterDeviceRequest._();
  RegisterDeviceRequest createEmptyInstance() => create();
  static $pb.PbList<RegisterDeviceRequest> createRepeated() => $pb.PbList<RegisterDeviceRequest>();
  @$core.pragma('dart2js:noInline')
  static RegisterDeviceRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<RegisterDeviceRequest>(create);
  static RegisterDeviceRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get deviceId => $_getSZ(1);
  @$pb.TagNumber(2)
  set deviceId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasDeviceId() => $_has(1);
  @$pb.TagNumber(2)
  void clearDeviceId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get platform => $_getSZ(2);
  @$pb.TagNumber(3)
  set platform($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasPlatform() => $_has(2);
  @$pb.TagNumber(3)
  void clearPlatform() => clearField(3);

  /// FCM/APNs tokens
  @$pb.TagNumber(4)
  $core.String get fcmToken => $_getSZ(3);
  @$pb.TagNumber(4)
  set fcmToken($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasFcmToken() => $_has(3);
  @$pb.TagNumber(4)
  void clearFcmToken() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get apnsToken => $_getSZ(4);
  @$pb.TagNumber(5)
  set apnsToken($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasApnsToken() => $_has(4);
  @$pb.TagNumber(5)
  void clearApnsToken() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get pushKitToken => $_getSZ(5);
  @$pb.TagNumber(6)
  set pushKitToken($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasPushKitToken() => $_has(5);
  @$pb.TagNumber(6)
  void clearPushKitToken() => clearField(6);

  @$pb.TagNumber(7)
  $core.String get appVersion => $_getSZ(6);
  @$pb.TagNumber(7)
  set appVersion($core.String v) { $_setString(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasAppVersion() => $_has(6);
  @$pb.TagNumber(7)
  void clearAppVersion() => clearField(7);

  @$pb.TagNumber(8)
  $core.String get osVersion => $_getSZ(7);
  @$pb.TagNumber(8)
  set osVersion($core.String v) { $_setString(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasOsVersion() => $_has(7);
  @$pb.TagNumber(8)
  void clearOsVersion() => clearField(8);

  @$pb.TagNumber(9)
  $core.String get deviceName => $_getSZ(8);
  @$pb.TagNumber(9)
  set deviceName($core.String v) { $_setString(8, v); }
  @$pb.TagNumber(9)
  $core.bool hasDeviceName() => $_has(8);
  @$pb.TagNumber(9)
  void clearDeviceName() => clearField(9);
}

class RegisterDeviceResponse extends $pb.GeneratedMessage {
  factory RegisterDeviceResponse({
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
  RegisterDeviceResponse._() : super();
  factory RegisterDeviceResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory RegisterDeviceResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'RegisterDeviceResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  RegisterDeviceResponse clone() => RegisterDeviceResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  RegisterDeviceResponse copyWith(void Function(RegisterDeviceResponse) updates) => super.copyWith((message) => updates(message as RegisterDeviceResponse)) as RegisterDeviceResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RegisterDeviceResponse create() => RegisterDeviceResponse._();
  RegisterDeviceResponse createEmptyInstance() => create();
  static $pb.PbList<RegisterDeviceResponse> createRepeated() => $pb.PbList<RegisterDeviceResponse>();
  @$core.pragma('dart2js:noInline')
  static RegisterDeviceResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<RegisterDeviceResponse>(create);
  static RegisterDeviceResponse? _defaultInstance;

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

/// Unregister device request
class UnregisterDeviceRequest extends $pb.GeneratedMessage {
  factory UnregisterDeviceRequest({
    $core.String? userId,
    $core.String? deviceId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (deviceId != null) {
      $result.deviceId = deviceId;
    }
    return $result;
  }
  UnregisterDeviceRequest._() : super();
  factory UnregisterDeviceRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnregisterDeviceRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnregisterDeviceRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'deviceId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnregisterDeviceRequest clone() => UnregisterDeviceRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnregisterDeviceRequest copyWith(void Function(UnregisterDeviceRequest) updates) => super.copyWith((message) => updates(message as UnregisterDeviceRequest)) as UnregisterDeviceRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnregisterDeviceRequest create() => UnregisterDeviceRequest._();
  UnregisterDeviceRequest createEmptyInstance() => create();
  static $pb.PbList<UnregisterDeviceRequest> createRepeated() => $pb.PbList<UnregisterDeviceRequest>();
  @$core.pragma('dart2js:noInline')
  static UnregisterDeviceRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnregisterDeviceRequest>(create);
  static UnregisterDeviceRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get deviceId => $_getSZ(1);
  @$pb.TagNumber(2)
  set deviceId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasDeviceId() => $_has(1);
  @$pb.TagNumber(2)
  void clearDeviceId() => clearField(2);
}

class UnregisterDeviceResponse extends $pb.GeneratedMessage {
  factory UnregisterDeviceResponse({
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
  UnregisterDeviceResponse._() : super();
  factory UnregisterDeviceResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnregisterDeviceResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnregisterDeviceResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnregisterDeviceResponse clone() => UnregisterDeviceResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnregisterDeviceResponse copyWith(void Function(UnregisterDeviceResponse) updates) => super.copyWith((message) => updates(message as UnregisterDeviceResponse)) as UnregisterDeviceResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnregisterDeviceResponse create() => UnregisterDeviceResponse._();
  UnregisterDeviceResponse createEmptyInstance() => create();
  static $pb.PbList<UnregisterDeviceResponse> createRepeated() => $pb.PbList<UnregisterDeviceResponse>();
  @$core.pragma('dart2js:noInline')
  static UnregisterDeviceResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnregisterDeviceResponse>(create);
  static UnregisterDeviceResponse? _defaultInstance;

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

/// Update device token request
class UpdateDeviceTokenRequest extends $pb.GeneratedMessage {
  factory UpdateDeviceTokenRequest({
    $core.String? deviceId,
    $core.String? fcmToken,
    $core.String? apnsToken,
    $core.String? pushKitToken,
  }) {
    final $result = create();
    if (deviceId != null) {
      $result.deviceId = deviceId;
    }
    if (fcmToken != null) {
      $result.fcmToken = fcmToken;
    }
    if (apnsToken != null) {
      $result.apnsToken = apnsToken;
    }
    if (pushKitToken != null) {
      $result.pushKitToken = pushKitToken;
    }
    return $result;
  }
  UpdateDeviceTokenRequest._() : super();
  factory UpdateDeviceTokenRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UpdateDeviceTokenRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UpdateDeviceTokenRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'deviceId')
    ..aOS(2, _omitFieldNames ? '' : 'fcmToken')
    ..aOS(3, _omitFieldNames ? '' : 'apnsToken')
    ..aOS(4, _omitFieldNames ? '' : 'pushKitToken')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UpdateDeviceTokenRequest clone() => UpdateDeviceTokenRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UpdateDeviceTokenRequest copyWith(void Function(UpdateDeviceTokenRequest) updates) => super.copyWith((message) => updates(message as UpdateDeviceTokenRequest)) as UpdateDeviceTokenRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UpdateDeviceTokenRequest create() => UpdateDeviceTokenRequest._();
  UpdateDeviceTokenRequest createEmptyInstance() => create();
  static $pb.PbList<UpdateDeviceTokenRequest> createRepeated() => $pb.PbList<UpdateDeviceTokenRequest>();
  @$core.pragma('dart2js:noInline')
  static UpdateDeviceTokenRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UpdateDeviceTokenRequest>(create);
  static UpdateDeviceTokenRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get deviceId => $_getSZ(0);
  @$pb.TagNumber(1)
  set deviceId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasDeviceId() => $_has(0);
  @$pb.TagNumber(1)
  void clearDeviceId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get fcmToken => $_getSZ(1);
  @$pb.TagNumber(2)
  set fcmToken($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFcmToken() => $_has(1);
  @$pb.TagNumber(2)
  void clearFcmToken() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get apnsToken => $_getSZ(2);
  @$pb.TagNumber(3)
  set apnsToken($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasApnsToken() => $_has(2);
  @$pb.TagNumber(3)
  void clearApnsToken() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get pushKitToken => $_getSZ(3);
  @$pb.TagNumber(4)
  set pushKitToken($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasPushKitToken() => $_has(3);
  @$pb.TagNumber(4)
  void clearPushKitToken() => clearField(4);
}

class UpdateDeviceTokenResponse extends $pb.GeneratedMessage {
  factory UpdateDeviceTokenResponse({
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
  UpdateDeviceTokenResponse._() : super();
  factory UpdateDeviceTokenResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UpdateDeviceTokenResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UpdateDeviceTokenResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UpdateDeviceTokenResponse clone() => UpdateDeviceTokenResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UpdateDeviceTokenResponse copyWith(void Function(UpdateDeviceTokenResponse) updates) => super.copyWith((message) => updates(message as UpdateDeviceTokenResponse)) as UpdateDeviceTokenResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UpdateDeviceTokenResponse create() => UpdateDeviceTokenResponse._();
  UpdateDeviceTokenResponse createEmptyInstance() => create();
  static $pb.PbList<UpdateDeviceTokenResponse> createRepeated() => $pb.PbList<UpdateDeviceTokenResponse>();
  @$core.pragma('dart2js:noInline')
  static UpdateDeviceTokenResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UpdateDeviceTokenResponse>(create);
  static UpdateDeviceTokenResponse? _defaultInstance;

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

/// Get user devices request
class GetUserDevicesRequest extends $pb.GeneratedMessage {
  factory GetUserDevicesRequest({
    $core.String? userId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetUserDevicesRequest._() : super();
  factory GetUserDevicesRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUserDevicesRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUserDevicesRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUserDevicesRequest clone() => GetUserDevicesRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUserDevicesRequest copyWith(void Function(GetUserDevicesRequest) updates) => super.copyWith((message) => updates(message as GetUserDevicesRequest)) as GetUserDevicesRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUserDevicesRequest create() => GetUserDevicesRequest._();
  GetUserDevicesRequest createEmptyInstance() => create();
  static $pb.PbList<GetUserDevicesRequest> createRepeated() => $pb.PbList<GetUserDevicesRequest>();
  @$core.pragma('dart2js:noInline')
  static GetUserDevicesRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUserDevicesRequest>(create);
  static GetUserDevicesRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);
}

class GetUserDevicesResponse extends $pb.GeneratedMessage {
  factory GetUserDevicesResponse({
    $0.ErrorCode? code,
    $core.Iterable<DeviceInfo>? devices,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (devices != null) {
      $result.devices.addAll(devices);
    }
    return $result;
  }
  GetUserDevicesResponse._() : super();
  factory GetUserDevicesResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUserDevicesResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUserDevicesResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<DeviceInfo>(2, _omitFieldNames ? '' : 'devices', $pb.PbFieldType.PM, subBuilder: DeviceInfo.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUserDevicesResponse clone() => GetUserDevicesResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUserDevicesResponse copyWith(void Function(GetUserDevicesResponse) updates) => super.copyWith((message) => updates(message as GetUserDevicesResponse)) as GetUserDevicesResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUserDevicesResponse create() => GetUserDevicesResponse._();
  GetUserDevicesResponse createEmptyInstance() => create();
  static $pb.PbList<GetUserDevicesResponse> createRepeated() => $pb.PbList<GetUserDevicesResponse>();
  @$core.pragma('dart2js:noInline')
  static GetUserDevicesResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUserDevicesResponse>(create);
  static GetUserDevicesResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<DeviceInfo> get devices => $_getList(1);
}

/// Device info
class DeviceInfo extends $pb.GeneratedMessage {
  factory DeviceInfo({
    $core.String? deviceId,
    $core.String? userId,
    $core.String? platform,
    $core.String? appVersion,
    $core.String? osVersion,
    $core.String? deviceName,
    $fixnum.Int64? registeredAt,
    $core.bool? isActive,
  }) {
    final $result = create();
    if (deviceId != null) {
      $result.deviceId = deviceId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (platform != null) {
      $result.platform = platform;
    }
    if (appVersion != null) {
      $result.appVersion = appVersion;
    }
    if (osVersion != null) {
      $result.osVersion = osVersion;
    }
    if (deviceName != null) {
      $result.deviceName = deviceName;
    }
    if (registeredAt != null) {
      $result.registeredAt = registeredAt;
    }
    if (isActive != null) {
      $result.isActive = isActive;
    }
    return $result;
  }
  DeviceInfo._() : super();
  factory DeviceInfo.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeviceInfo.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeviceInfo', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'deviceId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aOS(3, _omitFieldNames ? '' : 'platform')
    ..aOS(4, _omitFieldNames ? '' : 'appVersion')
    ..aOS(5, _omitFieldNames ? '' : 'osVersion')
    ..aOS(6, _omitFieldNames ? '' : 'deviceName')
    ..aInt64(7, _omitFieldNames ? '' : 'registeredAt')
    ..aOB(8, _omitFieldNames ? '' : 'isActive')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeviceInfo clone() => DeviceInfo()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeviceInfo copyWith(void Function(DeviceInfo) updates) => super.copyWith((message) => updates(message as DeviceInfo)) as DeviceInfo;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeviceInfo create() => DeviceInfo._();
  DeviceInfo createEmptyInstance() => create();
  static $pb.PbList<DeviceInfo> createRepeated() => $pb.PbList<DeviceInfo>();
  @$core.pragma('dart2js:noInline')
  static DeviceInfo getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeviceInfo>(create);
  static DeviceInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get deviceId => $_getSZ(0);
  @$pb.TagNumber(1)
  set deviceId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasDeviceId() => $_has(0);
  @$pb.TagNumber(1)
  void clearDeviceId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get platform => $_getSZ(2);
  @$pb.TagNumber(3)
  set platform($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasPlatform() => $_has(2);
  @$pb.TagNumber(3)
  void clearPlatform() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get appVersion => $_getSZ(3);
  @$pb.TagNumber(4)
  set appVersion($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasAppVersion() => $_has(3);
  @$pb.TagNumber(4)
  void clearAppVersion() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get osVersion => $_getSZ(4);
  @$pb.TagNumber(5)
  set osVersion($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasOsVersion() => $_has(4);
  @$pb.TagNumber(5)
  void clearOsVersion() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get deviceName => $_getSZ(5);
  @$pb.TagNumber(6)
  set deviceName($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasDeviceName() => $_has(5);
  @$pb.TagNumber(6)
  void clearDeviceName() => clearField(6);

  @$pb.TagNumber(7)
  $fixnum.Int64 get registeredAt => $_getI64(6);
  @$pb.TagNumber(7)
  set registeredAt($fixnum.Int64 v) { $_setInt64(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasRegisteredAt() => $_has(6);
  @$pb.TagNumber(7)
  void clearRegisteredAt() => clearField(7);

  @$pb.TagNumber(8)
  $core.bool get isActive => $_getBF(7);
  @$pb.TagNumber(8)
  set isActive($core.bool v) { $_setBool(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasIsActive() => $_has(7);
  @$pb.TagNumber(8)
  void clearIsActive() => clearField(8);
}

/// Set badge count request
class SetBadgeCountRequest extends $pb.GeneratedMessage {
  factory SetBadgeCountRequest({
    $core.String? userId,
    $core.int? count,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (count != null) {
      $result.count = count;
    }
    return $result;
  }
  SetBadgeCountRequest._() : super();
  factory SetBadgeCountRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetBadgeCountRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetBadgeCountRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..a<$core.int>(2, _omitFieldNames ? '' : 'count', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetBadgeCountRequest clone() => SetBadgeCountRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetBadgeCountRequest copyWith(void Function(SetBadgeCountRequest) updates) => super.copyWith((message) => updates(message as SetBadgeCountRequest)) as SetBadgeCountRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetBadgeCountRequest create() => SetBadgeCountRequest._();
  SetBadgeCountRequest createEmptyInstance() => create();
  static $pb.PbList<SetBadgeCountRequest> createRepeated() => $pb.PbList<SetBadgeCountRequest>();
  @$core.pragma('dart2js:noInline')
  static SetBadgeCountRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetBadgeCountRequest>(create);
  static SetBadgeCountRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.int get count => $_getIZ(1);
  @$pb.TagNumber(2)
  set count($core.int v) { $_setSignedInt32(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasCount() => $_has(1);
  @$pb.TagNumber(2)
  void clearCount() => clearField(2);
}

class SetBadgeCountResponse extends $pb.GeneratedMessage {
  factory SetBadgeCountResponse({
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
  SetBadgeCountResponse._() : super();
  factory SetBadgeCountResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetBadgeCountResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetBadgeCountResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetBadgeCountResponse clone() => SetBadgeCountResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetBadgeCountResponse copyWith(void Function(SetBadgeCountResponse) updates) => super.copyWith((message) => updates(message as SetBadgeCountResponse)) as SetBadgeCountResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetBadgeCountResponse create() => SetBadgeCountResponse._();
  SetBadgeCountResponse createEmptyInstance() => create();
  static $pb.PbList<SetBadgeCountResponse> createRepeated() => $pb.PbList<SetBadgeCountResponse>();
  @$core.pragma('dart2js:noInline')
  static SetBadgeCountResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetBadgeCountResponse>(create);
  static SetBadgeCountResponse? _defaultInstance;

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

/// Silent notification (data only)
class SilentNotificationRequest extends $pb.GeneratedMessage {
  factory SilentNotificationRequest({
    $core.String? userId,
    $core.Map<$core.String, $core.String>? data,
    $fixnum.Int64? ttlMs,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (data != null) {
      $result.data.addAll(data);
    }
    if (ttlMs != null) {
      $result.ttlMs = ttlMs;
    }
    return $result;
  }
  SilentNotificationRequest._() : super();
  factory SilentNotificationRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SilentNotificationRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SilentNotificationRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..m<$core.String, $core.String>(2, _omitFieldNames ? '' : 'data', entryClassName: 'SilentNotificationRequest.DataEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.app_notification'))
    ..aInt64(3, _omitFieldNames ? '' : 'ttlMs')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SilentNotificationRequest clone() => SilentNotificationRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SilentNotificationRequest copyWith(void Function(SilentNotificationRequest) updates) => super.copyWith((message) => updates(message as SilentNotificationRequest)) as SilentNotificationRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SilentNotificationRequest create() => SilentNotificationRequest._();
  SilentNotificationRequest createEmptyInstance() => create();
  static $pb.PbList<SilentNotificationRequest> createRepeated() => $pb.PbList<SilentNotificationRequest>();
  @$core.pragma('dart2js:noInline')
  static SilentNotificationRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SilentNotificationRequest>(create);
  static SilentNotificationRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.Map<$core.String, $core.String> get data => $_getMap(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get ttlMs => $_getI64(2);
  @$pb.TagNumber(3)
  set ttlMs($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTtlMs() => $_has(2);
  @$pb.TagNumber(3)
  void clearTtlMs() => clearField(3);
}

/// Notification preferences per user
class NotificationPreferences extends $pb.GeneratedMessage {
  factory NotificationPreferences({
    $core.bool? enabled,
    $core.bool? soundEnabled,
    $core.bool? vibrationEnabled,
    $core.bool? showPreview,
    $core.bool? allowMessages,
    $core.bool? allowMentions,
    $core.bool? allowFriendRequests,
    $core.bool? allowVoiceCalls,
    $core.bool? dndEnabled,
    $core.int? dndStartHour,
    $core.int? dndEndHour,
    $core.Iterable<$core.int>? dndDays,
    $core.Map<$core.String, ChannelNotificationSettings>? channelSettings,
  }) {
    final $result = create();
    if (enabled != null) {
      $result.enabled = enabled;
    }
    if (soundEnabled != null) {
      $result.soundEnabled = soundEnabled;
    }
    if (vibrationEnabled != null) {
      $result.vibrationEnabled = vibrationEnabled;
    }
    if (showPreview != null) {
      $result.showPreview = showPreview;
    }
    if (allowMessages != null) {
      $result.allowMessages = allowMessages;
    }
    if (allowMentions != null) {
      $result.allowMentions = allowMentions;
    }
    if (allowFriendRequests != null) {
      $result.allowFriendRequests = allowFriendRequests;
    }
    if (allowVoiceCalls != null) {
      $result.allowVoiceCalls = allowVoiceCalls;
    }
    if (dndEnabled != null) {
      $result.dndEnabled = dndEnabled;
    }
    if (dndStartHour != null) {
      $result.dndStartHour = dndStartHour;
    }
    if (dndEndHour != null) {
      $result.dndEndHour = dndEndHour;
    }
    if (dndDays != null) {
      $result.dndDays.addAll(dndDays);
    }
    if (channelSettings != null) {
      $result.channelSettings.addAll(channelSettings);
    }
    return $result;
  }
  NotificationPreferences._() : super();
  factory NotificationPreferences.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory NotificationPreferences.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'NotificationPreferences', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOB(1, _omitFieldNames ? '' : 'enabled')
    ..aOB(2, _omitFieldNames ? '' : 'soundEnabled')
    ..aOB(3, _omitFieldNames ? '' : 'vibrationEnabled')
    ..aOB(4, _omitFieldNames ? '' : 'showPreview')
    ..aOB(5, _omitFieldNames ? '' : 'allowMessages')
    ..aOB(6, _omitFieldNames ? '' : 'allowMentions')
    ..aOB(7, _omitFieldNames ? '' : 'allowFriendRequests')
    ..aOB(8, _omitFieldNames ? '' : 'allowVoiceCalls')
    ..aOB(9, _omitFieldNames ? '' : 'dndEnabled')
    ..a<$core.int>(10, _omitFieldNames ? '' : 'dndStartHour', $pb.PbFieldType.O3)
    ..a<$core.int>(11, _omitFieldNames ? '' : 'dndEndHour', $pb.PbFieldType.O3)
    ..p<$core.int>(12, _omitFieldNames ? '' : 'dndDays', $pb.PbFieldType.K3)
    ..m<$core.String, ChannelNotificationSettings>(13, _omitFieldNames ? '' : 'channelSettings', entryClassName: 'NotificationPreferences.ChannelSettingsEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OM, valueCreator: ChannelNotificationSettings.create, valueDefaultOrMaker: ChannelNotificationSettings.getDefault, packageName: const $pb.PackageName('chirp.app_notification'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  NotificationPreferences clone() => NotificationPreferences()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  NotificationPreferences copyWith(void Function(NotificationPreferences) updates) => super.copyWith((message) => updates(message as NotificationPreferences)) as NotificationPreferences;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static NotificationPreferences create() => NotificationPreferences._();
  NotificationPreferences createEmptyInstance() => create();
  static $pb.PbList<NotificationPreferences> createRepeated() => $pb.PbList<NotificationPreferences>();
  @$core.pragma('dart2js:noInline')
  static NotificationPreferences getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<NotificationPreferences>(create);
  static NotificationPreferences? _defaultInstance;

  @$pb.TagNumber(1)
  $core.bool get enabled => $_getBF(0);
  @$pb.TagNumber(1)
  set enabled($core.bool v) { $_setBool(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasEnabled() => $_has(0);
  @$pb.TagNumber(1)
  void clearEnabled() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get soundEnabled => $_getBF(1);
  @$pb.TagNumber(2)
  set soundEnabled($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSoundEnabled() => $_has(1);
  @$pb.TagNumber(2)
  void clearSoundEnabled() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get vibrationEnabled => $_getBF(2);
  @$pb.TagNumber(3)
  set vibrationEnabled($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasVibrationEnabled() => $_has(2);
  @$pb.TagNumber(3)
  void clearVibrationEnabled() => clearField(3);

  @$pb.TagNumber(4)
  $core.bool get showPreview => $_getBF(3);
  @$pb.TagNumber(4)
  set showPreview($core.bool v) { $_setBool(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasShowPreview() => $_has(3);
  @$pb.TagNumber(4)
  void clearShowPreview() => clearField(4);

  @$pb.TagNumber(5)
  $core.bool get allowMessages => $_getBF(4);
  @$pb.TagNumber(5)
  set allowMessages($core.bool v) { $_setBool(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasAllowMessages() => $_has(4);
  @$pb.TagNumber(5)
  void clearAllowMessages() => clearField(5);

  @$pb.TagNumber(6)
  $core.bool get allowMentions => $_getBF(5);
  @$pb.TagNumber(6)
  set allowMentions($core.bool v) { $_setBool(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasAllowMentions() => $_has(5);
  @$pb.TagNumber(6)
  void clearAllowMentions() => clearField(6);

  @$pb.TagNumber(7)
  $core.bool get allowFriendRequests => $_getBF(6);
  @$pb.TagNumber(7)
  set allowFriendRequests($core.bool v) { $_setBool(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasAllowFriendRequests() => $_has(6);
  @$pb.TagNumber(7)
  void clearAllowFriendRequests() => clearField(7);

  @$pb.TagNumber(8)
  $core.bool get allowVoiceCalls => $_getBF(7);
  @$pb.TagNumber(8)
  set allowVoiceCalls($core.bool v) { $_setBool(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasAllowVoiceCalls() => $_has(7);
  @$pb.TagNumber(8)
  void clearAllowVoiceCalls() => clearField(8);

  /// Do Not Disturb
  @$pb.TagNumber(9)
  $core.bool get dndEnabled => $_getBF(8);
  @$pb.TagNumber(9)
  set dndEnabled($core.bool v) { $_setBool(8, v); }
  @$pb.TagNumber(9)
  $core.bool hasDndEnabled() => $_has(8);
  @$pb.TagNumber(9)
  void clearDndEnabled() => clearField(9);

  @$pb.TagNumber(10)
  $core.int get dndStartHour => $_getIZ(9);
  @$pb.TagNumber(10)
  set dndStartHour($core.int v) { $_setSignedInt32(9, v); }
  @$pb.TagNumber(10)
  $core.bool hasDndStartHour() => $_has(9);
  @$pb.TagNumber(10)
  void clearDndStartHour() => clearField(10);

  @$pb.TagNumber(11)
  $core.int get dndEndHour => $_getIZ(10);
  @$pb.TagNumber(11)
  set dndEndHour($core.int v) { $_setSignedInt32(10, v); }
  @$pb.TagNumber(11)
  $core.bool hasDndEndHour() => $_has(10);
  @$pb.TagNumber(11)
  void clearDndEndHour() => clearField(11);

  @$pb.TagNumber(12)
  $core.List<$core.int> get dndDays => $_getList(11);

  /// Per-channel settings
  @$pb.TagNumber(13)
  $core.Map<$core.String, ChannelNotificationSettings> get channelSettings => $_getMap(12);
}

/// Channel notification settings
class ChannelNotificationSettings extends $pb.GeneratedMessage {
  factory ChannelNotificationSettings({
    $core.bool? muted,
    $core.bool? onlyMentions,
    $core.bool? notifyOnMentions,
  }) {
    final $result = create();
    if (muted != null) {
      $result.muted = muted;
    }
    if (onlyMentions != null) {
      $result.onlyMentions = onlyMentions;
    }
    if (notifyOnMentions != null) {
      $result.notifyOnMentions = notifyOnMentions;
    }
    return $result;
  }
  ChannelNotificationSettings._() : super();
  factory ChannelNotificationSettings.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelNotificationSettings.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelNotificationSettings', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOB(1, _omitFieldNames ? '' : 'muted')
    ..aOB(2, _omitFieldNames ? '' : 'onlyMentions')
    ..aOB(3, _omitFieldNames ? '' : 'notifyOnMentions')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelNotificationSettings clone() => ChannelNotificationSettings()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelNotificationSettings copyWith(void Function(ChannelNotificationSettings) updates) => super.copyWith((message) => updates(message as ChannelNotificationSettings)) as ChannelNotificationSettings;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelNotificationSettings create() => ChannelNotificationSettings._();
  ChannelNotificationSettings createEmptyInstance() => create();
  static $pb.PbList<ChannelNotificationSettings> createRepeated() => $pb.PbList<ChannelNotificationSettings>();
  @$core.pragma('dart2js:noInline')
  static ChannelNotificationSettings getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelNotificationSettings>(create);
  static ChannelNotificationSettings? _defaultInstance;

  @$pb.TagNumber(1)
  $core.bool get muted => $_getBF(0);
  @$pb.TagNumber(1)
  set muted($core.bool v) { $_setBool(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMuted() => $_has(0);
  @$pb.TagNumber(1)
  void clearMuted() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get onlyMentions => $_getBF(1);
  @$pb.TagNumber(2)
  set onlyMentions($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasOnlyMentions() => $_has(1);
  @$pb.TagNumber(2)
  void clearOnlyMentions() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get notifyOnMentions => $_getBF(2);
  @$pb.TagNumber(3)
  set notifyOnMentions($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasNotifyOnMentions() => $_has(2);
  @$pb.TagNumber(3)
  void clearNotifyOnMentions() => clearField(3);
}

/// Set notification preferences request
class SetPreferencesRequest extends $pb.GeneratedMessage {
  factory SetPreferencesRequest({
    $core.String? userId,
    NotificationPreferences? preferences,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (preferences != null) {
      $result.preferences = preferences;
    }
    return $result;
  }
  SetPreferencesRequest._() : super();
  factory SetPreferencesRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetPreferencesRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetPreferencesRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOM<NotificationPreferences>(2, _omitFieldNames ? '' : 'preferences', subBuilder: NotificationPreferences.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetPreferencesRequest clone() => SetPreferencesRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetPreferencesRequest copyWith(void Function(SetPreferencesRequest) updates) => super.copyWith((message) => updates(message as SetPreferencesRequest)) as SetPreferencesRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetPreferencesRequest create() => SetPreferencesRequest._();
  SetPreferencesRequest createEmptyInstance() => create();
  static $pb.PbList<SetPreferencesRequest> createRepeated() => $pb.PbList<SetPreferencesRequest>();
  @$core.pragma('dart2js:noInline')
  static SetPreferencesRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetPreferencesRequest>(create);
  static SetPreferencesRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  NotificationPreferences get preferences => $_getN(1);
  @$pb.TagNumber(2)
  set preferences(NotificationPreferences v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasPreferences() => $_has(1);
  @$pb.TagNumber(2)
  void clearPreferences() => clearField(2);
  @$pb.TagNumber(2)
  NotificationPreferences ensurePreferences() => $_ensure(1);
}

class SetPreferencesResponse extends $pb.GeneratedMessage {
  factory SetPreferencesResponse({
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
  SetPreferencesResponse._() : super();
  factory SetPreferencesResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetPreferencesResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetPreferencesResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetPreferencesResponse clone() => SetPreferencesResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetPreferencesResponse copyWith(void Function(SetPreferencesResponse) updates) => super.copyWith((message) => updates(message as SetPreferencesResponse)) as SetPreferencesResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetPreferencesResponse create() => SetPreferencesResponse._();
  SetPreferencesResponse createEmptyInstance() => create();
  static $pb.PbList<SetPreferencesResponse> createRepeated() => $pb.PbList<SetPreferencesResponse>();
  @$core.pragma('dart2js:noInline')
  static SetPreferencesResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetPreferencesResponse>(create);
  static SetPreferencesResponse? _defaultInstance;

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

/// Get preferences request
class GetPreferencesRequest extends $pb.GeneratedMessage {
  factory GetPreferencesRequest({
    $core.String? userId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetPreferencesRequest._() : super();
  factory GetPreferencesRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPreferencesRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPreferencesRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPreferencesRequest clone() => GetPreferencesRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPreferencesRequest copyWith(void Function(GetPreferencesRequest) updates) => super.copyWith((message) => updates(message as GetPreferencesRequest)) as GetPreferencesRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPreferencesRequest create() => GetPreferencesRequest._();
  GetPreferencesRequest createEmptyInstance() => create();
  static $pb.PbList<GetPreferencesRequest> createRepeated() => $pb.PbList<GetPreferencesRequest>();
  @$core.pragma('dart2js:noInline')
  static GetPreferencesRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPreferencesRequest>(create);
  static GetPreferencesRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);
}

class GetPreferencesResponse extends $pb.GeneratedMessage {
  factory GetPreferencesResponse({
    $0.ErrorCode? code,
    NotificationPreferences? preferences,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (preferences != null) {
      $result.preferences = preferences;
    }
    return $result;
  }
  GetPreferencesResponse._() : super();
  factory GetPreferencesResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetPreferencesResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetPreferencesResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.app_notification'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<NotificationPreferences>(2, _omitFieldNames ? '' : 'preferences', subBuilder: NotificationPreferences.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetPreferencesResponse clone() => GetPreferencesResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetPreferencesResponse copyWith(void Function(GetPreferencesResponse) updates) => super.copyWith((message) => updates(message as GetPreferencesResponse)) as GetPreferencesResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetPreferencesResponse create() => GetPreferencesResponse._();
  GetPreferencesResponse createEmptyInstance() => create();
  static $pb.PbList<GetPreferencesResponse> createRepeated() => $pb.PbList<GetPreferencesResponse>();
  @$core.pragma('dart2js:noInline')
  static GetPreferencesResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetPreferencesResponse>(create);
  static GetPreferencesResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  NotificationPreferences get preferences => $_getN(1);
  @$pb.TagNumber(2)
  set preferences(NotificationPreferences v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasPreferences() => $_has(1);
  @$pb.TagNumber(2)
  void clearPreferences() => clearField(2);
  @$pb.TagNumber(2)
  NotificationPreferences ensurePreferences() => $_ensure(1);
}


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
