//
//  Generated code. Do not modify.
//  source: proto/party.proto
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

/// A party member as seen on the wire. `ready` is the ready-check flag
/// (SET_READY); always false right after a join.
class PartyMember extends $pb.GeneratedMessage {
  factory PartyMember({
    $core.String? userId,
    $core.bool? ready,
    $fixnum.Int64? joinedAt,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (ready != null) {
      $result.ready = ready;
    }
    if (joinedAt != null) {
      $result.joinedAt = joinedAt;
    }
    return $result;
  }
  PartyMember._() : super();
  factory PartyMember.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PartyMember.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PartyMember', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOB(2, _omitFieldNames ? '' : 'ready')
    ..aInt64(3, _omitFieldNames ? '' : 'joinedAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PartyMember clone() => PartyMember()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PartyMember copyWith(void Function(PartyMember) updates) => super.copyWith((message) => updates(message as PartyMember)) as PartyMember;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PartyMember create() => PartyMember._();
  PartyMember createEmptyInstance() => create();
  static $pb.PbList<PartyMember> createRepeated() => $pb.PbList<PartyMember>();
  @$core.pragma('dart2js:noInline')
  static PartyMember getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PartyMember>(create);
  static PartyMember? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get ready => $_getBF(1);
  @$pb.TagNumber(2)
  set ready($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasReady() => $_has(1);
  @$pb.TagNumber(2)
  void clearReady() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get joinedAt => $_getI64(2);
  @$pb.TagNumber(3)
  set joinedAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasJoinedAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearJoinedAt() => clearField(3);
}

/// Full party snapshot; PARTY_STATE_CHANGED carries it whole.
class PartyInfo extends $pb.GeneratedMessage {
  factory PartyInfo({
    $core.String? partyId,
    $core.String? leaderId,
    $core.int? maxMembers,
    $core.Iterable<PartyMember>? members,
    $fixnum.Int64? createdAt,
  }) {
    final $result = create();
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (leaderId != null) {
      $result.leaderId = leaderId;
    }
    if (maxMembers != null) {
      $result.maxMembers = maxMembers;
    }
    if (members != null) {
      $result.members.addAll(members);
    }
    if (createdAt != null) {
      $result.createdAt = createdAt;
    }
    return $result;
  }
  PartyInfo._() : super();
  factory PartyInfo.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PartyInfo.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PartyInfo', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'partyId')
    ..aOS(2, _omitFieldNames ? '' : 'leaderId')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'maxMembers', $pb.PbFieldType.O3)
    ..pc<PartyMember>(4, _omitFieldNames ? '' : 'members', $pb.PbFieldType.PM, subBuilder: PartyMember.create)
    ..aInt64(5, _omitFieldNames ? '' : 'createdAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PartyInfo clone() => PartyInfo()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PartyInfo copyWith(void Function(PartyInfo) updates) => super.copyWith((message) => updates(message as PartyInfo)) as PartyInfo;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PartyInfo create() => PartyInfo._();
  PartyInfo createEmptyInstance() => create();
  static $pb.PbList<PartyInfo> createRepeated() => $pb.PbList<PartyInfo>();
  @$core.pragma('dart2js:noInline')
  static PartyInfo getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PartyInfo>(create);
  static PartyInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get partyId => $_getSZ(0);
  @$pb.TagNumber(1)
  set partyId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPartyId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPartyId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get leaderId => $_getSZ(1);
  @$pb.TagNumber(2)
  set leaderId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasLeaderId() => $_has(1);
  @$pb.TagNumber(2)
  void clearLeaderId() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get maxMembers => $_getIZ(2);
  @$pb.TagNumber(3)
  set maxMembers($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMaxMembers() => $_has(2);
  @$pb.TagNumber(3)
  void clearMaxMembers() => clearField(3);

  @$pb.TagNumber(4)
  $core.List<PartyMember> get members => $_getList(3);

  @$pb.TagNumber(5)
  $fixnum.Int64 get createdAt => $_getI64(4);
  @$pb.TagNumber(5)
  set createdAt($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasCreatedAt() => $_has(4);
  @$pb.TagNumber(5)
  void clearCreatedAt() => clearField(5);
}

/// Create a party; the creator is its only member and leader.
class CreatePartyRequest extends $pb.GeneratedMessage {
  factory CreatePartyRequest({
    $core.String? userId,
    $core.int? maxMembers,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (maxMembers != null) {
      $result.maxMembers = maxMembers;
    }
    return $result;
  }
  CreatePartyRequest._() : super();
  factory CreatePartyRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreatePartyRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreatePartyRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..a<$core.int>(2, _omitFieldNames ? '' : 'maxMembers', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreatePartyRequest clone() => CreatePartyRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreatePartyRequest copyWith(void Function(CreatePartyRequest) updates) => super.copyWith((message) => updates(message as CreatePartyRequest)) as CreatePartyRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreatePartyRequest create() => CreatePartyRequest._();
  CreatePartyRequest createEmptyInstance() => create();
  static $pb.PbList<CreatePartyRequest> createRepeated() => $pb.PbList<CreatePartyRequest>();
  @$core.pragma('dart2js:noInline')
  static CreatePartyRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreatePartyRequest>(create);
  static CreatePartyRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.int get maxMembers => $_getIZ(1);
  @$pb.TagNumber(2)
  set maxMembers($core.int v) { $_setSignedInt32(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasMaxMembers() => $_has(1);
  @$pb.TagNumber(2)
  void clearMaxMembers() => clearField(2);
}

class CreatePartyResponse extends $pb.GeneratedMessage {
  factory CreatePartyResponse({
    $0.ErrorCode? code,
    PartyInfo? party,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (party != null) {
      $result.party = party;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  CreatePartyResponse._() : super();
  factory CreatePartyResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreatePartyResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreatePartyResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<PartyInfo>(2, _omitFieldNames ? '' : 'party', subBuilder: PartyInfo.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreatePartyResponse clone() => CreatePartyResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreatePartyResponse copyWith(void Function(CreatePartyResponse) updates) => super.copyWith((message) => updates(message as CreatePartyResponse)) as CreatePartyResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreatePartyResponse create() => CreatePartyResponse._();
  CreatePartyResponse createEmptyInstance() => create();
  static $pb.PbList<CreatePartyResponse> createRepeated() => $pb.PbList<CreatePartyResponse>();
  @$core.pragma('dart2js:noInline')
  static CreatePartyResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreatePartyResponse>(create);
  static CreatePartyResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  PartyInfo get party => $_getN(1);
  @$pb.TagNumber(2)
  set party(PartyInfo v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasParty() => $_has(1);
  @$pb.TagNumber(2)
  void clearParty() => clearField(2);
  @$pb.TagNumber(2)
  PartyInfo ensureParty() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Disband the whole party (leader-only); remaining members get
/// PARTY_DISBANDED_NOTIFY.
class DisbandPartyRequest extends $pb.GeneratedMessage {
  factory DisbandPartyRequest({
    $core.String? userId,
    $core.String? partyId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (partyId != null) {
      $result.partyId = partyId;
    }
    return $result;
  }
  DisbandPartyRequest._() : super();
  factory DisbandPartyRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DisbandPartyRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DisbandPartyRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'partyId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DisbandPartyRequest clone() => DisbandPartyRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DisbandPartyRequest copyWith(void Function(DisbandPartyRequest) updates) => super.copyWith((message) => updates(message as DisbandPartyRequest)) as DisbandPartyRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DisbandPartyRequest create() => DisbandPartyRequest._();
  DisbandPartyRequest createEmptyInstance() => create();
  static $pb.PbList<DisbandPartyRequest> createRepeated() => $pb.PbList<DisbandPartyRequest>();
  @$core.pragma('dart2js:noInline')
  static DisbandPartyRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DisbandPartyRequest>(create);
  static DisbandPartyRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get partyId => $_getSZ(1);
  @$pb.TagNumber(2)
  set partyId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPartyId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPartyId() => clearField(2);
}

class DisbandPartyResponse extends $pb.GeneratedMessage {
  factory DisbandPartyResponse({
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
  DisbandPartyResponse._() : super();
  factory DisbandPartyResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DisbandPartyResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DisbandPartyResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DisbandPartyResponse clone() => DisbandPartyResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DisbandPartyResponse copyWith(void Function(DisbandPartyResponse) updates) => super.copyWith((message) => updates(message as DisbandPartyResponse)) as DisbandPartyResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DisbandPartyResponse create() => DisbandPartyResponse._();
  DisbandPartyResponse createEmptyInstance() => create();
  static $pb.PbList<DisbandPartyResponse> createRepeated() => $pb.PbList<DisbandPartyResponse>();
  @$core.pragma('dart2js:noInline')
  static DisbandPartyResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DisbandPartyResponse>(create);
  static DisbandPartyResponse? _defaultInstance;

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

/// Invite someone (any member may invite). Idempotent: re-inviting the same
/// target while an unexpired invite is open returns the original invite_id
/// and re-pings the invitee.
class InviteToPartyRequest extends $pb.GeneratedMessage {
  factory InviteToPartyRequest({
    $core.String? userId,
    $core.String? partyId,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  InviteToPartyRequest._() : super();
  factory InviteToPartyRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory InviteToPartyRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InviteToPartyRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'partyId')
    ..aOS(3, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  InviteToPartyRequest clone() => InviteToPartyRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  InviteToPartyRequest copyWith(void Function(InviteToPartyRequest) updates) => super.copyWith((message) => updates(message as InviteToPartyRequest)) as InviteToPartyRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InviteToPartyRequest create() => InviteToPartyRequest._();
  InviteToPartyRequest createEmptyInstance() => create();
  static $pb.PbList<InviteToPartyRequest> createRepeated() => $pb.PbList<InviteToPartyRequest>();
  @$core.pragma('dart2js:noInline')
  static InviteToPartyRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<InviteToPartyRequest>(create);
  static InviteToPartyRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get partyId => $_getSZ(1);
  @$pb.TagNumber(2)
  set partyId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPartyId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPartyId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get targetUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set targetUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTargetUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearTargetUserId() => clearField(3);
}

class InviteToPartyResponse extends $pb.GeneratedMessage {
  factory InviteToPartyResponse({
    $0.ErrorCode? code,
    $core.String? inviteId,
    $fixnum.Int64? expiresAt,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (inviteId != null) {
      $result.inviteId = inviteId;
    }
    if (expiresAt != null) {
      $result.expiresAt = expiresAt;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  InviteToPartyResponse._() : super();
  factory InviteToPartyResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory InviteToPartyResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InviteToPartyResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'inviteId')
    ..aInt64(3, _omitFieldNames ? '' : 'expiresAt')
    ..aInt64(4, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  InviteToPartyResponse clone() => InviteToPartyResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  InviteToPartyResponse copyWith(void Function(InviteToPartyResponse) updates) => super.copyWith((message) => updates(message as InviteToPartyResponse)) as InviteToPartyResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InviteToPartyResponse create() => InviteToPartyResponse._();
  InviteToPartyResponse createEmptyInstance() => create();
  static $pb.PbList<InviteToPartyResponse> createRepeated() => $pb.PbList<InviteToPartyResponse>();
  @$core.pragma('dart2js:noInline')
  static InviteToPartyResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<InviteToPartyResponse>(create);
  static InviteToPartyResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get inviteId => $_getSZ(1);
  @$pb.TagNumber(2)
  set inviteId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasInviteId() => $_has(1);
  @$pb.TagNumber(2)
  void clearInviteId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get expiresAt => $_getI64(2);
  @$pb.TagNumber(3)
  set expiresAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasExpiresAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearExpiresAt() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get serverTime => $_getI64(3);
  @$pb.TagNumber(4)
  set serverTime($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasServerTime() => $_has(3);
  @$pb.TagNumber(4)
  void clearServerTime() => clearField(4);
}

/// Accepting an invite joins the party: the response carries the new
/// snapshot, the invite is consumed and every other open invite to this user
/// is dropped too (joining one party invalidates the rest). Accepting on a
/// second device after already joining is an idempotent OK.
class AcceptInviteRequest extends $pb.GeneratedMessage {
  factory AcceptInviteRequest({
    $core.String? userId,
    $core.String? inviteId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (inviteId != null) {
      $result.inviteId = inviteId;
    }
    return $result;
  }
  AcceptInviteRequest._() : super();
  factory AcceptInviteRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory AcceptInviteRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'AcceptInviteRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'inviteId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  AcceptInviteRequest clone() => AcceptInviteRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  AcceptInviteRequest copyWith(void Function(AcceptInviteRequest) updates) => super.copyWith((message) => updates(message as AcceptInviteRequest)) as AcceptInviteRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AcceptInviteRequest create() => AcceptInviteRequest._();
  AcceptInviteRequest createEmptyInstance() => create();
  static $pb.PbList<AcceptInviteRequest> createRepeated() => $pb.PbList<AcceptInviteRequest>();
  @$core.pragma('dart2js:noInline')
  static AcceptInviteRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<AcceptInviteRequest>(create);
  static AcceptInviteRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get inviteId => $_getSZ(1);
  @$pb.TagNumber(2)
  set inviteId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasInviteId() => $_has(1);
  @$pb.TagNumber(2)
  void clearInviteId() => clearField(2);
}

class AcceptInviteResponse extends $pb.GeneratedMessage {
  factory AcceptInviteResponse({
    $0.ErrorCode? code,
    PartyInfo? party,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (party != null) {
      $result.party = party;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  AcceptInviteResponse._() : super();
  factory AcceptInviteResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory AcceptInviteResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'AcceptInviteResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<PartyInfo>(2, _omitFieldNames ? '' : 'party', subBuilder: PartyInfo.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  AcceptInviteResponse clone() => AcceptInviteResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  AcceptInviteResponse copyWith(void Function(AcceptInviteResponse) updates) => super.copyWith((message) => updates(message as AcceptInviteResponse)) as AcceptInviteResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AcceptInviteResponse create() => AcceptInviteResponse._();
  AcceptInviteResponse createEmptyInstance() => create();
  static $pb.PbList<AcceptInviteResponse> createRepeated() => $pb.PbList<AcceptInviteResponse>();
  @$core.pragma('dart2js:noInline')
  static AcceptInviteResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<AcceptInviteResponse>(create);
  static AcceptInviteResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  PartyInfo get party => $_getN(1);
  @$pb.TagNumber(2)
  set party(PartyInfo v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasParty() => $_has(1);
  @$pb.TagNumber(2)
  void clearParty() => clearField(2);
  @$pb.TagNumber(2)
  PartyInfo ensureParty() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

class DeclineInviteRequest extends $pb.GeneratedMessage {
  factory DeclineInviteRequest({
    $core.String? userId,
    $core.String? inviteId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (inviteId != null) {
      $result.inviteId = inviteId;
    }
    return $result;
  }
  DeclineInviteRequest._() : super();
  factory DeclineInviteRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeclineInviteRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeclineInviteRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'inviteId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeclineInviteRequest clone() => DeclineInviteRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeclineInviteRequest copyWith(void Function(DeclineInviteRequest) updates) => super.copyWith((message) => updates(message as DeclineInviteRequest)) as DeclineInviteRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeclineInviteRequest create() => DeclineInviteRequest._();
  DeclineInviteRequest createEmptyInstance() => create();
  static $pb.PbList<DeclineInviteRequest> createRepeated() => $pb.PbList<DeclineInviteRequest>();
  @$core.pragma('dart2js:noInline')
  static DeclineInviteRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeclineInviteRequest>(create);
  static DeclineInviteRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get inviteId => $_getSZ(1);
  @$pb.TagNumber(2)
  set inviteId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasInviteId() => $_has(1);
  @$pb.TagNumber(2)
  void clearInviteId() => clearField(2);
}

class DeclineInviteResponse extends $pb.GeneratedMessage {
  factory DeclineInviteResponse({
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
  DeclineInviteResponse._() : super();
  factory DeclineInviteResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeclineInviteResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeclineInviteResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeclineInviteResponse clone() => DeclineInviteResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeclineInviteResponse copyWith(void Function(DeclineInviteResponse) updates) => super.copyWith((message) => updates(message as DeclineInviteResponse)) as DeclineInviteResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeclineInviteResponse create() => DeclineInviteResponse._();
  DeclineInviteResponse createEmptyInstance() => create();
  static $pb.PbList<DeclineInviteResponse> createRepeated() => $pb.PbList<DeclineInviteResponse>();
  @$core.pragma('dart2js:noInline')
  static DeclineInviteResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeclineInviteResponse>(create);
  static DeclineInviteResponse? _defaultInstance;

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

/// Leave the party. The last member leaving disbands silently:
/// PARTY_DISBANDED isn't sent because nobody is left to tell;
/// resp.party_disbanded tells the leaver instead.
class LeavePartyRequest extends $pb.GeneratedMessage {
  factory LeavePartyRequest({
    $core.String? userId,
    $core.String? partyId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (partyId != null) {
      $result.partyId = partyId;
    }
    return $result;
  }
  LeavePartyRequest._() : super();
  factory LeavePartyRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory LeavePartyRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'LeavePartyRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'partyId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  LeavePartyRequest clone() => LeavePartyRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  LeavePartyRequest copyWith(void Function(LeavePartyRequest) updates) => super.copyWith((message) => updates(message as LeavePartyRequest)) as LeavePartyRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static LeavePartyRequest create() => LeavePartyRequest._();
  LeavePartyRequest createEmptyInstance() => create();
  static $pb.PbList<LeavePartyRequest> createRepeated() => $pb.PbList<LeavePartyRequest>();
  @$core.pragma('dart2js:noInline')
  static LeavePartyRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<LeavePartyRequest>(create);
  static LeavePartyRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get partyId => $_getSZ(1);
  @$pb.TagNumber(2)
  set partyId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPartyId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPartyId() => clearField(2);
}

class LeavePartyResponse extends $pb.GeneratedMessage {
  factory LeavePartyResponse({
    $0.ErrorCode? code,
    $core.bool? partyDisbanded,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (partyDisbanded != null) {
      $result.partyDisbanded = partyDisbanded;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  LeavePartyResponse._() : super();
  factory LeavePartyResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory LeavePartyResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'LeavePartyResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOB(2, _omitFieldNames ? '' : 'partyDisbanded')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  LeavePartyResponse clone() => LeavePartyResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  LeavePartyResponse copyWith(void Function(LeavePartyResponse) updates) => super.copyWith((message) => updates(message as LeavePartyResponse)) as LeavePartyResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static LeavePartyResponse create() => LeavePartyResponse._();
  LeavePartyResponse createEmptyInstance() => create();
  static $pb.PbList<LeavePartyResponse> createRepeated() => $pb.PbList<LeavePartyResponse>();
  @$core.pragma('dart2js:noInline')
  static LeavePartyResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<LeavePartyResponse>(create);
  static LeavePartyResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get partyDisbanded => $_getBF(1);
  @$pb.TagNumber(2)
  set partyDisbanded($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPartyDisbanded() => $_has(1);
  @$pb.TagNumber(2)
  void clearPartyDisbanded() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Remove a member (leader-only); the kicked user's devices get
/// PARTY_KICKED_NOTIFY, the remaining members get PARTY_LEFT_NOTIFY with
/// reason "kicked".
class KickMemberRequest extends $pb.GeneratedMessage {
  factory KickMemberRequest({
    $core.String? userId,
    $core.String? partyId,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  KickMemberRequest._() : super();
  factory KickMemberRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory KickMemberRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'KickMemberRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'partyId')
    ..aOS(3, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  KickMemberRequest clone() => KickMemberRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  KickMemberRequest copyWith(void Function(KickMemberRequest) updates) => super.copyWith((message) => updates(message as KickMemberRequest)) as KickMemberRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static KickMemberRequest create() => KickMemberRequest._();
  KickMemberRequest createEmptyInstance() => create();
  static $pb.PbList<KickMemberRequest> createRepeated() => $pb.PbList<KickMemberRequest>();
  @$core.pragma('dart2js:noInline')
  static KickMemberRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<KickMemberRequest>(create);
  static KickMemberRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get partyId => $_getSZ(1);
  @$pb.TagNumber(2)
  set partyId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPartyId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPartyId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get targetUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set targetUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTargetUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearTargetUserId() => clearField(3);
}

class KickMemberResponse extends $pb.GeneratedMessage {
  factory KickMemberResponse({
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
  KickMemberResponse._() : super();
  factory KickMemberResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory KickMemberResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'KickMemberResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  KickMemberResponse clone() => KickMemberResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  KickMemberResponse copyWith(void Function(KickMemberResponse) updates) => super.copyWith((message) => updates(message as KickMemberResponse)) as KickMemberResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static KickMemberResponse create() => KickMemberResponse._();
  KickMemberResponse createEmptyInstance() => create();
  static $pb.PbList<KickMemberResponse> createRepeated() => $pb.PbList<KickMemberResponse>();
  @$core.pragma('dart2js:noInline')
  static KickMemberResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<KickMemberResponse>(create);
  static KickMemberResponse? _defaultInstance;

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

/// Hand leadership to another member (leader-only).
class TransferLeaderRequest extends $pb.GeneratedMessage {
  factory TransferLeaderRequest({
    $core.String? userId,
    $core.String? partyId,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  TransferLeaderRequest._() : super();
  factory TransferLeaderRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory TransferLeaderRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'TransferLeaderRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'partyId')
    ..aOS(3, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  TransferLeaderRequest clone() => TransferLeaderRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  TransferLeaderRequest copyWith(void Function(TransferLeaderRequest) updates) => super.copyWith((message) => updates(message as TransferLeaderRequest)) as TransferLeaderRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TransferLeaderRequest create() => TransferLeaderRequest._();
  TransferLeaderRequest createEmptyInstance() => create();
  static $pb.PbList<TransferLeaderRequest> createRepeated() => $pb.PbList<TransferLeaderRequest>();
  @$core.pragma('dart2js:noInline')
  static TransferLeaderRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<TransferLeaderRequest>(create);
  static TransferLeaderRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get partyId => $_getSZ(1);
  @$pb.TagNumber(2)
  set partyId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPartyId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPartyId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get targetUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set targetUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTargetUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearTargetUserId() => clearField(3);
}

class TransferLeaderResponse extends $pb.GeneratedMessage {
  factory TransferLeaderResponse({
    $0.ErrorCode? code,
    $core.String? leaderId,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (leaderId != null) {
      $result.leaderId = leaderId;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  TransferLeaderResponse._() : super();
  factory TransferLeaderResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory TransferLeaderResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'TransferLeaderResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'leaderId')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  TransferLeaderResponse clone() => TransferLeaderResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  TransferLeaderResponse copyWith(void Function(TransferLeaderResponse) updates) => super.copyWith((message) => updates(message as TransferLeaderResponse)) as TransferLeaderResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TransferLeaderResponse create() => TransferLeaderResponse._();
  TransferLeaderResponse createEmptyInstance() => create();
  static $pb.PbList<TransferLeaderResponse> createRepeated() => $pb.PbList<TransferLeaderResponse>();
  @$core.pragma('dart2js:noInline')
  static TransferLeaderResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<TransferLeaderResponse>(create);
  static TransferLeaderResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get leaderId => $_getSZ(1);
  @$pb.TagNumber(2)
  set leaderId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasLeaderId() => $_has(1);
  @$pb.TagNumber(2)
  void clearLeaderId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Flip the caller's ready-check flag.
class SetReadyRequest extends $pb.GeneratedMessage {
  factory SetReadyRequest({
    $core.String? userId,
    $core.String? partyId,
    $core.bool? ready,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (ready != null) {
      $result.ready = ready;
    }
    return $result;
  }
  SetReadyRequest._() : super();
  factory SetReadyRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetReadyRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetReadyRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'partyId')
    ..aOB(3, _omitFieldNames ? '' : 'ready')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetReadyRequest clone() => SetReadyRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetReadyRequest copyWith(void Function(SetReadyRequest) updates) => super.copyWith((message) => updates(message as SetReadyRequest)) as SetReadyRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetReadyRequest create() => SetReadyRequest._();
  SetReadyRequest createEmptyInstance() => create();
  static $pb.PbList<SetReadyRequest> createRepeated() => $pb.PbList<SetReadyRequest>();
  @$core.pragma('dart2js:noInline')
  static SetReadyRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetReadyRequest>(create);
  static SetReadyRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get partyId => $_getSZ(1);
  @$pb.TagNumber(2)
  set partyId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasPartyId() => $_has(1);
  @$pb.TagNumber(2)
  void clearPartyId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get ready => $_getBF(2);
  @$pb.TagNumber(3)
  set ready($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasReady() => $_has(2);
  @$pb.TagNumber(3)
  void clearReady() => clearField(3);
}

class SetReadyResponse extends $pb.GeneratedMessage {
  factory SetReadyResponse({
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
  SetReadyResponse._() : super();
  factory SetReadyResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetReadyResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetReadyResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetReadyResponse clone() => SetReadyResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetReadyResponse copyWith(void Function(SetReadyResponse) updates) => super.copyWith((message) => updates(message as SetReadyResponse)) as SetReadyResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetReadyResponse create() => SetReadyResponse._();
  SetReadyResponse createEmptyInstance() => create();
  static $pb.PbList<SetReadyResponse> createRepeated() => $pb.PbList<SetReadyResponse>();
  @$core.pragma('dart2js:noInline')
  static SetReadyResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetReadyResponse>(create);
  static SetReadyResponse? _defaultInstance;

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

/// Get the caller's current party, if any (GET_* convention, see
/// proto/social.proto:90-94: no server_time).
class GetMyPartyRequest extends $pb.GeneratedMessage {
  factory GetMyPartyRequest({
    $core.String? userId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetMyPartyRequest._() : super();
  factory GetMyPartyRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetMyPartyRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetMyPartyRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetMyPartyRequest clone() => GetMyPartyRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetMyPartyRequest copyWith(void Function(GetMyPartyRequest) updates) => super.copyWith((message) => updates(message as GetMyPartyRequest)) as GetMyPartyRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetMyPartyRequest create() => GetMyPartyRequest._();
  GetMyPartyRequest createEmptyInstance() => create();
  static $pb.PbList<GetMyPartyRequest> createRepeated() => $pb.PbList<GetMyPartyRequest>();
  @$core.pragma('dart2js:noInline')
  static GetMyPartyRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetMyPartyRequest>(create);
  static GetMyPartyRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);
}

class GetMyPartyResponse extends $pb.GeneratedMessage {
  factory GetMyPartyResponse({
    $0.ErrorCode? code,
    $core.bool? inParty,
    PartyInfo? party,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (inParty != null) {
      $result.inParty = inParty;
    }
    if (party != null) {
      $result.party = party;
    }
    return $result;
  }
  GetMyPartyResponse._() : super();
  factory GetMyPartyResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetMyPartyResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetMyPartyResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOB(2, _omitFieldNames ? '' : 'inParty')
    ..aOM<PartyInfo>(3, _omitFieldNames ? '' : 'party', subBuilder: PartyInfo.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetMyPartyResponse clone() => GetMyPartyResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetMyPartyResponse copyWith(void Function(GetMyPartyResponse) updates) => super.copyWith((message) => updates(message as GetMyPartyResponse)) as GetMyPartyResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetMyPartyResponse create() => GetMyPartyResponse._();
  GetMyPartyResponse createEmptyInstance() => create();
  static $pb.PbList<GetMyPartyResponse> createRepeated() => $pb.PbList<GetMyPartyResponse>();
  @$core.pragma('dart2js:noInline')
  static GetMyPartyResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetMyPartyResponse>(create);
  static GetMyPartyResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get inParty => $_getBF(1);
  @$pb.TagNumber(2)
  set inParty($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasInParty() => $_has(1);
  @$pb.TagNumber(2)
  void clearInParty() => clearField(2);

  @$pb.TagNumber(3)
  PartyInfo get party => $_getN(2);
  @$pb.TagNumber(3)
  set party(PartyInfo v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasParty() => $_has(2);
  @$pb.TagNumber(3)
  void clearParty() => clearField(3);
  @$pb.TagNumber(3)
  PartyInfo ensureParty() => $_ensure(2);
}

/// New invite for the invitee (all their devices). Offline invitees see
/// nothing — there is no catch-up query yet (see the reserved 7028+ block).
class InviteNotify extends $pb.GeneratedMessage {
  factory InviteNotify({
    $core.String? inviteId,
    $core.String? fromUserId,
    PartyInfo? party,
    $fixnum.Int64? expiresAt,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (inviteId != null) {
      $result.inviteId = inviteId;
    }
    if (fromUserId != null) {
      $result.fromUserId = fromUserId;
    }
    if (party != null) {
      $result.party = party;
    }
    if (expiresAt != null) {
      $result.expiresAt = expiresAt;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  InviteNotify._() : super();
  factory InviteNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory InviteNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InviteNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'inviteId')
    ..aOS(2, _omitFieldNames ? '' : 'fromUserId')
    ..aOM<PartyInfo>(3, _omitFieldNames ? '' : 'party', subBuilder: PartyInfo.create)
    ..aInt64(4, _omitFieldNames ? '' : 'expiresAt')
    ..aInt64(5, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  InviteNotify clone() => InviteNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  InviteNotify copyWith(void Function(InviteNotify) updates) => super.copyWith((message) => updates(message as InviteNotify)) as InviteNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InviteNotify create() => InviteNotify._();
  InviteNotify createEmptyInstance() => create();
  static $pb.PbList<InviteNotify> createRepeated() => $pb.PbList<InviteNotify>();
  @$core.pragma('dart2js:noInline')
  static InviteNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<InviteNotify>(create);
  static InviteNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get inviteId => $_getSZ(0);
  @$pb.TagNumber(1)
  set inviteId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasInviteId() => $_has(0);
  @$pb.TagNumber(1)
  void clearInviteId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get fromUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set fromUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFromUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearFromUserId() => clearField(2);

  @$pb.TagNumber(3)
  PartyInfo get party => $_getN(2);
  @$pb.TagNumber(3)
  set party(PartyInfo v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasParty() => $_has(2);
  @$pb.TagNumber(3)
  void clearParty() => clearField(3);
  @$pb.TagNumber(3)
  PartyInfo ensureParty() => $_ensure(2);

  @$pb.TagNumber(4)
  $fixnum.Int64 get expiresAt => $_getI64(3);
  @$pb.TagNumber(4)
  set expiresAt($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasExpiresAt() => $_has(3);
  @$pb.TagNumber(4)
  void clearExpiresAt() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get timestamp => $_getI64(4);
  @$pb.TagNumber(5)
  set timestamp($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasTimestamp() => $_has(4);
  @$pb.TagNumber(5)
  void clearTimestamp() => clearField(5);
}

/// To the inviter: the invitee accepted or declined.
class InviteResultNotify extends $pb.GeneratedMessage {
  factory InviteResultNotify({
    $core.String? inviteId,
    $core.String? targetUserId,
    $core.bool? accepted,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (inviteId != null) {
      $result.inviteId = inviteId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    if (accepted != null) {
      $result.accepted = accepted;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  InviteResultNotify._() : super();
  factory InviteResultNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory InviteResultNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InviteResultNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'inviteId')
    ..aOS(2, _omitFieldNames ? '' : 'targetUserId')
    ..aOB(3, _omitFieldNames ? '' : 'accepted')
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  InviteResultNotify clone() => InviteResultNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  InviteResultNotify copyWith(void Function(InviteResultNotify) updates) => super.copyWith((message) => updates(message as InviteResultNotify)) as InviteResultNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InviteResultNotify create() => InviteResultNotify._();
  InviteResultNotify createEmptyInstance() => create();
  static $pb.PbList<InviteResultNotify> createRepeated() => $pb.PbList<InviteResultNotify>();
  @$core.pragma('dart2js:noInline')
  static InviteResultNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<InviteResultNotify>(create);
  static InviteResultNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get inviteId => $_getSZ(0);
  @$pb.TagNumber(1)
  set inviteId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasInviteId() => $_has(0);
  @$pb.TagNumber(1)
  void clearInviteId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get targetUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set targetUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTargetUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTargetUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get accepted => $_getBF(2);
  @$pb.TagNumber(3)
  set accepted($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasAccepted() => $_has(2);
  @$pb.TagNumber(3)
  void clearAccepted() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);
}

/// Someone joined (everyone except the joiner).
class PartyJoinedNotify extends $pb.GeneratedMessage {
  factory PartyJoinedNotify({
    $core.String? partyId,
    PartyMember? member,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (member != null) {
      $result.member = member;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  PartyJoinedNotify._() : super();
  factory PartyJoinedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PartyJoinedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PartyJoinedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'partyId')
    ..aOM<PartyMember>(2, _omitFieldNames ? '' : 'member', subBuilder: PartyMember.create)
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PartyJoinedNotify clone() => PartyJoinedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PartyJoinedNotify copyWith(void Function(PartyJoinedNotify) updates) => super.copyWith((message) => updates(message as PartyJoinedNotify)) as PartyJoinedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PartyJoinedNotify create() => PartyJoinedNotify._();
  PartyJoinedNotify createEmptyInstance() => create();
  static $pb.PbList<PartyJoinedNotify> createRepeated() => $pb.PbList<PartyJoinedNotify>();
  @$core.pragma('dart2js:noInline')
  static PartyJoinedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PartyJoinedNotify>(create);
  static PartyJoinedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get partyId => $_getSZ(0);
  @$pb.TagNumber(1)
  set partyId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPartyId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPartyId() => clearField(1);

  @$pb.TagNumber(2)
  PartyMember get member => $_getN(1);
  @$pb.TagNumber(2)
  set member(PartyMember v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasMember() => $_has(1);
  @$pb.TagNumber(2)
  void clearMember() => clearField(2);
  @$pb.TagNumber(2)
  PartyMember ensureMember() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

/// Someone left, for every reason ("left" | "kicked" | "offline"); everyone
/// except the leaver.
class PartyLeftNotify extends $pb.GeneratedMessage {
  factory PartyLeftNotify({
    $core.String? partyId,
    $core.String? userId,
    $core.String? reason,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (reason != null) {
      $result.reason = reason;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  PartyLeftNotify._() : super();
  factory PartyLeftNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PartyLeftNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PartyLeftNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'partyId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aOS(3, _omitFieldNames ? '' : 'reason')
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PartyLeftNotify clone() => PartyLeftNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PartyLeftNotify copyWith(void Function(PartyLeftNotify) updates) => super.copyWith((message) => updates(message as PartyLeftNotify)) as PartyLeftNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PartyLeftNotify create() => PartyLeftNotify._();
  PartyLeftNotify createEmptyInstance() => create();
  static $pb.PbList<PartyLeftNotify> createRepeated() => $pb.PbList<PartyLeftNotify>();
  @$core.pragma('dart2js:noInline')
  static PartyLeftNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PartyLeftNotify>(create);
  static PartyLeftNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get partyId => $_getSZ(0);
  @$pb.TagNumber(1)
  set partyId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPartyId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPartyId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get reason => $_getSZ(2);
  @$pb.TagNumber(3)
  set reason($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasReason() => $_has(2);
  @$pb.TagNumber(3)
  void clearReason() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);
}

/// To the kicked user only (their remaining membership view is gone).
class PartyKickedNotify extends $pb.GeneratedMessage {
  factory PartyKickedNotify({
    $core.String? partyId,
    $core.String? actorUserId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (actorUserId != null) {
      $result.actorUserId = actorUserId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  PartyKickedNotify._() : super();
  factory PartyKickedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PartyKickedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PartyKickedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'partyId')
    ..aOS(2, _omitFieldNames ? '' : 'actorUserId')
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PartyKickedNotify clone() => PartyKickedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PartyKickedNotify copyWith(void Function(PartyKickedNotify) updates) => super.copyWith((message) => updates(message as PartyKickedNotify)) as PartyKickedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PartyKickedNotify create() => PartyKickedNotify._();
  PartyKickedNotify createEmptyInstance() => create();
  static $pb.PbList<PartyKickedNotify> createRepeated() => $pb.PbList<PartyKickedNotify>();
  @$core.pragma('dart2js:noInline')
  static PartyKickedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PartyKickedNotify>(create);
  static PartyKickedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get partyId => $_getSZ(0);
  @$pb.TagNumber(1)
  set partyId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPartyId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPartyId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get actorUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set actorUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasActorUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearActorUserId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

/// Full snapshot to ALL members including the actor.
class PartyStateChangedNotify extends $pb.GeneratedMessage {
  factory PartyStateChangedNotify({
    PartyInfo? party,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (party != null) {
      $result.party = party;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  PartyStateChangedNotify._() : super();
  factory PartyStateChangedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PartyStateChangedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PartyStateChangedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOM<PartyInfo>(1, _omitFieldNames ? '' : 'party', subBuilder: PartyInfo.create)
    ..aInt64(2, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PartyStateChangedNotify clone() => PartyStateChangedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PartyStateChangedNotify copyWith(void Function(PartyStateChangedNotify) updates) => super.copyWith((message) => updates(message as PartyStateChangedNotify)) as PartyStateChangedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PartyStateChangedNotify create() => PartyStateChangedNotify._();
  PartyStateChangedNotify createEmptyInstance() => create();
  static $pb.PbList<PartyStateChangedNotify> createRepeated() => $pb.PbList<PartyStateChangedNotify>();
  @$core.pragma('dart2js:noInline')
  static PartyStateChangedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PartyStateChangedNotify>(create);
  static PartyStateChangedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  PartyInfo get party => $_getN(0);
  @$pb.TagNumber(1)
  set party(PartyInfo v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasParty() => $_has(0);
  @$pb.TagNumber(1)
  void clearParty() => clearField(1);
  @$pb.TagNumber(1)
  PartyInfo ensureParty() => $_ensure(0);

  @$pb.TagNumber(2)
  $fixnum.Int64 get timestamp => $_getI64(1);
  @$pb.TagNumber(2)
  set timestamp($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTimestamp() => $_has(1);
  @$pb.TagNumber(2)
  void clearTimestamp() => clearField(2);
}

/// The party is gone because the leader disbanded it; remaining members get
/// this. (The last member leaving disbands silently — nobody is left to
/// tell.) actor_user_id is the ex-leader; the service never auto-disbands
/// loudly, so it is never empty in practice.
class PartyDisbandedNotify extends $pb.GeneratedMessage {
  factory PartyDisbandedNotify({
    $core.String? partyId,
    $core.String? actorUserId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (actorUserId != null) {
      $result.actorUserId = actorUserId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  PartyDisbandedNotify._() : super();
  factory PartyDisbandedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PartyDisbandedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PartyDisbandedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'partyId')
    ..aOS(2, _omitFieldNames ? '' : 'actorUserId')
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PartyDisbandedNotify clone() => PartyDisbandedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PartyDisbandedNotify copyWith(void Function(PartyDisbandedNotify) updates) => super.copyWith((message) => updates(message as PartyDisbandedNotify)) as PartyDisbandedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PartyDisbandedNotify create() => PartyDisbandedNotify._();
  PartyDisbandedNotify createEmptyInstance() => create();
  static $pb.PbList<PartyDisbandedNotify> createRepeated() => $pb.PbList<PartyDisbandedNotify>();
  @$core.pragma('dart2js:noInline')
  static PartyDisbandedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PartyDisbandedNotify>(create);
  static PartyDisbandedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get partyId => $_getSZ(0);
  @$pb.TagNumber(1)
  set partyId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPartyId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPartyId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get actorUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set actorUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasActorUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearActorUserId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

/// ---------------------------------------------------------------------------
/// Server-side storage snapshots. These are Redis VALUES (one key per party,
/// e.g. "chirp:party:party:<party_id>"), never wire messages. Kept separate
/// from PartyInfo so the storage schema can evolve without touching the
/// client-facing protocol. Invites are deliberately NOT stored — they are
/// short-lived and lost on restart by design.
class StoredParty extends $pb.GeneratedMessage {
  factory StoredParty({
    $core.String? partyId,
    $core.String? leaderId,
    $core.int? maxMembers,
    $fixnum.Int64? createdAt,
    $core.Iterable<StoredMember>? members,
  }) {
    final $result = create();
    if (partyId != null) {
      $result.partyId = partyId;
    }
    if (leaderId != null) {
      $result.leaderId = leaderId;
    }
    if (maxMembers != null) {
      $result.maxMembers = maxMembers;
    }
    if (createdAt != null) {
      $result.createdAt = createdAt;
    }
    if (members != null) {
      $result.members.addAll(members);
    }
    return $result;
  }
  StoredParty._() : super();
  factory StoredParty.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredParty.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredParty', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'partyId')
    ..aOS(2, _omitFieldNames ? '' : 'leaderId')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'maxMembers', $pb.PbFieldType.O3)
    ..aInt64(4, _omitFieldNames ? '' : 'createdAt')
    ..pc<StoredMember>(5, _omitFieldNames ? '' : 'members', $pb.PbFieldType.PM, subBuilder: StoredMember.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredParty clone() => StoredParty()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredParty copyWith(void Function(StoredParty) updates) => super.copyWith((message) => updates(message as StoredParty)) as StoredParty;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredParty create() => StoredParty._();
  StoredParty createEmptyInstance() => create();
  static $pb.PbList<StoredParty> createRepeated() => $pb.PbList<StoredParty>();
  @$core.pragma('dart2js:noInline')
  static StoredParty getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredParty>(create);
  static StoredParty? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get partyId => $_getSZ(0);
  @$pb.TagNumber(1)
  set partyId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasPartyId() => $_has(0);
  @$pb.TagNumber(1)
  void clearPartyId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get leaderId => $_getSZ(1);
  @$pb.TagNumber(2)
  set leaderId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasLeaderId() => $_has(1);
  @$pb.TagNumber(2)
  void clearLeaderId() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get maxMembers => $_getIZ(2);
  @$pb.TagNumber(3)
  set maxMembers($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMaxMembers() => $_has(2);
  @$pb.TagNumber(3)
  void clearMaxMembers() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get createdAt => $_getI64(3);
  @$pb.TagNumber(4)
  set createdAt($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasCreatedAt() => $_has(3);
  @$pb.TagNumber(4)
  void clearCreatedAt() => clearField(4);

  @$pb.TagNumber(5)
  $core.List<StoredMember> get members => $_getList(4);
}

class StoredMember extends $pb.GeneratedMessage {
  factory StoredMember({
    $core.String? userId,
    $core.bool? ready,
    $fixnum.Int64? joinedAt,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (ready != null) {
      $result.ready = ready;
    }
    if (joinedAt != null) {
      $result.joinedAt = joinedAt;
    }
    return $result;
  }
  StoredMember._() : super();
  factory StoredMember.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory StoredMember.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'StoredMember', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.party'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOB(2, _omitFieldNames ? '' : 'ready')
    ..aInt64(3, _omitFieldNames ? '' : 'joinedAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  StoredMember clone() => StoredMember()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  StoredMember copyWith(void Function(StoredMember) updates) => super.copyWith((message) => updates(message as StoredMember)) as StoredMember;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static StoredMember create() => StoredMember._();
  StoredMember createEmptyInstance() => create();
  static $pb.PbList<StoredMember> createRepeated() => $pb.PbList<StoredMember>();
  @$core.pragma('dart2js:noInline')
  static StoredMember getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<StoredMember>(create);
  static StoredMember? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get ready => $_getBF(1);
  @$pb.TagNumber(2)
  set ready($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasReady() => $_has(1);
  @$pb.TagNumber(2)
  void clearReady() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get joinedAt => $_getI64(2);
  @$pb.TagNumber(3)
  set joinedAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasJoinedAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearJoinedAt() => clearField(3);
}


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
