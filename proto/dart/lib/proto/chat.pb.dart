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

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'chat.pbenum.dart';
import 'common.pbenum.dart' as $0;

export 'chat.pbenum.dart';

/// 发送消息请求
class SendMessageRequest extends $pb.GeneratedMessage {
  factory SendMessageRequest({
    $core.String? senderId,
    $core.String? receiverId,
    ChannelType? channelType,
    $core.String? channelId,
    MsgType? msgType,
    $core.List<$core.int>? content,
    $fixnum.Int64? clientTimestamp,
    Priority? priority,
    $core.List<$core.int>? metadata,
    $core.int? ttlSeconds,
    $core.String? replyToMessageId,
  }) {
    final $result = create();
    if (senderId != null) {
      $result.senderId = senderId;
    }
    if (receiverId != null) {
      $result.receiverId = receiverId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (msgType != null) {
      $result.msgType = msgType;
    }
    if (content != null) {
      $result.content = content;
    }
    if (clientTimestamp != null) {
      $result.clientTimestamp = clientTimestamp;
    }
    if (priority != null) {
      $result.priority = priority;
    }
    if (metadata != null) {
      $result.metadata = metadata;
    }
    if (ttlSeconds != null) {
      $result.ttlSeconds = ttlSeconds;
    }
    if (replyToMessageId != null) {
      $result.replyToMessageId = replyToMessageId;
    }
    return $result;
  }
  SendMessageRequest._() : super();
  factory SendMessageRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SendMessageRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SendMessageRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'senderId')
    ..aOS(2, _omitFieldNames ? '' : 'receiverId')
    ..e<ChannelType>(3, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(4, _omitFieldNames ? '' : 'channelId')
    ..e<MsgType>(5, _omitFieldNames ? '' : 'msgType', $pb.PbFieldType.OE, defaultOrMaker: MsgType.TEXT, valueOf: MsgType.valueOf, enumValues: MsgType.values)
    ..a<$core.List<$core.int>>(6, _omitFieldNames ? '' : 'content', $pb.PbFieldType.OY)
    ..aInt64(7, _omitFieldNames ? '' : 'clientTimestamp')
    ..e<Priority>(8, _omitFieldNames ? '' : 'priority', $pb.PbFieldType.OE, defaultOrMaker: Priority.PRIORITY_LOW, valueOf: Priority.valueOf, enumValues: Priority.values)
    ..a<$core.List<$core.int>>(9, _omitFieldNames ? '' : 'metadata', $pb.PbFieldType.OY)
    ..a<$core.int>(10, _omitFieldNames ? '' : 'ttlSeconds', $pb.PbFieldType.O3)
    ..aOS(11, _omitFieldNames ? '' : 'replyToMessageId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SendMessageRequest clone() => SendMessageRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SendMessageRequest copyWith(void Function(SendMessageRequest) updates) => super.copyWith((message) => updates(message as SendMessageRequest)) as SendMessageRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SendMessageRequest create() => SendMessageRequest._();
  SendMessageRequest createEmptyInstance() => create();
  static $pb.PbList<SendMessageRequest> createRepeated() => $pb.PbList<SendMessageRequest>();
  @$core.pragma('dart2js:noInline')
  static SendMessageRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SendMessageRequest>(create);
  static SendMessageRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get senderId => $_getSZ(0);
  @$pb.TagNumber(1)
  set senderId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasSenderId() => $_has(0);
  @$pb.TagNumber(1)
  void clearSenderId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get receiverId => $_getSZ(1);
  @$pb.TagNumber(2)
  set receiverId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasReceiverId() => $_has(1);
  @$pb.TagNumber(2)
  void clearReceiverId() => clearField(2);

  @$pb.TagNumber(3)
  ChannelType get channelType => $_getN(2);
  @$pb.TagNumber(3)
  set channelType(ChannelType v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelType() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelType() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get channelId => $_getSZ(3);
  @$pb.TagNumber(4)
  set channelId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasChannelId() => $_has(3);
  @$pb.TagNumber(4)
  void clearChannelId() => clearField(4);

  @$pb.TagNumber(5)
  MsgType get msgType => $_getN(4);
  @$pb.TagNumber(5)
  set msgType(MsgType v) { setField(5, v); }
  @$pb.TagNumber(5)
  $core.bool hasMsgType() => $_has(4);
  @$pb.TagNumber(5)
  void clearMsgType() => clearField(5);

  @$pb.TagNumber(6)
  $core.List<$core.int> get content => $_getN(5);
  @$pb.TagNumber(6)
  set content($core.List<$core.int> v) { $_setBytes(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasContent() => $_has(5);
  @$pb.TagNumber(6)
  void clearContent() => clearField(6);

  @$pb.TagNumber(7)
  $fixnum.Int64 get clientTimestamp => $_getI64(6);
  @$pb.TagNumber(7)
  set clientTimestamp($fixnum.Int64 v) { $_setInt64(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasClientTimestamp() => $_has(6);
  @$pb.TagNumber(7)
  void clearClientTimestamp() => clearField(7);

  @$pb.TagNumber(8)
  Priority get priority => $_getN(7);
  @$pb.TagNumber(8)
  set priority(Priority v) { setField(8, v); }
  @$pb.TagNumber(8)
  $core.bool hasPriority() => $_has(7);
  @$pb.TagNumber(8)
  void clearPriority() => clearField(8);

  @$pb.TagNumber(9)
  $core.List<$core.int> get metadata => $_getN(8);
  @$pb.TagNumber(9)
  set metadata($core.List<$core.int> v) { $_setBytes(8, v); }
  @$pb.TagNumber(9)
  $core.bool hasMetadata() => $_has(8);
  @$pb.TagNumber(9)
  void clearMetadata() => clearField(9);

  @$pb.TagNumber(10)
  $core.int get ttlSeconds => $_getIZ(9);
  @$pb.TagNumber(10)
  set ttlSeconds($core.int v) { $_setSignedInt32(9, v); }
  @$pb.TagNumber(10)
  $core.bool hasTtlSeconds() => $_has(9);
  @$pb.TagNumber(10)
  void clearTtlSeconds() => clearField(10);

  @$pb.TagNumber(11)
  $core.String get replyToMessageId => $_getSZ(10);
  @$pb.TagNumber(11)
  set replyToMessageId($core.String v) { $_setString(10, v); }
  @$pb.TagNumber(11)
  $core.bool hasReplyToMessageId() => $_has(10);
  @$pb.TagNumber(11)
  void clearReplyToMessageId() => clearField(11);
}

/// 发送消息响应
class SendMessageResponse extends $pb.GeneratedMessage {
  factory SendMessageResponse({
    $0.ErrorCode? code,
    $core.String? messageId,
    $fixnum.Int64? serverTimestamp,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (serverTimestamp != null) {
      $result.serverTimestamp = serverTimestamp;
    }
    return $result;
  }
  SendMessageResponse._() : super();
  factory SendMessageResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SendMessageResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SendMessageResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'messageId')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTimestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SendMessageResponse clone() => SendMessageResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SendMessageResponse copyWith(void Function(SendMessageResponse) updates) => super.copyWith((message) => updates(message as SendMessageResponse)) as SendMessageResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SendMessageResponse create() => SendMessageResponse._();
  SendMessageResponse createEmptyInstance() => create();
  static $pb.PbList<SendMessageResponse> createRepeated() => $pb.PbList<SendMessageResponse>();
  @$core.pragma('dart2js:noInline')
  static SendMessageResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SendMessageResponse>(create);
  static SendMessageResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get messageId => $_getSZ(1);
  @$pb.TagNumber(2)
  set messageId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasMessageId() => $_has(1);
  @$pb.TagNumber(2)
  void clearMessageId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTimestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTimestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTimestamp() => clearField(3);
}

/// 聊天消息
class ChatMessage extends $pb.GeneratedMessage {
  factory ChatMessage({
    $core.String? messageId,
    $core.String? senderId,
    $core.String? receiverId,
    ChannelType? channelType,
    $core.String? channelId,
    MsgType? msgType,
    $core.List<$core.int>? content,
    $fixnum.Int64? timestamp,
    Priority? priority,
    $core.List<$core.int>? metadata,
    $core.int? ttlSeconds,
    SenderKind? senderKind,
    $core.String? replyToMessageId,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (senderId != null) {
      $result.senderId = senderId;
    }
    if (receiverId != null) {
      $result.receiverId = receiverId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (msgType != null) {
      $result.msgType = msgType;
    }
    if (content != null) {
      $result.content = content;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    if (priority != null) {
      $result.priority = priority;
    }
    if (metadata != null) {
      $result.metadata = metadata;
    }
    if (ttlSeconds != null) {
      $result.ttlSeconds = ttlSeconds;
    }
    if (senderKind != null) {
      $result.senderKind = senderKind;
    }
    if (replyToMessageId != null) {
      $result.replyToMessageId = replyToMessageId;
    }
    return $result;
  }
  ChatMessage._() : super();
  factory ChatMessage.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChatMessage.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChatMessage', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'senderId')
    ..aOS(3, _omitFieldNames ? '' : 'receiverId')
    ..e<ChannelType>(4, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(5, _omitFieldNames ? '' : 'channelId')
    ..e<MsgType>(6, _omitFieldNames ? '' : 'msgType', $pb.PbFieldType.OE, defaultOrMaker: MsgType.TEXT, valueOf: MsgType.valueOf, enumValues: MsgType.values)
    ..a<$core.List<$core.int>>(7, _omitFieldNames ? '' : 'content', $pb.PbFieldType.OY)
    ..aInt64(8, _omitFieldNames ? '' : 'timestamp')
    ..e<Priority>(9, _omitFieldNames ? '' : 'priority', $pb.PbFieldType.OE, defaultOrMaker: Priority.PRIORITY_LOW, valueOf: Priority.valueOf, enumValues: Priority.values)
    ..a<$core.List<$core.int>>(10, _omitFieldNames ? '' : 'metadata', $pb.PbFieldType.OY)
    ..a<$core.int>(11, _omitFieldNames ? '' : 'ttlSeconds', $pb.PbFieldType.O3)
    ..e<SenderKind>(12, _omitFieldNames ? '' : 'senderKind', $pb.PbFieldType.OE, defaultOrMaker: SenderKind.SENDER_USER, valueOf: SenderKind.valueOf, enumValues: SenderKind.values)
    ..aOS(13, _omitFieldNames ? '' : 'replyToMessageId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChatMessage clone() => ChatMessage()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChatMessage copyWith(void Function(ChatMessage) updates) => super.copyWith((message) => updates(message as ChatMessage)) as ChatMessage;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChatMessage create() => ChatMessage._();
  ChatMessage createEmptyInstance() => create();
  static $pb.PbList<ChatMessage> createRepeated() => $pb.PbList<ChatMessage>();
  @$core.pragma('dart2js:noInline')
  static ChatMessage getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChatMessage>(create);
  static ChatMessage? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get senderId => $_getSZ(1);
  @$pb.TagNumber(2)
  set senderId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSenderId() => $_has(1);
  @$pb.TagNumber(2)
  void clearSenderId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get receiverId => $_getSZ(2);
  @$pb.TagNumber(3)
  set receiverId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasReceiverId() => $_has(2);
  @$pb.TagNumber(3)
  void clearReceiverId() => clearField(3);

  @$pb.TagNumber(4)
  ChannelType get channelType => $_getN(3);
  @$pb.TagNumber(4)
  set channelType(ChannelType v) { setField(4, v); }
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
  MsgType get msgType => $_getN(5);
  @$pb.TagNumber(6)
  set msgType(MsgType v) { setField(6, v); }
  @$pb.TagNumber(6)
  $core.bool hasMsgType() => $_has(5);
  @$pb.TagNumber(6)
  void clearMsgType() => clearField(6);

  @$pb.TagNumber(7)
  $core.List<$core.int> get content => $_getN(6);
  @$pb.TagNumber(7)
  set content($core.List<$core.int> v) { $_setBytes(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasContent() => $_has(6);
  @$pb.TagNumber(7)
  void clearContent() => clearField(7);

  @$pb.TagNumber(8)
  $fixnum.Int64 get timestamp => $_getI64(7);
  @$pb.TagNumber(8)
  set timestamp($fixnum.Int64 v) { $_setInt64(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasTimestamp() => $_has(7);
  @$pb.TagNumber(8)
  void clearTimestamp() => clearField(8);

  @$pb.TagNumber(9)
  Priority get priority => $_getN(8);
  @$pb.TagNumber(9)
  set priority(Priority v) { setField(9, v); }
  @$pb.TagNumber(9)
  $core.bool hasPriority() => $_has(8);
  @$pb.TagNumber(9)
  void clearPriority() => clearField(9);

  @$pb.TagNumber(10)
  $core.List<$core.int> get metadata => $_getN(9);
  @$pb.TagNumber(10)
  set metadata($core.List<$core.int> v) { $_setBytes(9, v); }
  @$pb.TagNumber(10)
  $core.bool hasMetadata() => $_has(9);
  @$pb.TagNumber(10)
  void clearMetadata() => clearField(10);

  @$pb.TagNumber(11)
  $core.int get ttlSeconds => $_getIZ(10);
  @$pb.TagNumber(11)
  set ttlSeconds($core.int v) { $_setSignedInt32(10, v); }
  @$pb.TagNumber(11)
  $core.bool hasTtlSeconds() => $_has(10);
  @$pb.TagNumber(11)
  void clearTtlSeconds() => clearField(11);

  @$pb.TagNumber(12)
  SenderKind get senderKind => $_getN(11);
  @$pb.TagNumber(12)
  set senderKind(SenderKind v) { setField(12, v); }
  @$pb.TagNumber(12)
  $core.bool hasSenderKind() => $_has(11);
  @$pb.TagNumber(12)
  void clearSenderKind() => clearField(12);

  @$pb.TagNumber(13)
  $core.String get replyToMessageId => $_getSZ(12);
  @$pb.TagNumber(13)
  set replyToMessageId($core.String v) { $_setString(12, v); }
  @$pb.TagNumber(13)
  $core.bool hasReplyToMessageId() => $_has(12);
  @$pb.TagNumber(13)
  void clearReplyToMessageId() => clearField(13);
}

/// Player -> NPC utterance, published as an event payload to the NPC dialog
/// service (event_type "npc.player_message"). The chat history keeps the
/// player's original message; the NPC replies through the injection path.
class NpcPlayerUtterance extends $pb.GeneratedMessage {
  factory NpcPlayerUtterance({
    $core.String? messageId,
    $core.String? senderId,
    $core.String? npcId,
    $core.List<$core.int>? content,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (senderId != null) {
      $result.senderId = senderId;
    }
    if (npcId != null) {
      $result.npcId = npcId;
    }
    if (content != null) {
      $result.content = content;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  NpcPlayerUtterance._() : super();
  factory NpcPlayerUtterance.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory NpcPlayerUtterance.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'NpcPlayerUtterance', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'senderId')
    ..aOS(3, _omitFieldNames ? '' : 'npcId')
    ..a<$core.List<$core.int>>(4, _omitFieldNames ? '' : 'content', $pb.PbFieldType.OY)
    ..aInt64(5, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  NpcPlayerUtterance clone() => NpcPlayerUtterance()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  NpcPlayerUtterance copyWith(void Function(NpcPlayerUtterance) updates) => super.copyWith((message) => updates(message as NpcPlayerUtterance)) as NpcPlayerUtterance;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static NpcPlayerUtterance create() => NpcPlayerUtterance._();
  NpcPlayerUtterance createEmptyInstance() => create();
  static $pb.PbList<NpcPlayerUtterance> createRepeated() => $pb.PbList<NpcPlayerUtterance>();
  @$core.pragma('dart2js:noInline')
  static NpcPlayerUtterance getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<NpcPlayerUtterance>(create);
  static NpcPlayerUtterance? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get senderId => $_getSZ(1);
  @$pb.TagNumber(2)
  set senderId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSenderId() => $_has(1);
  @$pb.TagNumber(2)
  void clearSenderId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get npcId => $_getSZ(2);
  @$pb.TagNumber(3)
  set npcId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasNpcId() => $_has(2);
  @$pb.TagNumber(3)
  void clearNpcId() => clearField(3);

  @$pb.TagNumber(4)
  $core.List<$core.int> get content => $_getN(3);
  @$pb.TagNumber(4)
  set content($core.List<$core.int> v) { $_setBytes(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasContent() => $_has(3);
  @$pb.TagNumber(4)
  void clearContent() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get timestamp => $_getI64(4);
  @$pb.TagNumber(5)
  set timestamp($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasTimestamp() => $_has(4);
  @$pb.TagNumber(5)
  void clearTimestamp() => clearField(5);
}

/// 拉取历史消息请求
class GetHistoryRequest extends $pb.GeneratedMessage {
  factory GetHistoryRequest({
    $core.String? userId,
    ChannelType? channelType,
    $core.String? channelId,
    $fixnum.Int64? beforeTimestamp,
    $core.int? limit,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (beforeTimestamp != null) {
      $result.beforeTimestamp = beforeTimestamp;
    }
    if (limit != null) {
      $result.limit = limit;
    }
    return $result;
  }
  GetHistoryRequest._() : super();
  factory GetHistoryRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetHistoryRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetHistoryRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(3, _omitFieldNames ? '' : 'channelId')
    ..aInt64(4, _omitFieldNames ? '' : 'beforeTimestamp')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'limit', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetHistoryRequest clone() => GetHistoryRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetHistoryRequest copyWith(void Function(GetHistoryRequest) updates) => super.copyWith((message) => updates(message as GetHistoryRequest)) as GetHistoryRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetHistoryRequest create() => GetHistoryRequest._();
  GetHistoryRequest createEmptyInstance() => create();
  static $pb.PbList<GetHistoryRequest> createRepeated() => $pb.PbList<GetHistoryRequest>();
  @$core.pragma('dart2js:noInline')
  static GetHistoryRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetHistoryRequest>(create);
  static GetHistoryRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get channelId => $_getSZ(2);
  @$pb.TagNumber(3)
  set channelId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelId() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelId() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get beforeTimestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set beforeTimestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasBeforeTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearBeforeTimestamp() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get limit => $_getIZ(4);
  @$pb.TagNumber(5)
  set limit($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasLimit() => $_has(4);
  @$pb.TagNumber(5)
  void clearLimit() => clearField(5);
}

/// 拉取历史消息响应
class GetHistoryResponse extends $pb.GeneratedMessage {
  factory GetHistoryResponse({
    $0.ErrorCode? code,
    $core.Iterable<ChatMessage>? messages,
    $core.bool? hasMore,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (messages != null) {
      $result.messages.addAll(messages);
    }
    if (hasMore != null) {
      $result.hasMore = hasMore;
    }
    return $result;
  }
  GetHistoryResponse._() : super();
  factory GetHistoryResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetHistoryResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetHistoryResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<ChatMessage>(2, _omitFieldNames ? '' : 'messages', $pb.PbFieldType.PM, subBuilder: ChatMessage.create)
    ..aOB(3, _omitFieldNames ? '' : 'hasMore')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetHistoryResponse clone() => GetHistoryResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetHistoryResponse copyWith(void Function(GetHistoryResponse) updates) => super.copyWith((message) => updates(message as GetHistoryResponse)) as GetHistoryResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetHistoryResponse create() => GetHistoryResponse._();
  GetHistoryResponse createEmptyInstance() => create();
  static $pb.PbList<GetHistoryResponse> createRepeated() => $pb.PbList<GetHistoryResponse>();
  @$core.pragma('dart2js:noInline')
  static GetHistoryResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetHistoryResponse>(create);
  static GetHistoryResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<ChatMessage> get messages => $_getList(1);

  @$pb.TagNumber(3)
  $core.bool get hasMore => $_getBF(2);
  @$pb.TagNumber(3)
  set hasMore($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasHasMore() => $_has(2);
  @$pb.TagNumber(3)
  void clearHasMore() => clearField(3);
}

/// Create group request
class CreateGroupRequest extends $pb.GeneratedMessage {
  factory CreateGroupRequest({
    $core.String? creatorId,
    $core.String? groupName,
    $core.String? description,
    $core.String? avatarUrl,
    $core.int? maxMembers,
    $core.Iterable<$core.String>? initialMembers,
  }) {
    final $result = create();
    if (creatorId != null) {
      $result.creatorId = creatorId;
    }
    if (groupName != null) {
      $result.groupName = groupName;
    }
    if (description != null) {
      $result.description = description;
    }
    if (avatarUrl != null) {
      $result.avatarUrl = avatarUrl;
    }
    if (maxMembers != null) {
      $result.maxMembers = maxMembers;
    }
    if (initialMembers != null) {
      $result.initialMembers.addAll(initialMembers);
    }
    return $result;
  }
  CreateGroupRequest._() : super();
  factory CreateGroupRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateGroupRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateGroupRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'creatorId')
    ..aOS(2, _omitFieldNames ? '' : 'groupName')
    ..aOS(3, _omitFieldNames ? '' : 'description')
    ..aOS(4, _omitFieldNames ? '' : 'avatarUrl')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'maxMembers', $pb.PbFieldType.O3)
    ..pPS(6, _omitFieldNames ? '' : 'initialMembers')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateGroupRequest clone() => CreateGroupRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateGroupRequest copyWith(void Function(CreateGroupRequest) updates) => super.copyWith((message) => updates(message as CreateGroupRequest)) as CreateGroupRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateGroupRequest create() => CreateGroupRequest._();
  CreateGroupRequest createEmptyInstance() => create();
  static $pb.PbList<CreateGroupRequest> createRepeated() => $pb.PbList<CreateGroupRequest>();
  @$core.pragma('dart2js:noInline')
  static CreateGroupRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateGroupRequest>(create);
  static CreateGroupRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get creatorId => $_getSZ(0);
  @$pb.TagNumber(1)
  set creatorId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasCreatorId() => $_has(0);
  @$pb.TagNumber(1)
  void clearCreatorId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupName => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupName($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupName() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupName() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get description => $_getSZ(2);
  @$pb.TagNumber(3)
  set description($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasDescription() => $_has(2);
  @$pb.TagNumber(3)
  void clearDescription() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get avatarUrl => $_getSZ(3);
  @$pb.TagNumber(4)
  set avatarUrl($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasAvatarUrl() => $_has(3);
  @$pb.TagNumber(4)
  void clearAvatarUrl() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get maxMembers => $_getIZ(4);
  @$pb.TagNumber(5)
  set maxMembers($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasMaxMembers() => $_has(4);
  @$pb.TagNumber(5)
  void clearMaxMembers() => clearField(5);

  @$pb.TagNumber(6)
  $core.List<$core.String> get initialMembers => $_getList(5);
}

class CreateGroupResponse extends $pb.GeneratedMessage {
  factory CreateGroupResponse({
    $0.ErrorCode? code,
    $core.String? groupId,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  CreateGroupResponse._() : super();
  factory CreateGroupResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateGroupResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateGroupResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateGroupResponse clone() => CreateGroupResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateGroupResponse copyWith(void Function(CreateGroupResponse) updates) => super.copyWith((message) => updates(message as CreateGroupResponse)) as CreateGroupResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateGroupResponse create() => CreateGroupResponse._();
  CreateGroupResponse createEmptyInstance() => create();
  static $pb.PbList<CreateGroupResponse> createRepeated() => $pb.PbList<CreateGroupResponse>();
  @$core.pragma('dart2js:noInline')
  static CreateGroupResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateGroupResponse>(create);
  static CreateGroupResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Group information
class GroupInfo extends $pb.GeneratedMessage {
  factory GroupInfo({
    $core.String? groupId,
    $core.String? groupName,
    $core.String? description,
    $core.String? avatarUrl,
    $core.String? ownerId,
    $core.int? memberCount,
    $core.int? maxMembers,
    $fixnum.Int64? createdAt,
    $core.Map<$core.String, $core.String>? metadata,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (groupName != null) {
      $result.groupName = groupName;
    }
    if (description != null) {
      $result.description = description;
    }
    if (avatarUrl != null) {
      $result.avatarUrl = avatarUrl;
    }
    if (ownerId != null) {
      $result.ownerId = ownerId;
    }
    if (memberCount != null) {
      $result.memberCount = memberCount;
    }
    if (maxMembers != null) {
      $result.maxMembers = maxMembers;
    }
    if (createdAt != null) {
      $result.createdAt = createdAt;
    }
    if (metadata != null) {
      $result.metadata.addAll(metadata);
    }
    return $result;
  }
  GroupInfo._() : super();
  factory GroupInfo.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GroupInfo.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GroupInfo', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..aOS(2, _omitFieldNames ? '' : 'groupName')
    ..aOS(3, _omitFieldNames ? '' : 'description')
    ..aOS(4, _omitFieldNames ? '' : 'avatarUrl')
    ..aOS(5, _omitFieldNames ? '' : 'ownerId')
    ..a<$core.int>(6, _omitFieldNames ? '' : 'memberCount', $pb.PbFieldType.O3)
    ..a<$core.int>(7, _omitFieldNames ? '' : 'maxMembers', $pb.PbFieldType.O3)
    ..aInt64(8, _omitFieldNames ? '' : 'createdAt')
    ..m<$core.String, $core.String>(9, _omitFieldNames ? '' : 'metadata', entryClassName: 'GroupInfo.MetadataEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.chat'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GroupInfo clone() => GroupInfo()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GroupInfo copyWith(void Function(GroupInfo) updates) => super.copyWith((message) => updates(message as GroupInfo)) as GroupInfo;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GroupInfo create() => GroupInfo._();
  GroupInfo createEmptyInstance() => create();
  static $pb.PbList<GroupInfo> createRepeated() => $pb.PbList<GroupInfo>();
  @$core.pragma('dart2js:noInline')
  static GroupInfo getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GroupInfo>(create);
  static GroupInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupName => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupName($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupName() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupName() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get description => $_getSZ(2);
  @$pb.TagNumber(3)
  set description($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasDescription() => $_has(2);
  @$pb.TagNumber(3)
  void clearDescription() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get avatarUrl => $_getSZ(3);
  @$pb.TagNumber(4)
  set avatarUrl($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasAvatarUrl() => $_has(3);
  @$pb.TagNumber(4)
  void clearAvatarUrl() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get ownerId => $_getSZ(4);
  @$pb.TagNumber(5)
  set ownerId($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasOwnerId() => $_has(4);
  @$pb.TagNumber(5)
  void clearOwnerId() => clearField(5);

  @$pb.TagNumber(6)
  $core.int get memberCount => $_getIZ(5);
  @$pb.TagNumber(6)
  set memberCount($core.int v) { $_setSignedInt32(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasMemberCount() => $_has(5);
  @$pb.TagNumber(6)
  void clearMemberCount() => clearField(6);

  @$pb.TagNumber(7)
  $core.int get maxMembers => $_getIZ(6);
  @$pb.TagNumber(7)
  set maxMembers($core.int v) { $_setSignedInt32(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasMaxMembers() => $_has(6);
  @$pb.TagNumber(7)
  void clearMaxMembers() => clearField(7);

  @$pb.TagNumber(8)
  $fixnum.Int64 get createdAt => $_getI64(7);
  @$pb.TagNumber(8)
  set createdAt($fixnum.Int64 v) { $_setInt64(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasCreatedAt() => $_has(7);
  @$pb.TagNumber(8)
  void clearCreatedAt() => clearField(8);

  @$pb.TagNumber(9)
  $core.Map<$core.String, $core.String> get metadata => $_getMap(8);
}

/// Group member information
class GroupMember extends $pb.GeneratedMessage {
  factory GroupMember({
    $core.String? userId,
    $core.String? username,
    $core.String? avatarUrl,
    GroupMemberRole? role,
    $fixnum.Int64? joinedAt,
    $fixnum.Int64? lastReadAt,
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
    if (role != null) {
      $result.role = role;
    }
    if (joinedAt != null) {
      $result.joinedAt = joinedAt;
    }
    if (lastReadAt != null) {
      $result.lastReadAt = lastReadAt;
    }
    return $result;
  }
  GroupMember._() : super();
  factory GroupMember.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GroupMember.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GroupMember', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'username')
    ..aOS(3, _omitFieldNames ? '' : 'avatarUrl')
    ..e<GroupMemberRole>(4, _omitFieldNames ? '' : 'role', $pb.PbFieldType.OE, defaultOrMaker: GroupMemberRole.MEMBER, valueOf: GroupMemberRole.valueOf, enumValues: GroupMemberRole.values)
    ..aInt64(5, _omitFieldNames ? '' : 'joinedAt')
    ..aInt64(6, _omitFieldNames ? '' : 'lastReadAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GroupMember clone() => GroupMember()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GroupMember copyWith(void Function(GroupMember) updates) => super.copyWith((message) => updates(message as GroupMember)) as GroupMember;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GroupMember create() => GroupMember._();
  GroupMember createEmptyInstance() => create();
  static $pb.PbList<GroupMember> createRepeated() => $pb.PbList<GroupMember>();
  @$core.pragma('dart2js:noInline')
  static GroupMember getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GroupMember>(create);
  static GroupMember? _defaultInstance;

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
  GroupMemberRole get role => $_getN(3);
  @$pb.TagNumber(4)
  set role(GroupMemberRole v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasRole() => $_has(3);
  @$pb.TagNumber(4)
  void clearRole() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get joinedAt => $_getI64(4);
  @$pb.TagNumber(5)
  set joinedAt($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasJoinedAt() => $_has(4);
  @$pb.TagNumber(5)
  void clearJoinedAt() => clearField(5);

  @$pb.TagNumber(6)
  $fixnum.Int64 get lastReadAt => $_getI64(5);
  @$pb.TagNumber(6)
  set lastReadAt($fixnum.Int64 v) { $_setInt64(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasLastReadAt() => $_has(5);
  @$pb.TagNumber(6)
  void clearLastReadAt() => clearField(6);
}

/// Join group request
class JoinGroupRequest extends $pb.GeneratedMessage {
  factory JoinGroupRequest({
    $core.String? userId,
    $core.String? groupId,
    $core.String? inviteCode,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (inviteCode != null) {
      $result.inviteCode = inviteCode;
    }
    return $result;
  }
  JoinGroupRequest._() : super();
  factory JoinGroupRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory JoinGroupRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'JoinGroupRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
    ..aOS(3, _omitFieldNames ? '' : 'inviteCode')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  JoinGroupRequest clone() => JoinGroupRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  JoinGroupRequest copyWith(void Function(JoinGroupRequest) updates) => super.copyWith((message) => updates(message as JoinGroupRequest)) as JoinGroupRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static JoinGroupRequest create() => JoinGroupRequest._();
  JoinGroupRequest createEmptyInstance() => create();
  static $pb.PbList<JoinGroupRequest> createRepeated() => $pb.PbList<JoinGroupRequest>();
  @$core.pragma('dart2js:noInline')
  static JoinGroupRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<JoinGroupRequest>(create);
  static JoinGroupRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get inviteCode => $_getSZ(2);
  @$pb.TagNumber(3)
  set inviteCode($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasInviteCode() => $_has(2);
  @$pb.TagNumber(3)
  void clearInviteCode() => clearField(3);
}

class JoinGroupResponse extends $pb.GeneratedMessage {
  factory JoinGroupResponse({
    $0.ErrorCode? code,
    GroupInfo? group,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (group != null) {
      $result.group = group;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  JoinGroupResponse._() : super();
  factory JoinGroupResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory JoinGroupResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'JoinGroupResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<GroupInfo>(2, _omitFieldNames ? '' : 'group', subBuilder: GroupInfo.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  JoinGroupResponse clone() => JoinGroupResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  JoinGroupResponse copyWith(void Function(JoinGroupResponse) updates) => super.copyWith((message) => updates(message as JoinGroupResponse)) as JoinGroupResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static JoinGroupResponse create() => JoinGroupResponse._();
  JoinGroupResponse createEmptyInstance() => create();
  static $pb.PbList<JoinGroupResponse> createRepeated() => $pb.PbList<JoinGroupResponse>();
  @$core.pragma('dart2js:noInline')
  static JoinGroupResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<JoinGroupResponse>(create);
  static JoinGroupResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  GroupInfo get group => $_getN(1);
  @$pb.TagNumber(2)
  set group(GroupInfo v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroup() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroup() => clearField(2);
  @$pb.TagNumber(2)
  GroupInfo ensureGroup() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Leave group request
class LeaveGroupRequest extends $pb.GeneratedMessage {
  factory LeaveGroupRequest({
    $core.String? userId,
    $core.String? groupId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    return $result;
  }
  LeaveGroupRequest._() : super();
  factory LeaveGroupRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory LeaveGroupRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'LeaveGroupRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  LeaveGroupRequest clone() => LeaveGroupRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  LeaveGroupRequest copyWith(void Function(LeaveGroupRequest) updates) => super.copyWith((message) => updates(message as LeaveGroupRequest)) as LeaveGroupRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static LeaveGroupRequest create() => LeaveGroupRequest._();
  LeaveGroupRequest createEmptyInstance() => create();
  static $pb.PbList<LeaveGroupRequest> createRepeated() => $pb.PbList<LeaveGroupRequest>();
  @$core.pragma('dart2js:noInline')
  static LeaveGroupRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<LeaveGroupRequest>(create);
  static LeaveGroupRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);
}

class LeaveGroupResponse extends $pb.GeneratedMessage {
  factory LeaveGroupResponse({
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
  LeaveGroupResponse._() : super();
  factory LeaveGroupResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory LeaveGroupResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'LeaveGroupResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  LeaveGroupResponse clone() => LeaveGroupResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  LeaveGroupResponse copyWith(void Function(LeaveGroupResponse) updates) => super.copyWith((message) => updates(message as LeaveGroupResponse)) as LeaveGroupResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static LeaveGroupResponse create() => LeaveGroupResponse._();
  LeaveGroupResponse createEmptyInstance() => create();
  static $pb.PbList<LeaveGroupResponse> createRepeated() => $pb.PbList<LeaveGroupResponse>();
  @$core.pragma('dart2js:noInline')
  static LeaveGroupResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<LeaveGroupResponse>(create);
  static LeaveGroupResponse? _defaultInstance;

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

/// Kick member from group
class KickMemberRequest extends $pb.GeneratedMessage {
  factory KickMemberRequest({
    $core.String? requesterId,
    $core.String? groupId,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (requesterId != null) {
      $result.requesterId = requesterId;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  KickMemberRequest._() : super();
  factory KickMemberRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory KickMemberRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'KickMemberRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'requesterId')
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
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
  $core.String get requesterId => $_getSZ(0);
  @$pb.TagNumber(1)
  set requesterId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasRequesterId() => $_has(0);
  @$pb.TagNumber(1)
  void clearRequesterId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);

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

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'KickMemberResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
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

/// Get group info
class GetGroupInfoRequest extends $pb.GeneratedMessage {
  factory GetGroupInfoRequest({
    $core.String? groupId,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    return $result;
  }
  GetGroupInfoRequest._() : super();
  factory GetGroupInfoRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetGroupInfoRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetGroupInfoRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetGroupInfoRequest clone() => GetGroupInfoRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetGroupInfoRequest copyWith(void Function(GetGroupInfoRequest) updates) => super.copyWith((message) => updates(message as GetGroupInfoRequest)) as GetGroupInfoRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetGroupInfoRequest create() => GetGroupInfoRequest._();
  GetGroupInfoRequest createEmptyInstance() => create();
  static $pb.PbList<GetGroupInfoRequest> createRepeated() => $pb.PbList<GetGroupInfoRequest>();
  @$core.pragma('dart2js:noInline')
  static GetGroupInfoRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetGroupInfoRequest>(create);
  static GetGroupInfoRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);
}

class GetGroupInfoResponse extends $pb.GeneratedMessage {
  factory GetGroupInfoResponse({
    $0.ErrorCode? code,
    GroupInfo? group,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (group != null) {
      $result.group = group;
    }
    return $result;
  }
  GetGroupInfoResponse._() : super();
  factory GetGroupInfoResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetGroupInfoResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetGroupInfoResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<GroupInfo>(2, _omitFieldNames ? '' : 'group', subBuilder: GroupInfo.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetGroupInfoResponse clone() => GetGroupInfoResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetGroupInfoResponse copyWith(void Function(GetGroupInfoResponse) updates) => super.copyWith((message) => updates(message as GetGroupInfoResponse)) as GetGroupInfoResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetGroupInfoResponse create() => GetGroupInfoResponse._();
  GetGroupInfoResponse createEmptyInstance() => create();
  static $pb.PbList<GetGroupInfoResponse> createRepeated() => $pb.PbList<GetGroupInfoResponse>();
  @$core.pragma('dart2js:noInline')
  static GetGroupInfoResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetGroupInfoResponse>(create);
  static GetGroupInfoResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  GroupInfo get group => $_getN(1);
  @$pb.TagNumber(2)
  set group(GroupInfo v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroup() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroup() => clearField(2);
  @$pb.TagNumber(2)
  GroupInfo ensureGroup() => $_ensure(1);
}

/// Get group members
class GetGroupMembersRequest extends $pb.GeneratedMessage {
  factory GetGroupMembersRequest({
    $core.String? groupId,
    $core.int? limit,
    $core.int? offset,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (limit != null) {
      $result.limit = limit;
    }
    if (offset != null) {
      $result.offset = offset;
    }
    return $result;
  }
  GetGroupMembersRequest._() : super();
  factory GetGroupMembersRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetGroupMembersRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetGroupMembersRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..a<$core.int>(2, _omitFieldNames ? '' : 'limit', $pb.PbFieldType.O3)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'offset', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetGroupMembersRequest clone() => GetGroupMembersRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetGroupMembersRequest copyWith(void Function(GetGroupMembersRequest) updates) => super.copyWith((message) => updates(message as GetGroupMembersRequest)) as GetGroupMembersRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetGroupMembersRequest create() => GetGroupMembersRequest._();
  GetGroupMembersRequest createEmptyInstance() => create();
  static $pb.PbList<GetGroupMembersRequest> createRepeated() => $pb.PbList<GetGroupMembersRequest>();
  @$core.pragma('dart2js:noInline')
  static GetGroupMembersRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetGroupMembersRequest>(create);
  static GetGroupMembersRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

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

class GetGroupMembersResponse extends $pb.GeneratedMessage {
  factory GetGroupMembersResponse({
    $0.ErrorCode? code,
    $core.Iterable<GroupMember>? members,
    $core.int? totalCount,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (members != null) {
      $result.members.addAll(members);
    }
    if (totalCount != null) {
      $result.totalCount = totalCount;
    }
    return $result;
  }
  GetGroupMembersResponse._() : super();
  factory GetGroupMembersResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetGroupMembersResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetGroupMembersResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<GroupMember>(2, _omitFieldNames ? '' : 'members', $pb.PbFieldType.PM, subBuilder: GroupMember.create)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'totalCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetGroupMembersResponse clone() => GetGroupMembersResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetGroupMembersResponse copyWith(void Function(GetGroupMembersResponse) updates) => super.copyWith((message) => updates(message as GetGroupMembersResponse)) as GetGroupMembersResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetGroupMembersResponse create() => GetGroupMembersResponse._();
  GetGroupMembersResponse createEmptyInstance() => create();
  static $pb.PbList<GetGroupMembersResponse> createRepeated() => $pb.PbList<GetGroupMembersResponse>();
  @$core.pragma('dart2js:noInline')
  static GetGroupMembersResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetGroupMembersResponse>(create);
  static GetGroupMembersResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<GroupMember> get members => $_getList(1);

  @$pb.TagNumber(3)
  $core.int get totalCount => $_getIZ(2);
  @$pb.TagNumber(3)
  set totalCount($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTotalCount() => $_has(2);
  @$pb.TagNumber(3)
  void clearTotalCount() => clearField(3);
}

/// Get user's groups
class GetUserGroupsRequest extends $pb.GeneratedMessage {
  factory GetUserGroupsRequest({
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
  GetUserGroupsRequest._() : super();
  factory GetUserGroupsRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUserGroupsRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUserGroupsRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..a<$core.int>(2, _omitFieldNames ? '' : 'limit', $pb.PbFieldType.O3)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'offset', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUserGroupsRequest clone() => GetUserGroupsRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUserGroupsRequest copyWith(void Function(GetUserGroupsRequest) updates) => super.copyWith((message) => updates(message as GetUserGroupsRequest)) as GetUserGroupsRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUserGroupsRequest create() => GetUserGroupsRequest._();
  GetUserGroupsRequest createEmptyInstance() => create();
  static $pb.PbList<GetUserGroupsRequest> createRepeated() => $pb.PbList<GetUserGroupsRequest>();
  @$core.pragma('dart2js:noInline')
  static GetUserGroupsRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUserGroupsRequest>(create);
  static GetUserGroupsRequest? _defaultInstance;

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

class GetUserGroupsResponse extends $pb.GeneratedMessage {
  factory GetUserGroupsResponse({
    $0.ErrorCode? code,
    $core.Iterable<GroupInfo>? groups,
    $core.int? totalCount,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (groups != null) {
      $result.groups.addAll(groups);
    }
    if (totalCount != null) {
      $result.totalCount = totalCount;
    }
    return $result;
  }
  GetUserGroupsResponse._() : super();
  factory GetUserGroupsResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUserGroupsResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUserGroupsResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<GroupInfo>(2, _omitFieldNames ? '' : 'groups', $pb.PbFieldType.PM, subBuilder: GroupInfo.create)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'totalCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUserGroupsResponse clone() => GetUserGroupsResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUserGroupsResponse copyWith(void Function(GetUserGroupsResponse) updates) => super.copyWith((message) => updates(message as GetUserGroupsResponse)) as GetUserGroupsResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUserGroupsResponse create() => GetUserGroupsResponse._();
  GetUserGroupsResponse createEmptyInstance() => create();
  static $pb.PbList<GetUserGroupsResponse> createRepeated() => $pb.PbList<GetUserGroupsResponse>();
  @$core.pragma('dart2js:noInline')
  static GetUserGroupsResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUserGroupsResponse>(create);
  static GetUserGroupsResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<GroupInfo> get groups => $_getList(1);

  @$pb.TagNumber(3)
  $core.int get totalCount => $_getIZ(2);
  @$pb.TagNumber(3)
  set totalCount($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTotalCount() => $_has(2);
  @$pb.TagNumber(3)
  void clearTotalCount() => clearField(3);
}

/// Invite user to group
class InviteToGroupRequest extends $pb.GeneratedMessage {
  factory InviteToGroupRequest({
    $core.String? inviterId,
    $core.String? groupId,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (inviterId != null) {
      $result.inviterId = inviterId;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  InviteToGroupRequest._() : super();
  factory InviteToGroupRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory InviteToGroupRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InviteToGroupRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'inviterId')
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
    ..aOS(3, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  InviteToGroupRequest clone() => InviteToGroupRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  InviteToGroupRequest copyWith(void Function(InviteToGroupRequest) updates) => super.copyWith((message) => updates(message as InviteToGroupRequest)) as InviteToGroupRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InviteToGroupRequest create() => InviteToGroupRequest._();
  InviteToGroupRequest createEmptyInstance() => create();
  static $pb.PbList<InviteToGroupRequest> createRepeated() => $pb.PbList<InviteToGroupRequest>();
  @$core.pragma('dart2js:noInline')
  static InviteToGroupRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<InviteToGroupRequest>(create);
  static InviteToGroupRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get inviterId => $_getSZ(0);
  @$pb.TagNumber(1)
  set inviterId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasInviterId() => $_has(0);
  @$pb.TagNumber(1)
  void clearInviterId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get targetUserId => $_getSZ(2);
  @$pb.TagNumber(3)
  set targetUserId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTargetUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearTargetUserId() => clearField(3);
}

class InviteToGroupResponse extends $pb.GeneratedMessage {
  factory InviteToGroupResponse({
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
  InviteToGroupResponse._() : super();
  factory InviteToGroupResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory InviteToGroupResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'InviteToGroupResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  InviteToGroupResponse clone() => InviteToGroupResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  InviteToGroupResponse copyWith(void Function(InviteToGroupResponse) updates) => super.copyWith((message) => updates(message as InviteToGroupResponse)) as InviteToGroupResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InviteToGroupResponse create() => InviteToGroupResponse._();
  InviteToGroupResponse createEmptyInstance() => create();
  static $pb.PbList<InviteToGroupResponse> createRepeated() => $pb.PbList<InviteToGroupResponse>();
  @$core.pragma('dart2js:noInline')
  static InviteToGroupResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<InviteToGroupResponse>(create);
  static InviteToGroupResponse? _defaultInstance;

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

/// Group notification messages
class GroupCreatedNotify extends $pb.GeneratedMessage {
  factory GroupCreatedNotify({
    GroupInfo? group,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (group != null) {
      $result.group = group;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  GroupCreatedNotify._() : super();
  factory GroupCreatedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GroupCreatedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GroupCreatedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOM<GroupInfo>(1, _omitFieldNames ? '' : 'group', subBuilder: GroupInfo.create)
    ..aInt64(2, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GroupCreatedNotify clone() => GroupCreatedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GroupCreatedNotify copyWith(void Function(GroupCreatedNotify) updates) => super.copyWith((message) => updates(message as GroupCreatedNotify)) as GroupCreatedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GroupCreatedNotify create() => GroupCreatedNotify._();
  GroupCreatedNotify createEmptyInstance() => create();
  static $pb.PbList<GroupCreatedNotify> createRepeated() => $pb.PbList<GroupCreatedNotify>();
  @$core.pragma('dart2js:noInline')
  static GroupCreatedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GroupCreatedNotify>(create);
  static GroupCreatedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  GroupInfo get group => $_getN(0);
  @$pb.TagNumber(1)
  set group(GroupInfo v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroup() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroup() => clearField(1);
  @$pb.TagNumber(1)
  GroupInfo ensureGroup() => $_ensure(0);

  @$pb.TagNumber(2)
  $fixnum.Int64 get timestamp => $_getI64(1);
  @$pb.TagNumber(2)
  set timestamp($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTimestamp() => $_has(1);
  @$pb.TagNumber(2)
  void clearTimestamp() => clearField(2);
}

class GroupMemberJoinedNotify extends $pb.GeneratedMessage {
  factory GroupMemberJoinedNotify({
    $core.String? groupId,
    GroupMember? member,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (member != null) {
      $result.member = member;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  GroupMemberJoinedNotify._() : super();
  factory GroupMemberJoinedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GroupMemberJoinedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GroupMemberJoinedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..aOM<GroupMember>(2, _omitFieldNames ? '' : 'member', subBuilder: GroupMember.create)
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GroupMemberJoinedNotify clone() => GroupMemberJoinedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GroupMemberJoinedNotify copyWith(void Function(GroupMemberJoinedNotify) updates) => super.copyWith((message) => updates(message as GroupMemberJoinedNotify)) as GroupMemberJoinedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GroupMemberJoinedNotify create() => GroupMemberJoinedNotify._();
  GroupMemberJoinedNotify createEmptyInstance() => create();
  static $pb.PbList<GroupMemberJoinedNotify> createRepeated() => $pb.PbList<GroupMemberJoinedNotify>();
  @$core.pragma('dart2js:noInline')
  static GroupMemberJoinedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GroupMemberJoinedNotify>(create);
  static GroupMemberJoinedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

  @$pb.TagNumber(2)
  GroupMember get member => $_getN(1);
  @$pb.TagNumber(2)
  set member(GroupMember v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasMember() => $_has(1);
  @$pb.TagNumber(2)
  void clearMember() => clearField(2);
  @$pb.TagNumber(2)
  GroupMember ensureMember() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

class GroupMemberLeftNotify extends $pb.GeneratedMessage {
  factory GroupMemberLeftNotify({
    $core.String? groupId,
    $core.String? userId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  GroupMemberLeftNotify._() : super();
  factory GroupMemberLeftNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GroupMemberLeftNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GroupMemberLeftNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GroupMemberLeftNotify clone() => GroupMemberLeftNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GroupMemberLeftNotify copyWith(void Function(GroupMemberLeftNotify) updates) => super.copyWith((message) => updates(message as GroupMemberLeftNotify)) as GroupMemberLeftNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GroupMemberLeftNotify create() => GroupMemberLeftNotify._();
  GroupMemberLeftNotify createEmptyInstance() => create();
  static $pb.PbList<GroupMemberLeftNotify> createRepeated() => $pb.PbList<GroupMemberLeftNotify>();
  @$core.pragma('dart2js:noInline')
  static GroupMemberLeftNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GroupMemberLeftNotify>(create);
  static GroupMemberLeftNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

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

class GroupMemberKickedNotify extends $pb.GeneratedMessage {
  factory GroupMemberKickedNotify({
    $core.String? groupId,
    $core.String? userId,
    $core.String? kickedBy,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (kickedBy != null) {
      $result.kickedBy = kickedBy;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  GroupMemberKickedNotify._() : super();
  factory GroupMemberKickedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GroupMemberKickedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GroupMemberKickedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aOS(3, _omitFieldNames ? '' : 'kickedBy')
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GroupMemberKickedNotify clone() => GroupMemberKickedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GroupMemberKickedNotify copyWith(void Function(GroupMemberKickedNotify) updates) => super.copyWith((message) => updates(message as GroupMemberKickedNotify)) as GroupMemberKickedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GroupMemberKickedNotify create() => GroupMemberKickedNotify._();
  GroupMemberKickedNotify createEmptyInstance() => create();
  static $pb.PbList<GroupMemberKickedNotify> createRepeated() => $pb.PbList<GroupMemberKickedNotify>();
  @$core.pragma('dart2js:noInline')
  static GroupMemberKickedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GroupMemberKickedNotify>(create);
  static GroupMemberKickedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get kickedBy => $_getSZ(2);
  @$pb.TagNumber(3)
  set kickedBy($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasKickedBy() => $_has(2);
  @$pb.TagNumber(3)
  void clearKickedBy() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);
}

class GroupUpdatedNotify extends $pb.GeneratedMessage {
  factory GroupUpdatedNotify({
    GroupInfo? group,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (group != null) {
      $result.group = group;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  GroupUpdatedNotify._() : super();
  factory GroupUpdatedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GroupUpdatedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GroupUpdatedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOM<GroupInfo>(1, _omitFieldNames ? '' : 'group', subBuilder: GroupInfo.create)
    ..aInt64(2, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GroupUpdatedNotify clone() => GroupUpdatedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GroupUpdatedNotify copyWith(void Function(GroupUpdatedNotify) updates) => super.copyWith((message) => updates(message as GroupUpdatedNotify)) as GroupUpdatedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GroupUpdatedNotify create() => GroupUpdatedNotify._();
  GroupUpdatedNotify createEmptyInstance() => create();
  static $pb.PbList<GroupUpdatedNotify> createRepeated() => $pb.PbList<GroupUpdatedNotify>();
  @$core.pragma('dart2js:noInline')
  static GroupUpdatedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GroupUpdatedNotify>(create);
  static GroupUpdatedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  GroupInfo get group => $_getN(0);
  @$pb.TagNumber(1)
  set group(GroupInfo v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroup() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroup() => clearField(1);
  @$pb.TagNumber(1)
  GroupInfo ensureGroup() => $_ensure(0);

  @$pb.TagNumber(2)
  $fixnum.Int64 get timestamp => $_getI64(1);
  @$pb.TagNumber(2)
  set timestamp($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTimestamp() => $_has(1);
  @$pb.TagNumber(2)
  void clearTimestamp() => clearField(2);
}

/// Mark message(s) as read
class MarkReadRequest extends $pb.GeneratedMessage {
  factory MarkReadRequest({
    $core.String? userId,
    $core.String? channelId,
    ChannelType? channelType,
    $core.String? messageId,
    $fixnum.Int64? readTimestamp,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (readTimestamp != null) {
      $result.readTimestamp = readTimestamp;
    }
    return $result;
  }
  MarkReadRequest._() : super();
  factory MarkReadRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MarkReadRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MarkReadRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..e<ChannelType>(3, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(4, _omitFieldNames ? '' : 'messageId')
    ..aInt64(5, _omitFieldNames ? '' : 'readTimestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MarkReadRequest clone() => MarkReadRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MarkReadRequest copyWith(void Function(MarkReadRequest) updates) => super.copyWith((message) => updates(message as MarkReadRequest)) as MarkReadRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MarkReadRequest create() => MarkReadRequest._();
  MarkReadRequest createEmptyInstance() => create();
  static $pb.PbList<MarkReadRequest> createRepeated() => $pb.PbList<MarkReadRequest>();
  @$core.pragma('dart2js:noInline')
  static MarkReadRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MarkReadRequest>(create);
  static MarkReadRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  ChannelType get channelType => $_getN(2);
  @$pb.TagNumber(3)
  set channelType(ChannelType v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelType() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelType() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get messageId => $_getSZ(3);
  @$pb.TagNumber(4)
  set messageId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasMessageId() => $_has(3);
  @$pb.TagNumber(4)
  void clearMessageId() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get readTimestamp => $_getI64(4);
  @$pb.TagNumber(5)
  set readTimestamp($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasReadTimestamp() => $_has(4);
  @$pb.TagNumber(5)
  void clearReadTimestamp() => clearField(5);
}

class MarkReadResponse extends $pb.GeneratedMessage {
  factory MarkReadResponse({
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
  MarkReadResponse._() : super();
  factory MarkReadResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MarkReadResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MarkReadResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MarkReadResponse clone() => MarkReadResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MarkReadResponse copyWith(void Function(MarkReadResponse) updates) => super.copyWith((message) => updates(message as MarkReadResponse)) as MarkReadResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MarkReadResponse create() => MarkReadResponse._();
  MarkReadResponse createEmptyInstance() => create();
  static $pb.PbList<MarkReadResponse> createRepeated() => $pb.PbList<MarkReadResponse>();
  @$core.pragma('dart2js:noInline')
  static MarkReadResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MarkReadResponse>(create);
  static MarkReadResponse? _defaultInstance;

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

/// Read receipt information
class ReadReceipt extends $pb.GeneratedMessage {
  factory ReadReceipt({
    $core.String? userId,
    $core.String? messageId,
    $fixnum.Int64? readAt,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (readAt != null) {
      $result.readAt = readAt;
    }
    return $result;
  }
  ReadReceipt._() : super();
  factory ReadReceipt.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ReadReceipt.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ReadReceipt', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'messageId')
    ..aInt64(3, _omitFieldNames ? '' : 'readAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ReadReceipt clone() => ReadReceipt()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ReadReceipt copyWith(void Function(ReadReceipt) updates) => super.copyWith((message) => updates(message as ReadReceipt)) as ReadReceipt;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ReadReceipt create() => ReadReceipt._();
  ReadReceipt createEmptyInstance() => create();
  static $pb.PbList<ReadReceipt> createRepeated() => $pb.PbList<ReadReceipt>();
  @$core.pragma('dart2js:noInline')
  static ReadReceipt getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ReadReceipt>(create);
  static ReadReceipt? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get messageId => $_getSZ(1);
  @$pb.TagNumber(2)
  set messageId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasMessageId() => $_has(1);
  @$pb.TagNumber(2)
  void clearMessageId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get readAt => $_getI64(2);
  @$pb.TagNumber(3)
  set readAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasReadAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearReadAt() => clearField(3);
}

/// Get read receipts for a message
class GetReadReceiptsRequest extends $pb.GeneratedMessage {
  factory GetReadReceiptsRequest({
    $core.String? messageId,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    return $result;
  }
  GetReadReceiptsRequest._() : super();
  factory GetReadReceiptsRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetReadReceiptsRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetReadReceiptsRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetReadReceiptsRequest clone() => GetReadReceiptsRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetReadReceiptsRequest copyWith(void Function(GetReadReceiptsRequest) updates) => super.copyWith((message) => updates(message as GetReadReceiptsRequest)) as GetReadReceiptsRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetReadReceiptsRequest create() => GetReadReceiptsRequest._();
  GetReadReceiptsRequest createEmptyInstance() => create();
  static $pb.PbList<GetReadReceiptsRequest> createRepeated() => $pb.PbList<GetReadReceiptsRequest>();
  @$core.pragma('dart2js:noInline')
  static GetReadReceiptsRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetReadReceiptsRequest>(create);
  static GetReadReceiptsRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);
}

class GetReadReceiptsResponse extends $pb.GeneratedMessage {
  factory GetReadReceiptsResponse({
    $0.ErrorCode? code,
    $core.Iterable<ReadReceipt>? receipts,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (receipts != null) {
      $result.receipts.addAll(receipts);
    }
    return $result;
  }
  GetReadReceiptsResponse._() : super();
  factory GetReadReceiptsResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetReadReceiptsResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetReadReceiptsResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<ReadReceipt>(2, _omitFieldNames ? '' : 'receipts', $pb.PbFieldType.PM, subBuilder: ReadReceipt.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetReadReceiptsResponse clone() => GetReadReceiptsResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetReadReceiptsResponse copyWith(void Function(GetReadReceiptsResponse) updates) => super.copyWith((message) => updates(message as GetReadReceiptsResponse)) as GetReadReceiptsResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetReadReceiptsResponse create() => GetReadReceiptsResponse._();
  GetReadReceiptsResponse createEmptyInstance() => create();
  static $pb.PbList<GetReadReceiptsResponse> createRepeated() => $pb.PbList<GetReadReceiptsResponse>();
  @$core.pragma('dart2js:noInline')
  static GetReadReceiptsResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetReadReceiptsResponse>(create);
  static GetReadReceiptsResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<ReadReceipt> get receipts => $_getList(1);
}

/// Get unread count
class GetUnreadCountRequest extends $pb.GeneratedMessage {
  factory GetUnreadCountRequest({
    $core.String? userId,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetUnreadCountRequest._() : super();
  factory GetUnreadCountRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUnreadCountRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUnreadCountRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUnreadCountRequest clone() => GetUnreadCountRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUnreadCountRequest copyWith(void Function(GetUnreadCountRequest) updates) => super.copyWith((message) => updates(message as GetUnreadCountRequest)) as GetUnreadCountRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUnreadCountRequest create() => GetUnreadCountRequest._();
  GetUnreadCountRequest createEmptyInstance() => create();
  static $pb.PbList<GetUnreadCountRequest> createRepeated() => $pb.PbList<GetUnreadCountRequest>();
  @$core.pragma('dart2js:noInline')
  static GetUnreadCountRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUnreadCountRequest>(create);
  static GetUnreadCountRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);
}

class GetUnreadCountResponse_ChannelUnread extends $pb.GeneratedMessage {
  factory GetUnreadCountResponse_ChannelUnread({
    $core.String? channelId,
    ChannelType? channelType,
    $core.int? count,
    $core.String? lastMessageId,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (count != null) {
      $result.count = count;
    }
    if (lastMessageId != null) {
      $result.lastMessageId = lastMessageId;
    }
    return $result;
  }
  GetUnreadCountResponse_ChannelUnread._() : super();
  factory GetUnreadCountResponse_ChannelUnread.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUnreadCountResponse_ChannelUnread.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUnreadCountResponse.ChannelUnread', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..a<$core.int>(3, _omitFieldNames ? '' : 'count', $pb.PbFieldType.O3)
    ..aOS(4, _omitFieldNames ? '' : 'lastMessageId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUnreadCountResponse_ChannelUnread clone() => GetUnreadCountResponse_ChannelUnread()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUnreadCountResponse_ChannelUnread copyWith(void Function(GetUnreadCountResponse_ChannelUnread) updates) => super.copyWith((message) => updates(message as GetUnreadCountResponse_ChannelUnread)) as GetUnreadCountResponse_ChannelUnread;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUnreadCountResponse_ChannelUnread create() => GetUnreadCountResponse_ChannelUnread._();
  GetUnreadCountResponse_ChannelUnread createEmptyInstance() => create();
  static $pb.PbList<GetUnreadCountResponse_ChannelUnread> createRepeated() => $pb.PbList<GetUnreadCountResponse_ChannelUnread>();
  @$core.pragma('dart2js:noInline')
  static GetUnreadCountResponse_ChannelUnread getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUnreadCountResponse_ChannelUnread>(create);
  static GetUnreadCountResponse_ChannelUnread? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get count => $_getIZ(2);
  @$pb.TagNumber(3)
  set count($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasCount() => $_has(2);
  @$pb.TagNumber(3)
  void clearCount() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get lastMessageId => $_getSZ(3);
  @$pb.TagNumber(4)
  set lastMessageId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasLastMessageId() => $_has(3);
  @$pb.TagNumber(4)
  void clearLastMessageId() => clearField(4);
}

class GetUnreadCountResponse extends $pb.GeneratedMessage {
  factory GetUnreadCountResponse({
    $0.ErrorCode? code,
    $core.int? totalUnread,
    $core.Iterable<GetUnreadCountResponse_ChannelUnread>? channels,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (totalUnread != null) {
      $result.totalUnread = totalUnread;
    }
    if (channels != null) {
      $result.channels.addAll(channels);
    }
    return $result;
  }
  GetUnreadCountResponse._() : super();
  factory GetUnreadCountResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetUnreadCountResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetUnreadCountResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..a<$core.int>(2, _omitFieldNames ? '' : 'totalUnread', $pb.PbFieldType.O3)
    ..pc<GetUnreadCountResponse_ChannelUnread>(3, _omitFieldNames ? '' : 'channels', $pb.PbFieldType.PM, subBuilder: GetUnreadCountResponse_ChannelUnread.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetUnreadCountResponse clone() => GetUnreadCountResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetUnreadCountResponse copyWith(void Function(GetUnreadCountResponse) updates) => super.copyWith((message) => updates(message as GetUnreadCountResponse)) as GetUnreadCountResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetUnreadCountResponse create() => GetUnreadCountResponse._();
  GetUnreadCountResponse createEmptyInstance() => create();
  static $pb.PbList<GetUnreadCountResponse> createRepeated() => $pb.PbList<GetUnreadCountResponse>();
  @$core.pragma('dart2js:noInline')
  static GetUnreadCountResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetUnreadCountResponse>(create);
  static GetUnreadCountResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.int get totalUnread => $_getIZ(1);
  @$pb.TagNumber(2)
  set totalUnread($core.int v) { $_setSignedInt32(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTotalUnread() => $_has(1);
  @$pb.TagNumber(2)
  void clearTotalUnread() => clearField(2);

  @$pb.TagNumber(3)
  $core.List<GetUnreadCountResponse_ChannelUnread> get channels => $_getList(2);
}

/// Read notification (sent to sender)
class MessageReadNotify extends $pb.GeneratedMessage {
  factory MessageReadNotify({
    $core.String? channelId,
    ChannelType? channelType,
    $core.String? messageId,
    $core.String? readerUserId,
    $fixnum.Int64? readAt,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (readerUserId != null) {
      $result.readerUserId = readerUserId;
    }
    if (readAt != null) {
      $result.readAt = readAt;
    }
    return $result;
  }
  MessageReadNotify._() : super();
  factory MessageReadNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageReadNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageReadNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(3, _omitFieldNames ? '' : 'messageId')
    ..aOS(4, _omitFieldNames ? '' : 'readerUserId')
    ..aInt64(5, _omitFieldNames ? '' : 'readAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageReadNotify clone() => MessageReadNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageReadNotify copyWith(void Function(MessageReadNotify) updates) => super.copyWith((message) => updates(message as MessageReadNotify)) as MessageReadNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageReadNotify create() => MessageReadNotify._();
  MessageReadNotify createEmptyInstance() => create();
  static $pb.PbList<MessageReadNotify> createRepeated() => $pb.PbList<MessageReadNotify>();
  @$core.pragma('dart2js:noInline')
  static MessageReadNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageReadNotify>(create);
  static MessageReadNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get messageId => $_getSZ(2);
  @$pb.TagNumber(3)
  set messageId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMessageId() => $_has(2);
  @$pb.TagNumber(3)
  void clearMessageId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get readerUserId => $_getSZ(3);
  @$pb.TagNumber(4)
  set readerUserId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasReaderUserId() => $_has(3);
  @$pb.TagNumber(4)
  void clearReaderUserId() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get readAt => $_getI64(4);
  @$pb.TagNumber(5)
  set readAt($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasReadAt() => $_has(4);
  @$pb.TagNumber(5)
  void clearReadAt() => clearField(5);
}

class TypingIndicatorState extends $pb.GeneratedMessage {
  factory TypingIndicatorState({
    $core.String? channelId,
    ChannelType? channelType,
    $core.String? userId,
    $core.bool? isTyping,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (isTyping != null) {
      $result.isTyping = isTyping;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  TypingIndicatorState._() : super();
  factory TypingIndicatorState.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory TypingIndicatorState.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'TypingIndicatorState', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(3, _omitFieldNames ? '' : 'userId')
    ..aOB(4, _omitFieldNames ? '' : 'isTyping')
    ..aInt64(5, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  TypingIndicatorState clone() => TypingIndicatorState()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  TypingIndicatorState copyWith(void Function(TypingIndicatorState) updates) => super.copyWith((message) => updates(message as TypingIndicatorState)) as TypingIndicatorState;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TypingIndicatorState create() => TypingIndicatorState._();
  TypingIndicatorState createEmptyInstance() => create();
  static $pb.PbList<TypingIndicatorState> createRepeated() => $pb.PbList<TypingIndicatorState>();
  @$core.pragma('dart2js:noInline')
  static TypingIndicatorState getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<TypingIndicatorState>(create);
  static TypingIndicatorState? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get userId => $_getSZ(2);
  @$pb.TagNumber(3)
  set userId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearUserId() => clearField(3);

  @$pb.TagNumber(4)
  $core.bool get isTyping => $_getBF(3);
  @$pb.TagNumber(4)
  set isTyping($core.bool v) { $_setBool(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasIsTyping() => $_has(3);
  @$pb.TagNumber(4)
  void clearIsTyping() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get timestamp => $_getI64(4);
  @$pb.TagNumber(5)
  set timestamp($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasTimestamp() => $_has(4);
  @$pb.TagNumber(5)
  void clearTimestamp() => clearField(5);
}

/// Message acknowledgment
class MessageAck extends $pb.GeneratedMessage {
  factory MessageAck({
    $core.String? messageId,
    $core.String? userId,
    $fixnum.Int64? receivedAt,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (receivedAt != null) {
      $result.receivedAt = receivedAt;
    }
    return $result;
  }
  MessageAck._() : super();
  factory MessageAck.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageAck.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageAck', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aInt64(3, _omitFieldNames ? '' : 'receivedAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageAck clone() => MessageAck()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageAck copyWith(void Function(MessageAck) updates) => super.copyWith((message) => updates(message as MessageAck)) as MessageAck;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageAck create() => MessageAck._();
  MessageAck createEmptyInstance() => create();
  static $pb.PbList<MessageAck> createRepeated() => $pb.PbList<MessageAck>();
  @$core.pragma('dart2js:noInline')
  static MessageAck getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageAck>(create);
  static MessageAck? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get receivedAt => $_getI64(2);
  @$pb.TagNumber(3)
  set receivedAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasReceivedAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearReceivedAt() => clearField(3);
}

/// Message negative acknowledgment (delivery failed)
class MessageNack extends $pb.GeneratedMessage {
  factory MessageNack({
    $core.String? messageId,
    $core.String? userId,
    $0.ErrorCode? errorCode,
    $core.String? errorMessage,
    $fixnum.Int64? failedAt,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (errorCode != null) {
      $result.errorCode = errorCode;
    }
    if (errorMessage != null) {
      $result.errorMessage = errorMessage;
    }
    if (failedAt != null) {
      $result.failedAt = failedAt;
    }
    return $result;
  }
  MessageNack._() : super();
  factory MessageNack.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageNack.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageNack', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..e<$0.ErrorCode>(3, _omitFieldNames ? '' : 'errorCode', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(4, _omitFieldNames ? '' : 'errorMessage')
    ..aInt64(5, _omitFieldNames ? '' : 'failedAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageNack clone() => MessageNack()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageNack copyWith(void Function(MessageNack) updates) => super.copyWith((message) => updates(message as MessageNack)) as MessageNack;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageNack create() => MessageNack._();
  MessageNack createEmptyInstance() => create();
  static $pb.PbList<MessageNack> createRepeated() => $pb.PbList<MessageNack>();
  @$core.pragma('dart2js:noInline')
  static MessageNack getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageNack>(create);
  static MessageNack? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $0.ErrorCode get errorCode => $_getN(2);
  @$pb.TagNumber(3)
  set errorCode($0.ErrorCode v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasErrorCode() => $_has(2);
  @$pb.TagNumber(3)
  void clearErrorCode() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get errorMessage => $_getSZ(3);
  @$pb.TagNumber(4)
  set errorMessage($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasErrorMessage() => $_has(3);
  @$pb.TagNumber(4)
  void clearErrorMessage() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get failedAt => $_getI64(4);
  @$pb.TagNumber(5)
  set failedAt($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasFailedAt() => $_has(4);
  @$pb.TagNumber(5)
  void clearFailedAt() => clearField(5);
}

/// Delivery status
class DeliveryStatus extends $pb.GeneratedMessage {
  factory DeliveryStatus({
    $core.String? messageId,
    $core.String? userId,
    DeliveryStatus_Status? status,
    $fixnum.Int64? timestamp,
    $core.int? retryCount,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (status != null) {
      $result.status = status;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    if (retryCount != null) {
      $result.retryCount = retryCount;
    }
    return $result;
  }
  DeliveryStatus._() : super();
  factory DeliveryStatus.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeliveryStatus.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeliveryStatus', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..e<DeliveryStatus_Status>(3, _omitFieldNames ? '' : 'status', $pb.PbFieldType.OE, defaultOrMaker: DeliveryStatus_Status.PENDING, valueOf: DeliveryStatus_Status.valueOf, enumValues: DeliveryStatus_Status.values)
    ..aInt64(4, _omitFieldNames ? '' : 'timestamp')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'retryCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeliveryStatus clone() => DeliveryStatus()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeliveryStatus copyWith(void Function(DeliveryStatus) updates) => super.copyWith((message) => updates(message as DeliveryStatus)) as DeliveryStatus;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeliveryStatus create() => DeliveryStatus._();
  DeliveryStatus createEmptyInstance() => create();
  static $pb.PbList<DeliveryStatus> createRepeated() => $pb.PbList<DeliveryStatus>();
  @$core.pragma('dart2js:noInline')
  static DeliveryStatus getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeliveryStatus>(create);
  static DeliveryStatus? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  DeliveryStatus_Status get status => $_getN(2);
  @$pb.TagNumber(3)
  set status(DeliveryStatus_Status v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasStatus() => $_has(2);
  @$pb.TagNumber(3)
  void clearStatus() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get timestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set timestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearTimestamp() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get retryCount => $_getIZ(4);
  @$pb.TagNumber(5)
  set retryCount($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasRetryCount() => $_has(4);
  @$pb.TagNumber(5)
  void clearRetryCount() => clearField(5);
}

/// Pagination token for cursor-based pagination
class PaginationToken extends $pb.GeneratedMessage {
  factory PaginationToken({
    $core.String? cursor,
    $fixnum.Int64? timestamp,
    $core.int? pageSize,
  }) {
    final $result = create();
    if (cursor != null) {
      $result.cursor = cursor;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    if (pageSize != null) {
      $result.pageSize = pageSize;
    }
    return $result;
  }
  PaginationToken._() : super();
  factory PaginationToken.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PaginationToken.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PaginationToken', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'cursor')
    ..aInt64(2, _omitFieldNames ? '' : 'timestamp')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'pageSize', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PaginationToken clone() => PaginationToken()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PaginationToken copyWith(void Function(PaginationToken) updates) => super.copyWith((message) => updates(message as PaginationToken)) as PaginationToken;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PaginationToken create() => PaginationToken._();
  PaginationToken createEmptyInstance() => create();
  static $pb.PbList<PaginationToken> createRepeated() => $pb.PbList<PaginationToken>();
  @$core.pragma('dart2js:noInline')
  static PaginationToken getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PaginationToken>(create);
  static PaginationToken? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get cursor => $_getSZ(0);
  @$pb.TagNumber(1)
  set cursor($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasCursor() => $_has(0);
  @$pb.TagNumber(1)
  void clearCursor() => clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get timestamp => $_getI64(1);
  @$pb.TagNumber(2)
  set timestamp($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTimestamp() => $_has(1);
  @$pb.TagNumber(2)
  void clearTimestamp() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get pageSize => $_getIZ(2);
  @$pb.TagNumber(3)
  set pageSize($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasPageSize() => $_has(2);
  @$pb.TagNumber(3)
  void clearPageSize() => clearField(3);
}

/// Enhanced history request with pagination
class GetHistoryRequestV2 extends $pb.GeneratedMessage {
  factory GetHistoryRequestV2({
    $core.String? userId,
    ChannelType? channelType,
    $core.String? channelId,
    PaginationToken? pagination,
    $core.int? limit,
    $core.bool? includeDeleted,
    $fixnum.Int64? sinceTimestamp,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (pagination != null) {
      $result.pagination = pagination;
    }
    if (limit != null) {
      $result.limit = limit;
    }
    if (includeDeleted != null) {
      $result.includeDeleted = includeDeleted;
    }
    if (sinceTimestamp != null) {
      $result.sinceTimestamp = sinceTimestamp;
    }
    return $result;
  }
  GetHistoryRequestV2._() : super();
  factory GetHistoryRequestV2.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetHistoryRequestV2.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetHistoryRequestV2', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(3, _omitFieldNames ? '' : 'channelId')
    ..aOM<PaginationToken>(4, _omitFieldNames ? '' : 'pagination', subBuilder: PaginationToken.create)
    ..a<$core.int>(5, _omitFieldNames ? '' : 'limit', $pb.PbFieldType.O3)
    ..aOB(6, _omitFieldNames ? '' : 'includeDeleted')
    ..aInt64(7, _omitFieldNames ? '' : 'sinceTimestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetHistoryRequestV2 clone() => GetHistoryRequestV2()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetHistoryRequestV2 copyWith(void Function(GetHistoryRequestV2) updates) => super.copyWith((message) => updates(message as GetHistoryRequestV2)) as GetHistoryRequestV2;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetHistoryRequestV2 create() => GetHistoryRequestV2._();
  GetHistoryRequestV2 createEmptyInstance() => create();
  static $pb.PbList<GetHistoryRequestV2> createRepeated() => $pb.PbList<GetHistoryRequestV2>();
  @$core.pragma('dart2js:noInline')
  static GetHistoryRequestV2 getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetHistoryRequestV2>(create);
  static GetHistoryRequestV2? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get channelId => $_getSZ(2);
  @$pb.TagNumber(3)
  set channelId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelId() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelId() => clearField(3);

  @$pb.TagNumber(4)
  PaginationToken get pagination => $_getN(3);
  @$pb.TagNumber(4)
  set pagination(PaginationToken v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasPagination() => $_has(3);
  @$pb.TagNumber(4)
  void clearPagination() => clearField(4);
  @$pb.TagNumber(4)
  PaginationToken ensurePagination() => $_ensure(3);

  @$pb.TagNumber(5)
  $core.int get limit => $_getIZ(4);
  @$pb.TagNumber(5)
  set limit($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasLimit() => $_has(4);
  @$pb.TagNumber(5)
  void clearLimit() => clearField(5);

  @$pb.TagNumber(6)
  $core.bool get includeDeleted => $_getBF(5);
  @$pb.TagNumber(6)
  set includeDeleted($core.bool v) { $_setBool(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasIncludeDeleted() => $_has(5);
  @$pb.TagNumber(6)
  void clearIncludeDeleted() => clearField(6);

  @$pb.TagNumber(7)
  $fixnum.Int64 get sinceTimestamp => $_getI64(6);
  @$pb.TagNumber(7)
  set sinceTimestamp($fixnum.Int64 v) { $_setInt64(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasSinceTimestamp() => $_has(6);
  @$pb.TagNumber(7)
  void clearSinceTimestamp() => clearField(7);
}

/// Enhanced history response
class GetHistoryResponseV2 extends $pb.GeneratedMessage {
  factory GetHistoryResponseV2({
    $0.ErrorCode? code,
    $core.Iterable<ChatMessage>? messages,
    PaginationToken? nextPage,
    $core.bool? hasMore,
    $core.int? totalCount,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (messages != null) {
      $result.messages.addAll(messages);
    }
    if (nextPage != null) {
      $result.nextPage = nextPage;
    }
    if (hasMore != null) {
      $result.hasMore = hasMore;
    }
    if (totalCount != null) {
      $result.totalCount = totalCount;
    }
    return $result;
  }
  GetHistoryResponseV2._() : super();
  factory GetHistoryResponseV2.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetHistoryResponseV2.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetHistoryResponseV2', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<ChatMessage>(2, _omitFieldNames ? '' : 'messages', $pb.PbFieldType.PM, subBuilder: ChatMessage.create)
    ..aOM<PaginationToken>(3, _omitFieldNames ? '' : 'nextPage', subBuilder: PaginationToken.create)
    ..aOB(4, _omitFieldNames ? '' : 'hasMore')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'totalCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetHistoryResponseV2 clone() => GetHistoryResponseV2()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetHistoryResponseV2 copyWith(void Function(GetHistoryResponseV2) updates) => super.copyWith((message) => updates(message as GetHistoryResponseV2)) as GetHistoryResponseV2;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetHistoryResponseV2 create() => GetHistoryResponseV2._();
  GetHistoryResponseV2 createEmptyInstance() => create();
  static $pb.PbList<GetHistoryResponseV2> createRepeated() => $pb.PbList<GetHistoryResponseV2>();
  @$core.pragma('dart2js:noInline')
  static GetHistoryResponseV2 getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetHistoryResponseV2>(create);
  static GetHistoryResponseV2? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<ChatMessage> get messages => $_getList(1);

  @$pb.TagNumber(3)
  PaginationToken get nextPage => $_getN(2);
  @$pb.TagNumber(3)
  set nextPage(PaginationToken v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasNextPage() => $_has(2);
  @$pb.TagNumber(3)
  void clearNextPage() => clearField(3);
  @$pb.TagNumber(3)
  PaginationToken ensureNextPage() => $_ensure(2);

  @$pb.TagNumber(4)
  $core.bool get hasMore => $_getBF(3);
  @$pb.TagNumber(4)
  set hasMore($core.bool v) { $_setBool(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasHasMore() => $_has(3);
  @$pb.TagNumber(4)
  void clearHasMore() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get totalCount => $_getIZ(4);
  @$pb.TagNumber(5)
  set totalCount($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasTotalCount() => $_has(4);
  @$pb.TagNumber(5)
  void clearTotalCount() => clearField(5);
}

/// Message delivery tracking request
class TrackMessageRequest extends $pb.GeneratedMessage {
  factory TrackMessageRequest({
    $core.String? messageId,
    $core.String? receiverId,
    $fixnum.Int64? expiresAt,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (receiverId != null) {
      $result.receiverId = receiverId;
    }
    if (expiresAt != null) {
      $result.expiresAt = expiresAt;
    }
    return $result;
  }
  TrackMessageRequest._() : super();
  factory TrackMessageRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory TrackMessageRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'TrackMessageRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'receiverId')
    ..aInt64(3, _omitFieldNames ? '' : 'expiresAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  TrackMessageRequest clone() => TrackMessageRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  TrackMessageRequest copyWith(void Function(TrackMessageRequest) updates) => super.copyWith((message) => updates(message as TrackMessageRequest)) as TrackMessageRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TrackMessageRequest create() => TrackMessageRequest._();
  TrackMessageRequest createEmptyInstance() => create();
  static $pb.PbList<TrackMessageRequest> createRepeated() => $pb.PbList<TrackMessageRequest>();
  @$core.pragma('dart2js:noInline')
  static TrackMessageRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<TrackMessageRequest>(create);
  static TrackMessageRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get receiverId => $_getSZ(1);
  @$pb.TagNumber(2)
  set receiverId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasReceiverId() => $_has(1);
  @$pb.TagNumber(2)
  void clearReceiverId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get expiresAt => $_getI64(2);
  @$pb.TagNumber(3)
  set expiresAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasExpiresAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearExpiresAt() => clearField(3);
}

/// Message delivery tracking response
class TrackMessageResponse extends $pb.GeneratedMessage {
  factory TrackMessageResponse({
    $0.ErrorCode? code,
    $core.String? trackingId,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (trackingId != null) {
      $result.trackingId = trackingId;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  TrackMessageResponse._() : super();
  factory TrackMessageResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory TrackMessageResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'TrackMessageResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'trackingId')
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  TrackMessageResponse clone() => TrackMessageResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  TrackMessageResponse copyWith(void Function(TrackMessageResponse) updates) => super.copyWith((message) => updates(message as TrackMessageResponse)) as TrackMessageResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TrackMessageResponse create() => TrackMessageResponse._();
  TrackMessageResponse createEmptyInstance() => create();
  static $pb.PbList<TrackMessageResponse> createRepeated() => $pb.PbList<TrackMessageResponse>();
  @$core.pragma('dart2js:noInline')
  static TrackMessageResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<TrackMessageResponse>(create);
  static TrackMessageResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get trackingId => $_getSZ(1);
  @$pb.TagNumber(2)
  set trackingId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTrackingId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTrackingId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Channel permissions
class ChannelPermissions extends $pb.GeneratedMessage {
  factory ChannelPermissions({
    $core.bool? canRead,
    $core.bool? canWrite,
    $core.bool? canSpeak,
    $core.bool? canJoin,
    $core.bool? canManage,
  }) {
    final $result = create();
    if (canRead != null) {
      $result.canRead = canRead;
    }
    if (canWrite != null) {
      $result.canWrite = canWrite;
    }
    if (canSpeak != null) {
      $result.canSpeak = canSpeak;
    }
    if (canJoin != null) {
      $result.canJoin = canJoin;
    }
    if (canManage != null) {
      $result.canManage = canManage;
    }
    return $result;
  }
  ChannelPermissions._() : super();
  factory ChannelPermissions.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelPermissions.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelPermissions', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOB(1, _omitFieldNames ? '' : 'canRead')
    ..aOB(2, _omitFieldNames ? '' : 'canWrite')
    ..aOB(3, _omitFieldNames ? '' : 'canSpeak')
    ..aOB(4, _omitFieldNames ? '' : 'canJoin')
    ..aOB(5, _omitFieldNames ? '' : 'canManage')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelPermissions clone() => ChannelPermissions()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelPermissions copyWith(void Function(ChannelPermissions) updates) => super.copyWith((message) => updates(message as ChannelPermissions)) as ChannelPermissions;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelPermissions create() => ChannelPermissions._();
  ChannelPermissions createEmptyInstance() => create();
  static $pb.PbList<ChannelPermissions> createRepeated() => $pb.PbList<ChannelPermissions>();
  @$core.pragma('dart2js:noInline')
  static ChannelPermissions getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelPermissions>(create);
  static ChannelPermissions? _defaultInstance;

  @$pb.TagNumber(1)
  $core.bool get canRead => $_getBF(0);
  @$pb.TagNumber(1)
  set canRead($core.bool v) { $_setBool(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasCanRead() => $_has(0);
  @$pb.TagNumber(1)
  void clearCanRead() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get canWrite => $_getBF(1);
  @$pb.TagNumber(2)
  set canWrite($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasCanWrite() => $_has(1);
  @$pb.TagNumber(2)
  void clearCanWrite() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get canSpeak => $_getBF(2);
  @$pb.TagNumber(3)
  set canSpeak($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasCanSpeak() => $_has(2);
  @$pb.TagNumber(3)
  void clearCanSpeak() => clearField(3);

  @$pb.TagNumber(4)
  $core.bool get canJoin => $_getBF(3);
  @$pb.TagNumber(4)
  set canJoin($core.bool v) { $_setBool(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasCanJoin() => $_has(3);
  @$pb.TagNumber(4)
  void clearCanJoin() => clearField(4);

  @$pb.TagNumber(5)
  $core.bool get canManage => $_getBF(4);
  @$pb.TagNumber(5)
  set canManage($core.bool v) { $_setBool(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasCanManage() => $_has(4);
  @$pb.TagNumber(5)
  void clearCanManage() => clearField(5);
}

/// Permission override for a role/user
class PermissionOverrideEntry extends $pb.GeneratedMessage {
  factory PermissionOverrideEntry({
    PermissionType? type,
    $core.String? id,
    ChannelPermissions? permissions,
    PermissionOverride? allow,
    PermissionOverride? deny,
  }) {
    final $result = create();
    if (type != null) {
      $result.type = type;
    }
    if (id != null) {
      $result.id = id;
    }
    if (permissions != null) {
      $result.permissions = permissions;
    }
    if (allow != null) {
      $result.allow = allow;
    }
    if (deny != null) {
      $result.deny = deny;
    }
    return $result;
  }
  PermissionOverrideEntry._() : super();
  factory PermissionOverrideEntry.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PermissionOverrideEntry.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PermissionOverrideEntry', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<PermissionType>(1, _omitFieldNames ? '' : 'type', $pb.PbFieldType.OE, defaultOrMaker: PermissionType.PERMISSION_TYPE_ROLE, valueOf: PermissionType.valueOf, enumValues: PermissionType.values)
    ..aOS(2, _omitFieldNames ? '' : 'id')
    ..aOM<ChannelPermissions>(3, _omitFieldNames ? '' : 'permissions', subBuilder: ChannelPermissions.create)
    ..e<PermissionOverride>(4, _omitFieldNames ? '' : 'allow', $pb.PbFieldType.OE, defaultOrMaker: PermissionOverride.INHERIT, valueOf: PermissionOverride.valueOf, enumValues: PermissionOverride.values)
    ..e<PermissionOverride>(5, _omitFieldNames ? '' : 'deny', $pb.PbFieldType.OE, defaultOrMaker: PermissionOverride.INHERIT, valueOf: PermissionOverride.valueOf, enumValues: PermissionOverride.values)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PermissionOverrideEntry clone() => PermissionOverrideEntry()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PermissionOverrideEntry copyWith(void Function(PermissionOverrideEntry) updates) => super.copyWith((message) => updates(message as PermissionOverrideEntry)) as PermissionOverrideEntry;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PermissionOverrideEntry create() => PermissionOverrideEntry._();
  PermissionOverrideEntry createEmptyInstance() => create();
  static $pb.PbList<PermissionOverrideEntry> createRepeated() => $pb.PbList<PermissionOverrideEntry>();
  @$core.pragma('dart2js:noInline')
  static PermissionOverrideEntry getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PermissionOverrideEntry>(create);
  static PermissionOverrideEntry? _defaultInstance;

  @$pb.TagNumber(1)
  PermissionType get type => $_getN(0);
  @$pb.TagNumber(1)
  set type(PermissionType v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasType() => $_has(0);
  @$pb.TagNumber(1)
  void clearType() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get id => $_getSZ(1);
  @$pb.TagNumber(2)
  set id($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasId() => $_has(1);
  @$pb.TagNumber(2)
  void clearId() => clearField(2);

  @$pb.TagNumber(3)
  ChannelPermissions get permissions => $_getN(2);
  @$pb.TagNumber(3)
  set permissions(ChannelPermissions v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasPermissions() => $_has(2);
  @$pb.TagNumber(3)
  void clearPermissions() => clearField(3);
  @$pb.TagNumber(3)
  ChannelPermissions ensurePermissions() => $_ensure(2);

  @$pb.TagNumber(4)
  PermissionOverride get allow => $_getN(3);
  @$pb.TagNumber(4)
  set allow(PermissionOverride v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasAllow() => $_has(3);
  @$pb.TagNumber(4)
  void clearAllow() => clearField(4);

  @$pb.TagNumber(5)
  PermissionOverride get deny => $_getN(4);
  @$pb.TagNumber(5)
  set deny(PermissionOverride v) { setField(5, v); }
  @$pb.TagNumber(5)
  $core.bool hasDeny() => $_has(4);
  @$pb.TagNumber(5)
  void clearDeny() => clearField(5);
}

/// Channel category (for organizing channels)
class ChannelCategory extends $pb.GeneratedMessage {
  factory ChannelCategory({
    $core.String? categoryId,
    $core.String? groupId,
    $core.String? name,
    $core.int? position,
    $core.bool? isCollapsed,
    $fixnum.Int64? createdAt,
  }) {
    final $result = create();
    if (categoryId != null) {
      $result.categoryId = categoryId;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (name != null) {
      $result.name = name;
    }
    if (position != null) {
      $result.position = position;
    }
    if (isCollapsed != null) {
      $result.isCollapsed = isCollapsed;
    }
    if (createdAt != null) {
      $result.createdAt = createdAt;
    }
    return $result;
  }
  ChannelCategory._() : super();
  factory ChannelCategory.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelCategory.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelCategory', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'categoryId')
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
    ..aOS(3, _omitFieldNames ? '' : 'name')
    ..a<$core.int>(4, _omitFieldNames ? '' : 'position', $pb.PbFieldType.O3)
    ..aOB(5, _omitFieldNames ? '' : 'isCollapsed')
    ..aInt64(6, _omitFieldNames ? '' : 'createdAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelCategory clone() => ChannelCategory()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelCategory copyWith(void Function(ChannelCategory) updates) => super.copyWith((message) => updates(message as ChannelCategory)) as ChannelCategory;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelCategory create() => ChannelCategory._();
  ChannelCategory createEmptyInstance() => create();
  static $pb.PbList<ChannelCategory> createRepeated() => $pb.PbList<ChannelCategory>();
  @$core.pragma('dart2js:noInline')
  static ChannelCategory getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelCategory>(create);
  static ChannelCategory? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get categoryId => $_getSZ(0);
  @$pb.TagNumber(1)
  set categoryId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasCategoryId() => $_has(0);
  @$pb.TagNumber(1)
  void clearCategoryId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get name => $_getSZ(2);
  @$pb.TagNumber(3)
  set name($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasName() => $_has(2);
  @$pb.TagNumber(3)
  void clearName() => clearField(3);

  @$pb.TagNumber(4)
  $core.int get position => $_getIZ(3);
  @$pb.TagNumber(4)
  set position($core.int v) { $_setSignedInt32(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasPosition() => $_has(3);
  @$pb.TagNumber(4)
  void clearPosition() => clearField(4);

  @$pb.TagNumber(5)
  $core.bool get isCollapsed => $_getBF(4);
  @$pb.TagNumber(5)
  set isCollapsed($core.bool v) { $_setBool(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasIsCollapsed() => $_has(4);
  @$pb.TagNumber(5)
  void clearIsCollapsed() => clearField(5);

  @$pb.TagNumber(6)
  $fixnum.Int64 get createdAt => $_getI64(5);
  @$pb.TagNumber(6)
  set createdAt($fixnum.Int64 v) { $_setInt64(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasCreatedAt() => $_has(5);
  @$pb.TagNumber(6)
  void clearCreatedAt() => clearField(6);
}

/// Channel within a group/server
class Channel extends $pb.GeneratedMessage {
  factory Channel({
    $core.String? channelId,
    $core.String? groupId,
    $core.String? categoryId,
    $core.String? name,
    ChannelKind? kind,
    $core.int? position,
    $core.String? description,
    $core.bool? isNsfw,
    $fixnum.Int64? createdAt,
    $fixnum.Int64? slowmodeSeconds,
    $core.Iterable<PermissionOverrideEntry>? permissionOverrides,
    $core.int? bitrate,
    $core.int? userLimit,
    $core.String? rtcRegion,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (categoryId != null) {
      $result.categoryId = categoryId;
    }
    if (name != null) {
      $result.name = name;
    }
    if (kind != null) {
      $result.kind = kind;
    }
    if (position != null) {
      $result.position = position;
    }
    if (description != null) {
      $result.description = description;
    }
    if (isNsfw != null) {
      $result.isNsfw = isNsfw;
    }
    if (createdAt != null) {
      $result.createdAt = createdAt;
    }
    if (slowmodeSeconds != null) {
      $result.slowmodeSeconds = slowmodeSeconds;
    }
    if (permissionOverrides != null) {
      $result.permissionOverrides.addAll(permissionOverrides);
    }
    if (bitrate != null) {
      $result.bitrate = bitrate;
    }
    if (userLimit != null) {
      $result.userLimit = userLimit;
    }
    if (rtcRegion != null) {
      $result.rtcRegion = rtcRegion;
    }
    return $result;
  }
  Channel._() : super();
  factory Channel.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory Channel.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'Channel', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
    ..aOS(3, _omitFieldNames ? '' : 'categoryId')
    ..aOS(4, _omitFieldNames ? '' : 'name')
    ..e<ChannelKind>(5, _omitFieldNames ? '' : 'kind', $pb.PbFieldType.OE, defaultOrMaker: ChannelKind.CHANNEL_KIND_TEXT, valueOf: ChannelKind.valueOf, enumValues: ChannelKind.values)
    ..a<$core.int>(6, _omitFieldNames ? '' : 'position', $pb.PbFieldType.O3)
    ..aOS(7, _omitFieldNames ? '' : 'description')
    ..aOB(8, _omitFieldNames ? '' : 'isNsfw')
    ..aInt64(9, _omitFieldNames ? '' : 'createdAt')
    ..aInt64(10, _omitFieldNames ? '' : 'slowmodeSeconds')
    ..pc<PermissionOverrideEntry>(11, _omitFieldNames ? '' : 'permissionOverrides', $pb.PbFieldType.PM, subBuilder: PermissionOverrideEntry.create)
    ..a<$core.int>(12, _omitFieldNames ? '' : 'bitrate', $pb.PbFieldType.O3)
    ..a<$core.int>(13, _omitFieldNames ? '' : 'userLimit', $pb.PbFieldType.O3)
    ..aOS(14, _omitFieldNames ? '' : 'rtcRegion')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  Channel clone() => Channel()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  Channel copyWith(void Function(Channel) updates) => super.copyWith((message) => updates(message as Channel)) as Channel;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Channel create() => Channel._();
  Channel createEmptyInstance() => create();
  static $pb.PbList<Channel> createRepeated() => $pb.PbList<Channel>();
  @$core.pragma('dart2js:noInline')
  static Channel getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Channel>(create);
  static Channel? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get categoryId => $_getSZ(2);
  @$pb.TagNumber(3)
  set categoryId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasCategoryId() => $_has(2);
  @$pb.TagNumber(3)
  void clearCategoryId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get name => $_getSZ(3);
  @$pb.TagNumber(4)
  set name($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasName() => $_has(3);
  @$pb.TagNumber(4)
  void clearName() => clearField(4);

  @$pb.TagNumber(5)
  ChannelKind get kind => $_getN(4);
  @$pb.TagNumber(5)
  set kind(ChannelKind v) { setField(5, v); }
  @$pb.TagNumber(5)
  $core.bool hasKind() => $_has(4);
  @$pb.TagNumber(5)
  void clearKind() => clearField(5);

  @$pb.TagNumber(6)
  $core.int get position => $_getIZ(5);
  @$pb.TagNumber(6)
  set position($core.int v) { $_setSignedInt32(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasPosition() => $_has(5);
  @$pb.TagNumber(6)
  void clearPosition() => clearField(6);

  @$pb.TagNumber(7)
  $core.String get description => $_getSZ(6);
  @$pb.TagNumber(7)
  set description($core.String v) { $_setString(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasDescription() => $_has(6);
  @$pb.TagNumber(7)
  void clearDescription() => clearField(7);

  @$pb.TagNumber(8)
  $core.bool get isNsfw => $_getBF(7);
  @$pb.TagNumber(8)
  set isNsfw($core.bool v) { $_setBool(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasIsNsfw() => $_has(7);
  @$pb.TagNumber(8)
  void clearIsNsfw() => clearField(8);

  @$pb.TagNumber(9)
  $fixnum.Int64 get createdAt => $_getI64(8);
  @$pb.TagNumber(9)
  set createdAt($fixnum.Int64 v) { $_setInt64(8, v); }
  @$pb.TagNumber(9)
  $core.bool hasCreatedAt() => $_has(8);
  @$pb.TagNumber(9)
  void clearCreatedAt() => clearField(9);

  @$pb.TagNumber(10)
  $fixnum.Int64 get slowmodeSeconds => $_getI64(9);
  @$pb.TagNumber(10)
  set slowmodeSeconds($fixnum.Int64 v) { $_setInt64(9, v); }
  @$pb.TagNumber(10)
  $core.bool hasSlowmodeSeconds() => $_has(9);
  @$pb.TagNumber(10)
  void clearSlowmodeSeconds() => clearField(10);

  @$pb.TagNumber(11)
  $core.List<PermissionOverrideEntry> get permissionOverrides => $_getList(10);

  /// Voice-specific fields
  @$pb.TagNumber(12)
  $core.int get bitrate => $_getIZ(11);
  @$pb.TagNumber(12)
  set bitrate($core.int v) { $_setSignedInt32(11, v); }
  @$pb.TagNumber(12)
  $core.bool hasBitrate() => $_has(11);
  @$pb.TagNumber(12)
  void clearBitrate() => clearField(12);

  @$pb.TagNumber(13)
  $core.int get userLimit => $_getIZ(12);
  @$pb.TagNumber(13)
  set userLimit($core.int v) { $_setSignedInt32(12, v); }
  @$pb.TagNumber(13)
  $core.bool hasUserLimit() => $_has(12);
  @$pb.TagNumber(13)
  void clearUserLimit() => clearField(13);

  @$pb.TagNumber(14)
  $core.String get rtcRegion => $_getSZ(13);
  @$pb.TagNumber(14)
  set rtcRegion($core.String v) { $_setString(13, v); }
  @$pb.TagNumber(14)
  $core.bool hasRtcRegion() => $_has(13);
  @$pb.TagNumber(14)
  void clearRtcRegion() => clearField(14);
}

/// Create channel request
class CreateChannelRequest extends $pb.GeneratedMessage {
  factory CreateChannelRequest({
    $core.String? groupId,
    $core.String? requesterId,
    $core.String? name,
    ChannelKind? kind,
    $core.String? categoryId,
    $core.String? description,
    $core.Iterable<PermissionOverrideEntry>? permissionOverrides,
    $core.int? position,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (requesterId != null) {
      $result.requesterId = requesterId;
    }
    if (name != null) {
      $result.name = name;
    }
    if (kind != null) {
      $result.kind = kind;
    }
    if (categoryId != null) {
      $result.categoryId = categoryId;
    }
    if (description != null) {
      $result.description = description;
    }
    if (permissionOverrides != null) {
      $result.permissionOverrides.addAll(permissionOverrides);
    }
    if (position != null) {
      $result.position = position;
    }
    return $result;
  }
  CreateChannelRequest._() : super();
  factory CreateChannelRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateChannelRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateChannelRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..aOS(2, _omitFieldNames ? '' : 'requesterId')
    ..aOS(3, _omitFieldNames ? '' : 'name')
    ..e<ChannelKind>(4, _omitFieldNames ? '' : 'kind', $pb.PbFieldType.OE, defaultOrMaker: ChannelKind.CHANNEL_KIND_TEXT, valueOf: ChannelKind.valueOf, enumValues: ChannelKind.values)
    ..aOS(5, _omitFieldNames ? '' : 'categoryId')
    ..aOS(6, _omitFieldNames ? '' : 'description')
    ..pc<PermissionOverrideEntry>(7, _omitFieldNames ? '' : 'permissionOverrides', $pb.PbFieldType.PM, subBuilder: PermissionOverrideEntry.create)
    ..a<$core.int>(8, _omitFieldNames ? '' : 'position', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateChannelRequest clone() => CreateChannelRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateChannelRequest copyWith(void Function(CreateChannelRequest) updates) => super.copyWith((message) => updates(message as CreateChannelRequest)) as CreateChannelRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateChannelRequest create() => CreateChannelRequest._();
  CreateChannelRequest createEmptyInstance() => create();
  static $pb.PbList<CreateChannelRequest> createRepeated() => $pb.PbList<CreateChannelRequest>();
  @$core.pragma('dart2js:noInline')
  static CreateChannelRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateChannelRequest>(create);
  static CreateChannelRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get requesterId => $_getSZ(1);
  @$pb.TagNumber(2)
  set requesterId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRequesterId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRequesterId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get name => $_getSZ(2);
  @$pb.TagNumber(3)
  set name($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasName() => $_has(2);
  @$pb.TagNumber(3)
  void clearName() => clearField(3);

  @$pb.TagNumber(4)
  ChannelKind get kind => $_getN(3);
  @$pb.TagNumber(4)
  set kind(ChannelKind v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasKind() => $_has(3);
  @$pb.TagNumber(4)
  void clearKind() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get categoryId => $_getSZ(4);
  @$pb.TagNumber(5)
  set categoryId($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasCategoryId() => $_has(4);
  @$pb.TagNumber(5)
  void clearCategoryId() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get description => $_getSZ(5);
  @$pb.TagNumber(6)
  set description($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasDescription() => $_has(5);
  @$pb.TagNumber(6)
  void clearDescription() => clearField(6);

  @$pb.TagNumber(7)
  $core.List<PermissionOverrideEntry> get permissionOverrides => $_getList(6);

  @$pb.TagNumber(8)
  $core.int get position => $_getIZ(7);
  @$pb.TagNumber(8)
  set position($core.int v) { $_setSignedInt32(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasPosition() => $_has(7);
  @$pb.TagNumber(8)
  void clearPosition() => clearField(8);
}

class CreateChannelResponse extends $pb.GeneratedMessage {
  factory CreateChannelResponse({
    $0.ErrorCode? code,
    Channel? channel,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (channel != null) {
      $result.channel = channel;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  CreateChannelResponse._() : super();
  factory CreateChannelResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateChannelResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateChannelResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<Channel>(2, _omitFieldNames ? '' : 'channel', subBuilder: Channel.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateChannelResponse clone() => CreateChannelResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateChannelResponse copyWith(void Function(CreateChannelResponse) updates) => super.copyWith((message) => updates(message as CreateChannelResponse)) as CreateChannelResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateChannelResponse create() => CreateChannelResponse._();
  CreateChannelResponse createEmptyInstance() => create();
  static $pb.PbList<CreateChannelResponse> createRepeated() => $pb.PbList<CreateChannelResponse>();
  @$core.pragma('dart2js:noInline')
  static CreateChannelResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateChannelResponse>(create);
  static CreateChannelResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  Channel get channel => $_getN(1);
  @$pb.TagNumber(2)
  set channel(Channel v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannel() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannel() => clearField(2);
  @$pb.TagNumber(2)
  Channel ensureChannel() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Update channel request
class UpdateChannelRequest extends $pb.GeneratedMessage {
  factory UpdateChannelRequest({
    $core.String? channelId,
    $core.String? requesterId,
    $core.String? name,
    $core.String? description,
    $core.int? position,
    $core.String? categoryId,
    $core.Iterable<PermissionOverrideEntry>? permissionOverrides,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (requesterId != null) {
      $result.requesterId = requesterId;
    }
    if (name != null) {
      $result.name = name;
    }
    if (description != null) {
      $result.description = description;
    }
    if (position != null) {
      $result.position = position;
    }
    if (categoryId != null) {
      $result.categoryId = categoryId;
    }
    if (permissionOverrides != null) {
      $result.permissionOverrides.addAll(permissionOverrides);
    }
    return $result;
  }
  UpdateChannelRequest._() : super();
  factory UpdateChannelRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UpdateChannelRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UpdateChannelRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..aOS(2, _omitFieldNames ? '' : 'requesterId')
    ..aOS(3, _omitFieldNames ? '' : 'name')
    ..aOS(4, _omitFieldNames ? '' : 'description')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'position', $pb.PbFieldType.O3)
    ..aOS(6, _omitFieldNames ? '' : 'categoryId')
    ..pc<PermissionOverrideEntry>(7, _omitFieldNames ? '' : 'permissionOverrides', $pb.PbFieldType.PM, subBuilder: PermissionOverrideEntry.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UpdateChannelRequest clone() => UpdateChannelRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UpdateChannelRequest copyWith(void Function(UpdateChannelRequest) updates) => super.copyWith((message) => updates(message as UpdateChannelRequest)) as UpdateChannelRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UpdateChannelRequest create() => UpdateChannelRequest._();
  UpdateChannelRequest createEmptyInstance() => create();
  static $pb.PbList<UpdateChannelRequest> createRepeated() => $pb.PbList<UpdateChannelRequest>();
  @$core.pragma('dart2js:noInline')
  static UpdateChannelRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UpdateChannelRequest>(create);
  static UpdateChannelRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get requesterId => $_getSZ(1);
  @$pb.TagNumber(2)
  set requesterId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRequesterId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRequesterId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get name => $_getSZ(2);
  @$pb.TagNumber(3)
  set name($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasName() => $_has(2);
  @$pb.TagNumber(3)
  void clearName() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get description => $_getSZ(3);
  @$pb.TagNumber(4)
  set description($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasDescription() => $_has(3);
  @$pb.TagNumber(4)
  void clearDescription() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get position => $_getIZ(4);
  @$pb.TagNumber(5)
  set position($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasPosition() => $_has(4);
  @$pb.TagNumber(5)
  void clearPosition() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get categoryId => $_getSZ(5);
  @$pb.TagNumber(6)
  set categoryId($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasCategoryId() => $_has(5);
  @$pb.TagNumber(6)
  void clearCategoryId() => clearField(6);

  @$pb.TagNumber(7)
  $core.List<PermissionOverrideEntry> get permissionOverrides => $_getList(6);
}

class UpdateChannelResponse extends $pb.GeneratedMessage {
  factory UpdateChannelResponse({
    $0.ErrorCode? code,
    Channel? channel,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (channel != null) {
      $result.channel = channel;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  UpdateChannelResponse._() : super();
  factory UpdateChannelResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UpdateChannelResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UpdateChannelResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<Channel>(2, _omitFieldNames ? '' : 'channel', subBuilder: Channel.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UpdateChannelResponse clone() => UpdateChannelResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UpdateChannelResponse copyWith(void Function(UpdateChannelResponse) updates) => super.copyWith((message) => updates(message as UpdateChannelResponse)) as UpdateChannelResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UpdateChannelResponse create() => UpdateChannelResponse._();
  UpdateChannelResponse createEmptyInstance() => create();
  static $pb.PbList<UpdateChannelResponse> createRepeated() => $pb.PbList<UpdateChannelResponse>();
  @$core.pragma('dart2js:noInline')
  static UpdateChannelResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UpdateChannelResponse>(create);
  static UpdateChannelResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  Channel get channel => $_getN(1);
  @$pb.TagNumber(2)
  set channel(Channel v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannel() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannel() => clearField(2);
  @$pb.TagNumber(2)
  Channel ensureChannel() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Delete channel request
class DeleteChannelRequest extends $pb.GeneratedMessage {
  factory DeleteChannelRequest({
    $core.String? channelId,
    $core.String? requesterId,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (requesterId != null) {
      $result.requesterId = requesterId;
    }
    return $result;
  }
  DeleteChannelRequest._() : super();
  factory DeleteChannelRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeleteChannelRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeleteChannelRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..aOS(2, _omitFieldNames ? '' : 'requesterId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeleteChannelRequest clone() => DeleteChannelRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeleteChannelRequest copyWith(void Function(DeleteChannelRequest) updates) => super.copyWith((message) => updates(message as DeleteChannelRequest)) as DeleteChannelRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeleteChannelRequest create() => DeleteChannelRequest._();
  DeleteChannelRequest createEmptyInstance() => create();
  static $pb.PbList<DeleteChannelRequest> createRepeated() => $pb.PbList<DeleteChannelRequest>();
  @$core.pragma('dart2js:noInline')
  static DeleteChannelRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeleteChannelRequest>(create);
  static DeleteChannelRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get requesterId => $_getSZ(1);
  @$pb.TagNumber(2)
  set requesterId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRequesterId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRequesterId() => clearField(2);
}

class DeleteChannelResponse extends $pb.GeneratedMessage {
  factory DeleteChannelResponse({
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
  DeleteChannelResponse._() : super();
  factory DeleteChannelResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeleteChannelResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeleteChannelResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeleteChannelResponse clone() => DeleteChannelResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeleteChannelResponse copyWith(void Function(DeleteChannelResponse) updates) => super.copyWith((message) => updates(message as DeleteChannelResponse)) as DeleteChannelResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeleteChannelResponse create() => DeleteChannelResponse._();
  DeleteChannelResponse createEmptyInstance() => create();
  static $pb.PbList<DeleteChannelResponse> createRepeated() => $pb.PbList<DeleteChannelResponse>();
  @$core.pragma('dart2js:noInline')
  static DeleteChannelResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeleteChannelResponse>(create);
  static DeleteChannelResponse? _defaultInstance;

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

/// Get channels in a group
class GetChannelsRequest extends $pb.GeneratedMessage {
  factory GetChannelsRequest({
    $core.String? groupId,
    $core.String? userId,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetChannelsRequest._() : super();
  factory GetChannelsRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetChannelsRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetChannelsRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetChannelsRequest clone() => GetChannelsRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetChannelsRequest copyWith(void Function(GetChannelsRequest) updates) => super.copyWith((message) => updates(message as GetChannelsRequest)) as GetChannelsRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetChannelsRequest create() => GetChannelsRequest._();
  GetChannelsRequest createEmptyInstance() => create();
  static $pb.PbList<GetChannelsRequest> createRepeated() => $pb.PbList<GetChannelsRequest>();
  @$core.pragma('dart2js:noInline')
  static GetChannelsRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetChannelsRequest>(create);
  static GetChannelsRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);
}

class GetChannelsResponse extends $pb.GeneratedMessage {
  factory GetChannelsResponse({
    $0.ErrorCode? code,
    $core.Iterable<Channel>? channels,
    $core.Iterable<ChannelCategory>? categories,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (channels != null) {
      $result.channels.addAll(channels);
    }
    if (categories != null) {
      $result.categories.addAll(categories);
    }
    return $result;
  }
  GetChannelsResponse._() : super();
  factory GetChannelsResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetChannelsResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetChannelsResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<Channel>(2, _omitFieldNames ? '' : 'channels', $pb.PbFieldType.PM, subBuilder: Channel.create)
    ..pc<ChannelCategory>(3, _omitFieldNames ? '' : 'categories', $pb.PbFieldType.PM, subBuilder: ChannelCategory.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetChannelsResponse clone() => GetChannelsResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetChannelsResponse copyWith(void Function(GetChannelsResponse) updates) => super.copyWith((message) => updates(message as GetChannelsResponse)) as GetChannelsResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetChannelsResponse create() => GetChannelsResponse._();
  GetChannelsResponse createEmptyInstance() => create();
  static $pb.PbList<GetChannelsResponse> createRepeated() => $pb.PbList<GetChannelsResponse>();
  @$core.pragma('dart2js:noInline')
  static GetChannelsResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetChannelsResponse>(create);
  static GetChannelsResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<Channel> get channels => $_getList(1);

  @$pb.TagNumber(3)
  $core.List<ChannelCategory> get categories => $_getList(2);
}

/// Create category request
class CreateCategoryRequest extends $pb.GeneratedMessage {
  factory CreateCategoryRequest({
    $core.String? groupId,
    $core.String? requesterId,
    $core.String? name,
    $core.int? position,
  }) {
    final $result = create();
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (requesterId != null) {
      $result.requesterId = requesterId;
    }
    if (name != null) {
      $result.name = name;
    }
    if (position != null) {
      $result.position = position;
    }
    return $result;
  }
  CreateCategoryRequest._() : super();
  factory CreateCategoryRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateCategoryRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateCategoryRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'groupId')
    ..aOS(2, _omitFieldNames ? '' : 'requesterId')
    ..aOS(3, _omitFieldNames ? '' : 'name')
    ..a<$core.int>(4, _omitFieldNames ? '' : 'position', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateCategoryRequest clone() => CreateCategoryRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateCategoryRequest copyWith(void Function(CreateCategoryRequest) updates) => super.copyWith((message) => updates(message as CreateCategoryRequest)) as CreateCategoryRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateCategoryRequest create() => CreateCategoryRequest._();
  CreateCategoryRequest createEmptyInstance() => create();
  static $pb.PbList<CreateCategoryRequest> createRepeated() => $pb.PbList<CreateCategoryRequest>();
  @$core.pragma('dart2js:noInline')
  static CreateCategoryRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateCategoryRequest>(create);
  static CreateCategoryRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get groupId => $_getSZ(0);
  @$pb.TagNumber(1)
  set groupId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasGroupId() => $_has(0);
  @$pb.TagNumber(1)
  void clearGroupId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get requesterId => $_getSZ(1);
  @$pb.TagNumber(2)
  set requesterId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRequesterId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRequesterId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get name => $_getSZ(2);
  @$pb.TagNumber(3)
  set name($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasName() => $_has(2);
  @$pb.TagNumber(3)
  void clearName() => clearField(3);

  @$pb.TagNumber(4)
  $core.int get position => $_getIZ(3);
  @$pb.TagNumber(4)
  set position($core.int v) { $_setSignedInt32(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasPosition() => $_has(3);
  @$pb.TagNumber(4)
  void clearPosition() => clearField(4);
}

class CreateCategoryResponse extends $pb.GeneratedMessage {
  factory CreateCategoryResponse({
    $0.ErrorCode? code,
    ChannelCategory? category,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (category != null) {
      $result.category = category;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  CreateCategoryResponse._() : super();
  factory CreateCategoryResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory CreateCategoryResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'CreateCategoryResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<ChannelCategory>(2, _omitFieldNames ? '' : 'category', subBuilder: ChannelCategory.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  CreateCategoryResponse clone() => CreateCategoryResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  CreateCategoryResponse copyWith(void Function(CreateCategoryResponse) updates) => super.copyWith((message) => updates(message as CreateCategoryResponse)) as CreateCategoryResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CreateCategoryResponse create() => CreateCategoryResponse._();
  CreateCategoryResponse createEmptyInstance() => create();
  static $pb.PbList<CreateCategoryResponse> createRepeated() => $pb.PbList<CreateCategoryResponse>();
  @$core.pragma('dart2js:noInline')
  static CreateCategoryResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<CreateCategoryResponse>(create);
  static CreateCategoryResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  ChannelCategory get category => $_getN(1);
  @$pb.TagNumber(2)
  set category(ChannelCategory v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasCategory() => $_has(1);
  @$pb.TagNumber(2)
  void clearCategory() => clearField(2);
  @$pb.TagNumber(2)
  ChannelCategory ensureCategory() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Channel notification messages
class ChannelCreatedNotify extends $pb.GeneratedMessage {
  factory ChannelCreatedNotify({
    Channel? channel,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (channel != null) {
      $result.channel = channel;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ChannelCreatedNotify._() : super();
  factory ChannelCreatedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelCreatedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelCreatedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOM<Channel>(1, _omitFieldNames ? '' : 'channel', subBuilder: Channel.create)
    ..aInt64(2, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelCreatedNotify clone() => ChannelCreatedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelCreatedNotify copyWith(void Function(ChannelCreatedNotify) updates) => super.copyWith((message) => updates(message as ChannelCreatedNotify)) as ChannelCreatedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelCreatedNotify create() => ChannelCreatedNotify._();
  ChannelCreatedNotify createEmptyInstance() => create();
  static $pb.PbList<ChannelCreatedNotify> createRepeated() => $pb.PbList<ChannelCreatedNotify>();
  @$core.pragma('dart2js:noInline')
  static ChannelCreatedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelCreatedNotify>(create);
  static ChannelCreatedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  Channel get channel => $_getN(0);
  @$pb.TagNumber(1)
  set channel(Channel v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannel() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannel() => clearField(1);
  @$pb.TagNumber(1)
  Channel ensureChannel() => $_ensure(0);

  @$pb.TagNumber(2)
  $fixnum.Int64 get timestamp => $_getI64(1);
  @$pb.TagNumber(2)
  set timestamp($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTimestamp() => $_has(1);
  @$pb.TagNumber(2)
  void clearTimestamp() => clearField(2);
}

class ChannelUpdatedNotify extends $pb.GeneratedMessage {
  factory ChannelUpdatedNotify({
    Channel? channel,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (channel != null) {
      $result.channel = channel;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ChannelUpdatedNotify._() : super();
  factory ChannelUpdatedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelUpdatedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelUpdatedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOM<Channel>(1, _omitFieldNames ? '' : 'channel', subBuilder: Channel.create)
    ..aInt64(2, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelUpdatedNotify clone() => ChannelUpdatedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelUpdatedNotify copyWith(void Function(ChannelUpdatedNotify) updates) => super.copyWith((message) => updates(message as ChannelUpdatedNotify)) as ChannelUpdatedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelUpdatedNotify create() => ChannelUpdatedNotify._();
  ChannelUpdatedNotify createEmptyInstance() => create();
  static $pb.PbList<ChannelUpdatedNotify> createRepeated() => $pb.PbList<ChannelUpdatedNotify>();
  @$core.pragma('dart2js:noInline')
  static ChannelUpdatedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelUpdatedNotify>(create);
  static ChannelUpdatedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  Channel get channel => $_getN(0);
  @$pb.TagNumber(1)
  set channel(Channel v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannel() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannel() => clearField(1);
  @$pb.TagNumber(1)
  Channel ensureChannel() => $_ensure(0);

  @$pb.TagNumber(2)
  $fixnum.Int64 get timestamp => $_getI64(1);
  @$pb.TagNumber(2)
  set timestamp($fixnum.Int64 v) { $_setInt64(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTimestamp() => $_has(1);
  @$pb.TagNumber(2)
  void clearTimestamp() => clearField(2);
}

class ChannelDeletedNotify extends $pb.GeneratedMessage {
  factory ChannelDeletedNotify({
    $core.String? channelId,
    $core.String? groupId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (groupId != null) {
      $result.groupId = groupId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ChannelDeletedNotify._() : super();
  factory ChannelDeletedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelDeletedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelDeletedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..aOS(2, _omitFieldNames ? '' : 'groupId')
    ..aInt64(3, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelDeletedNotify clone() => ChannelDeletedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelDeletedNotify copyWith(void Function(ChannelDeletedNotify) updates) => super.copyWith((message) => updates(message as ChannelDeletedNotify)) as ChannelDeletedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelDeletedNotify create() => ChannelDeletedNotify._();
  ChannelDeletedNotify createEmptyInstance() => create();
  static $pb.PbList<ChannelDeletedNotify> createRepeated() => $pb.PbList<ChannelDeletedNotify>();
  @$core.pragma('dart2js:noInline')
  static ChannelDeletedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelDeletedNotify>(create);
  static ChannelDeletedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get groupId => $_getSZ(1);
  @$pb.TagNumber(2)
  set groupId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasGroupId() => $_has(1);
  @$pb.TagNumber(2)
  void clearGroupId() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get timestamp => $_getI64(2);
  @$pb.TagNumber(3)
  set timestamp($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasTimestamp() => $_has(2);
  @$pb.TagNumber(3)
  void clearTimestamp() => clearField(3);
}

/// Message reaction
class MessageReaction extends $pb.GeneratedMessage {
  factory MessageReaction({
    $core.String? messageId,
    $core.String? emoji,
    $core.int? count,
    $core.Iterable<$core.String>? userIds,
    $core.bool? reactedByMe,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (emoji != null) {
      $result.emoji = emoji;
    }
    if (count != null) {
      $result.count = count;
    }
    if (userIds != null) {
      $result.userIds.addAll(userIds);
    }
    if (reactedByMe != null) {
      $result.reactedByMe = reactedByMe;
    }
    return $result;
  }
  MessageReaction._() : super();
  factory MessageReaction.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageReaction.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageReaction', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'emoji')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'count', $pb.PbFieldType.O3)
    ..pPS(4, _omitFieldNames ? '' : 'userIds')
    ..aOB(5, _omitFieldNames ? '' : 'reactedByMe')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageReaction clone() => MessageReaction()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageReaction copyWith(void Function(MessageReaction) updates) => super.copyWith((message) => updates(message as MessageReaction)) as MessageReaction;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageReaction create() => MessageReaction._();
  MessageReaction createEmptyInstance() => create();
  static $pb.PbList<MessageReaction> createRepeated() => $pb.PbList<MessageReaction>();
  @$core.pragma('dart2js:noInline')
  static MessageReaction getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageReaction>(create);
  static MessageReaction? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get emoji => $_getSZ(1);
  @$pb.TagNumber(2)
  set emoji($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasEmoji() => $_has(1);
  @$pb.TagNumber(2)
  void clearEmoji() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get count => $_getIZ(2);
  @$pb.TagNumber(3)
  set count($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasCount() => $_has(2);
  @$pb.TagNumber(3)
  void clearCount() => clearField(3);

  @$pb.TagNumber(4)
  $core.List<$core.String> get userIds => $_getList(3);

  @$pb.TagNumber(5)
  $core.bool get reactedByMe => $_getBF(4);
  @$pb.TagNumber(5)
  set reactedByMe($core.bool v) { $_setBool(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasReactedByMe() => $_has(4);
  @$pb.TagNumber(5)
  void clearReactedByMe() => clearField(5);
}

/// Add reaction request
class AddReactionRequest extends $pb.GeneratedMessage {
  factory AddReactionRequest({
    $core.String? messageId,
    $core.String? userId,
    $core.String? emoji,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (emoji != null) {
      $result.emoji = emoji;
    }
    return $result;
  }
  AddReactionRequest._() : super();
  factory AddReactionRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory AddReactionRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'AddReactionRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aOS(3, _omitFieldNames ? '' : 'emoji')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  AddReactionRequest clone() => AddReactionRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  AddReactionRequest copyWith(void Function(AddReactionRequest) updates) => super.copyWith((message) => updates(message as AddReactionRequest)) as AddReactionRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AddReactionRequest create() => AddReactionRequest._();
  AddReactionRequest createEmptyInstance() => create();
  static $pb.PbList<AddReactionRequest> createRepeated() => $pb.PbList<AddReactionRequest>();
  @$core.pragma('dart2js:noInline')
  static AddReactionRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<AddReactionRequest>(create);
  static AddReactionRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get emoji => $_getSZ(2);
  @$pb.TagNumber(3)
  set emoji($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasEmoji() => $_has(2);
  @$pb.TagNumber(3)
  void clearEmoji() => clearField(3);
}

class AddReactionResponse extends $pb.GeneratedMessage {
  factory AddReactionResponse({
    $0.ErrorCode? code,
    MessageReaction? reaction,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (reaction != null) {
      $result.reaction = reaction;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  AddReactionResponse._() : super();
  factory AddReactionResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory AddReactionResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'AddReactionResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<MessageReaction>(2, _omitFieldNames ? '' : 'reaction', subBuilder: MessageReaction.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  AddReactionResponse clone() => AddReactionResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  AddReactionResponse copyWith(void Function(AddReactionResponse) updates) => super.copyWith((message) => updates(message as AddReactionResponse)) as AddReactionResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AddReactionResponse create() => AddReactionResponse._();
  AddReactionResponse createEmptyInstance() => create();
  static $pb.PbList<AddReactionResponse> createRepeated() => $pb.PbList<AddReactionResponse>();
  @$core.pragma('dart2js:noInline')
  static AddReactionResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<AddReactionResponse>(create);
  static AddReactionResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  MessageReaction get reaction => $_getN(1);
  @$pb.TagNumber(2)
  set reaction(MessageReaction v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasReaction() => $_has(1);
  @$pb.TagNumber(2)
  void clearReaction() => clearField(2);
  @$pb.TagNumber(2)
  MessageReaction ensureReaction() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Remove reaction request
class RemoveReactionRequest extends $pb.GeneratedMessage {
  factory RemoveReactionRequest({
    $core.String? messageId,
    $core.String? userId,
    $core.String? emoji,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (emoji != null) {
      $result.emoji = emoji;
    }
    return $result;
  }
  RemoveReactionRequest._() : super();
  factory RemoveReactionRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory RemoveReactionRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'RemoveReactionRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aOS(3, _omitFieldNames ? '' : 'emoji')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  RemoveReactionRequest clone() => RemoveReactionRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  RemoveReactionRequest copyWith(void Function(RemoveReactionRequest) updates) => super.copyWith((message) => updates(message as RemoveReactionRequest)) as RemoveReactionRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RemoveReactionRequest create() => RemoveReactionRequest._();
  RemoveReactionRequest createEmptyInstance() => create();
  static $pb.PbList<RemoveReactionRequest> createRepeated() => $pb.PbList<RemoveReactionRequest>();
  @$core.pragma('dart2js:noInline')
  static RemoveReactionRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<RemoveReactionRequest>(create);
  static RemoveReactionRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get emoji => $_getSZ(2);
  @$pb.TagNumber(3)
  set emoji($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasEmoji() => $_has(2);
  @$pb.TagNumber(3)
  void clearEmoji() => clearField(3);
}

class RemoveReactionResponse extends $pb.GeneratedMessage {
  factory RemoveReactionResponse({
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
  RemoveReactionResponse._() : super();
  factory RemoveReactionResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory RemoveReactionResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'RemoveReactionResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  RemoveReactionResponse clone() => RemoveReactionResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  RemoveReactionResponse copyWith(void Function(RemoveReactionResponse) updates) => super.copyWith((message) => updates(message as RemoveReactionResponse)) as RemoveReactionResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RemoveReactionResponse create() => RemoveReactionResponse._();
  RemoveReactionResponse createEmptyInstance() => create();
  static $pb.PbList<RemoveReactionResponse> createRepeated() => $pb.PbList<RemoveReactionResponse>();
  @$core.pragma('dart2js:noInline')
  static RemoveReactionResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<RemoveReactionResponse>(create);
  static RemoveReactionResponse? _defaultInstance;

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

/// Get reactions for a message
class GetReactionsRequest extends $pb.GeneratedMessage {
  factory GetReactionsRequest({
    $core.String? messageId,
    $core.String? emoji,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (emoji != null) {
      $result.emoji = emoji;
    }
    return $result;
  }
  GetReactionsRequest._() : super();
  factory GetReactionsRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetReactionsRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetReactionsRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'emoji')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetReactionsRequest clone() => GetReactionsRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetReactionsRequest copyWith(void Function(GetReactionsRequest) updates) => super.copyWith((message) => updates(message as GetReactionsRequest)) as GetReactionsRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetReactionsRequest create() => GetReactionsRequest._();
  GetReactionsRequest createEmptyInstance() => create();
  static $pb.PbList<GetReactionsRequest> createRepeated() => $pb.PbList<GetReactionsRequest>();
  @$core.pragma('dart2js:noInline')
  static GetReactionsRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetReactionsRequest>(create);
  static GetReactionsRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get emoji => $_getSZ(1);
  @$pb.TagNumber(2)
  set emoji($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasEmoji() => $_has(1);
  @$pb.TagNumber(2)
  void clearEmoji() => clearField(2);
}

class GetReactionsResponse extends $pb.GeneratedMessage {
  factory GetReactionsResponse({
    $0.ErrorCode? code,
    $core.Iterable<MessageReaction>? reactions,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (reactions != null) {
      $result.reactions.addAll(reactions);
    }
    return $result;
  }
  GetReactionsResponse._() : super();
  factory GetReactionsResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetReactionsResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetReactionsResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<MessageReaction>(2, _omitFieldNames ? '' : 'reactions', $pb.PbFieldType.PM, subBuilder: MessageReaction.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetReactionsResponse clone() => GetReactionsResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetReactionsResponse copyWith(void Function(GetReactionsResponse) updates) => super.copyWith((message) => updates(message as GetReactionsResponse)) as GetReactionsResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetReactionsResponse create() => GetReactionsResponse._();
  GetReactionsResponse createEmptyInstance() => create();
  static $pb.PbList<GetReactionsResponse> createRepeated() => $pb.PbList<GetReactionsResponse>();
  @$core.pragma('dart2js:noInline')
  static GetReactionsResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetReactionsResponse>(create);
  static GetReactionsResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<MessageReaction> get reactions => $_getList(1);
}

/// Reaction notification
class ReactionAddedNotify extends $pb.GeneratedMessage {
  factory ReactionAddedNotify({
    $core.String? messageId,
    $core.String? channelId,
    $core.String? emoji,
    $core.String? userId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (emoji != null) {
      $result.emoji = emoji;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ReactionAddedNotify._() : super();
  factory ReactionAddedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ReactionAddedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ReactionAddedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..aOS(3, _omitFieldNames ? '' : 'emoji')
    ..aOS(4, _omitFieldNames ? '' : 'userId')
    ..aInt64(5, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ReactionAddedNotify clone() => ReactionAddedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ReactionAddedNotify copyWith(void Function(ReactionAddedNotify) updates) => super.copyWith((message) => updates(message as ReactionAddedNotify)) as ReactionAddedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ReactionAddedNotify create() => ReactionAddedNotify._();
  ReactionAddedNotify createEmptyInstance() => create();
  static $pb.PbList<ReactionAddedNotify> createRepeated() => $pb.PbList<ReactionAddedNotify>();
  @$core.pragma('dart2js:noInline')
  static ReactionAddedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ReactionAddedNotify>(create);
  static ReactionAddedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get emoji => $_getSZ(2);
  @$pb.TagNumber(3)
  set emoji($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasEmoji() => $_has(2);
  @$pb.TagNumber(3)
  void clearEmoji() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get userId => $_getSZ(3);
  @$pb.TagNumber(4)
  set userId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasUserId() => $_has(3);
  @$pb.TagNumber(4)
  void clearUserId() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get timestamp => $_getI64(4);
  @$pb.TagNumber(5)
  set timestamp($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasTimestamp() => $_has(4);
  @$pb.TagNumber(5)
  void clearTimestamp() => clearField(5);
}

class ReactionRemovedNotify extends $pb.GeneratedMessage {
  factory ReactionRemovedNotify({
    $core.String? messageId,
    $core.String? channelId,
    $core.String? emoji,
    $core.String? userId,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (emoji != null) {
      $result.emoji = emoji;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  ReactionRemovedNotify._() : super();
  factory ReactionRemovedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ReactionRemovedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ReactionRemovedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..aOS(3, _omitFieldNames ? '' : 'emoji')
    ..aOS(4, _omitFieldNames ? '' : 'userId')
    ..aInt64(5, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ReactionRemovedNotify clone() => ReactionRemovedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ReactionRemovedNotify copyWith(void Function(ReactionRemovedNotify) updates) => super.copyWith((message) => updates(message as ReactionRemovedNotify)) as ReactionRemovedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ReactionRemovedNotify create() => ReactionRemovedNotify._();
  ReactionRemovedNotify createEmptyInstance() => create();
  static $pb.PbList<ReactionRemovedNotify> createRepeated() => $pb.PbList<ReactionRemovedNotify>();
  @$core.pragma('dart2js:noInline')
  static ReactionRemovedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ReactionRemovedNotify>(create);
  static ReactionRemovedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get emoji => $_getSZ(2);
  @$pb.TagNumber(3)
  set emoji($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasEmoji() => $_has(2);
  @$pb.TagNumber(3)
  void clearEmoji() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get userId => $_getSZ(3);
  @$pb.TagNumber(4)
  set userId($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasUserId() => $_has(3);
  @$pb.TagNumber(4)
  void clearUserId() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get timestamp => $_getI64(4);
  @$pb.TagNumber(5)
  set timestamp($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasTimestamp() => $_has(4);
  @$pb.TagNumber(5)
  void clearTimestamp() => clearField(5);
}

/// Mention in a message
class Mention extends $pb.GeneratedMessage {
  factory Mention({
    MentionType? type,
    $core.String? id,
    $core.int? startIndex,
    $core.int? length,
  }) {
    final $result = create();
    if (type != null) {
      $result.type = type;
    }
    if (id != null) {
      $result.id = id;
    }
    if (startIndex != null) {
      $result.startIndex = startIndex;
    }
    if (length != null) {
      $result.length = length;
    }
    return $result;
  }
  Mention._() : super();
  factory Mention.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory Mention.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'Mention', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<MentionType>(1, _omitFieldNames ? '' : 'type', $pb.PbFieldType.OE, defaultOrMaker: MentionType.MENTION_TYPE_USER, valueOf: MentionType.valueOf, enumValues: MentionType.values)
    ..aOS(2, _omitFieldNames ? '' : 'id')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'startIndex', $pb.PbFieldType.O3)
    ..a<$core.int>(4, _omitFieldNames ? '' : 'length', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  Mention clone() => Mention()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  Mention copyWith(void Function(Mention) updates) => super.copyWith((message) => updates(message as Mention)) as Mention;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Mention create() => Mention._();
  Mention createEmptyInstance() => create();
  static $pb.PbList<Mention> createRepeated() => $pb.PbList<Mention>();
  @$core.pragma('dart2js:noInline')
  static Mention getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Mention>(create);
  static Mention? _defaultInstance;

  @$pb.TagNumber(1)
  MentionType get type => $_getN(0);
  @$pb.TagNumber(1)
  set type(MentionType v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasType() => $_has(0);
  @$pb.TagNumber(1)
  void clearType() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get id => $_getSZ(1);
  @$pb.TagNumber(2)
  set id($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasId() => $_has(1);
  @$pb.TagNumber(2)
  void clearId() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get startIndex => $_getIZ(2);
  @$pb.TagNumber(3)
  set startIndex($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasStartIndex() => $_has(2);
  @$pb.TagNumber(3)
  void clearStartIndex() => clearField(3);

  @$pb.TagNumber(4)
  $core.int get length => $_getIZ(3);
  @$pb.TagNumber(4)
  set length($core.int v) { $_setSignedInt32(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasLength() => $_has(3);
  @$pb.TagNumber(4)
  void clearLength() => clearField(4);
}

/// Extended chat message with mentions
class ChatMessageEx extends $pb.GeneratedMessage {
  factory ChatMessageEx({
    ChatMessage? baseMessage,
    $core.Iterable<Mention>? mentions,
    $core.Iterable<$core.String>? mentionedUserIds,
    $core.bool? mentionsEveryone,
    $core.bool? mentionsHere,
  }) {
    final $result = create();
    if (baseMessage != null) {
      $result.baseMessage = baseMessage;
    }
    if (mentions != null) {
      $result.mentions.addAll(mentions);
    }
    if (mentionedUserIds != null) {
      $result.mentionedUserIds.addAll(mentionedUserIds);
    }
    if (mentionsEveryone != null) {
      $result.mentionsEveryone = mentionsEveryone;
    }
    if (mentionsHere != null) {
      $result.mentionsHere = mentionsHere;
    }
    return $result;
  }
  ChatMessageEx._() : super();
  factory ChatMessageEx.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChatMessageEx.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChatMessageEx', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOM<ChatMessage>(1, _omitFieldNames ? '' : 'baseMessage', subBuilder: ChatMessage.create)
    ..pc<Mention>(2, _omitFieldNames ? '' : 'mentions', $pb.PbFieldType.PM, subBuilder: Mention.create)
    ..pPS(3, _omitFieldNames ? '' : 'mentionedUserIds')
    ..aOB(4, _omitFieldNames ? '' : 'mentionsEveryone')
    ..aOB(5, _omitFieldNames ? '' : 'mentionsHere')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChatMessageEx clone() => ChatMessageEx()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChatMessageEx copyWith(void Function(ChatMessageEx) updates) => super.copyWith((message) => updates(message as ChatMessageEx)) as ChatMessageEx;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChatMessageEx create() => ChatMessageEx._();
  ChatMessageEx createEmptyInstance() => create();
  static $pb.PbList<ChatMessageEx> createRepeated() => $pb.PbList<ChatMessageEx>();
  @$core.pragma('dart2js:noInline')
  static ChatMessageEx getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChatMessageEx>(create);
  static ChatMessageEx? _defaultInstance;

  @$pb.TagNumber(1)
  ChatMessage get baseMessage => $_getN(0);
  @$pb.TagNumber(1)
  set baseMessage(ChatMessage v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasBaseMessage() => $_has(0);
  @$pb.TagNumber(1)
  void clearBaseMessage() => clearField(1);
  @$pb.TagNumber(1)
  ChatMessage ensureBaseMessage() => $_ensure(0);

  @$pb.TagNumber(2)
  $core.List<Mention> get mentions => $_getList(1);

  @$pb.TagNumber(3)
  $core.List<$core.String> get mentionedUserIds => $_getList(2);

  @$pb.TagNumber(4)
  $core.bool get mentionsEveryone => $_getBF(3);
  @$pb.TagNumber(4)
  set mentionsEveryone($core.bool v) { $_setBool(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasMentionsEveryone() => $_has(3);
  @$pb.TagNumber(4)
  void clearMentionsEveryone() => clearField(4);

  @$pb.TagNumber(5)
  $core.bool get mentionsHere => $_getBF(4);
  @$pb.TagNumber(5)
  set mentionsHere($core.bool v) { $_setBool(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasMentionsHere() => $_has(4);
  @$pb.TagNumber(5)
  void clearMentionsHere() => clearField(5);
}

/// Mention autocomplete entry
class MentionSuggestion extends $pb.GeneratedMessage {
  factory MentionSuggestion({
    $core.String? displayText,
    $core.String? id,
    MentionType? type,
    $core.String? iconUrl,
  }) {
    final $result = create();
    if (displayText != null) {
      $result.displayText = displayText;
    }
    if (id != null) {
      $result.id = id;
    }
    if (type != null) {
      $result.type = type;
    }
    if (iconUrl != null) {
      $result.iconUrl = iconUrl;
    }
    return $result;
  }
  MentionSuggestion._() : super();
  factory MentionSuggestion.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MentionSuggestion.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MentionSuggestion', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'displayText')
    ..aOS(2, _omitFieldNames ? '' : 'id')
    ..e<MentionType>(3, _omitFieldNames ? '' : 'type', $pb.PbFieldType.OE, defaultOrMaker: MentionType.MENTION_TYPE_USER, valueOf: MentionType.valueOf, enumValues: MentionType.values)
    ..aOS(4, _omitFieldNames ? '' : 'iconUrl')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MentionSuggestion clone() => MentionSuggestion()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MentionSuggestion copyWith(void Function(MentionSuggestion) updates) => super.copyWith((message) => updates(message as MentionSuggestion)) as MentionSuggestion;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MentionSuggestion create() => MentionSuggestion._();
  MentionSuggestion createEmptyInstance() => create();
  static $pb.PbList<MentionSuggestion> createRepeated() => $pb.PbList<MentionSuggestion>();
  @$core.pragma('dart2js:noInline')
  static MentionSuggestion getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MentionSuggestion>(create);
  static MentionSuggestion? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get displayText => $_getSZ(0);
  @$pb.TagNumber(1)
  set displayText($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasDisplayText() => $_has(0);
  @$pb.TagNumber(1)
  void clearDisplayText() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get id => $_getSZ(1);
  @$pb.TagNumber(2)
  set id($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasId() => $_has(1);
  @$pb.TagNumber(2)
  void clearId() => clearField(2);

  @$pb.TagNumber(3)
  MentionType get type => $_getN(2);
  @$pb.TagNumber(3)
  set type(MentionType v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasType() => $_has(2);
  @$pb.TagNumber(3)
  void clearType() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get iconUrl => $_getSZ(3);
  @$pb.TagNumber(4)
  set iconUrl($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasIconUrl() => $_has(3);
  @$pb.TagNumber(4)
  void clearIconUrl() => clearField(4);
}

/// Mention autocomplete query
class GetMentionSuggestionsRequest extends $pb.GeneratedMessage {
  factory GetMentionSuggestionsRequest({
    $core.String? userId,
    $core.String? channelId,
    $core.String? query,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (query != null) {
      $result.query = query;
    }
    return $result;
  }
  GetMentionSuggestionsRequest._() : super();
  factory GetMentionSuggestionsRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetMentionSuggestionsRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetMentionSuggestionsRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..aOS(3, _omitFieldNames ? '' : 'query')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetMentionSuggestionsRequest clone() => GetMentionSuggestionsRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetMentionSuggestionsRequest copyWith(void Function(GetMentionSuggestionsRequest) updates) => super.copyWith((message) => updates(message as GetMentionSuggestionsRequest)) as GetMentionSuggestionsRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetMentionSuggestionsRequest create() => GetMentionSuggestionsRequest._();
  GetMentionSuggestionsRequest createEmptyInstance() => create();
  static $pb.PbList<GetMentionSuggestionsRequest> createRepeated() => $pb.PbList<GetMentionSuggestionsRequest>();
  @$core.pragma('dart2js:noInline')
  static GetMentionSuggestionsRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetMentionSuggestionsRequest>(create);
  static GetMentionSuggestionsRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get query => $_getSZ(2);
  @$pb.TagNumber(3)
  set query($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasQuery() => $_has(2);
  @$pb.TagNumber(3)
  void clearQuery() => clearField(3);
}

class GetMentionSuggestionsResponse extends $pb.GeneratedMessage {
  factory GetMentionSuggestionsResponse({
    $0.ErrorCode? code,
    $core.Iterable<MentionSuggestion>? suggestions,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (suggestions != null) {
      $result.suggestions.addAll(suggestions);
    }
    return $result;
  }
  GetMentionSuggestionsResponse._() : super();
  factory GetMentionSuggestionsResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetMentionSuggestionsResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetMentionSuggestionsResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<MentionSuggestion>(2, _omitFieldNames ? '' : 'suggestions', $pb.PbFieldType.PM, subBuilder: MentionSuggestion.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetMentionSuggestionsResponse clone() => GetMentionSuggestionsResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetMentionSuggestionsResponse copyWith(void Function(GetMentionSuggestionsResponse) updates) => super.copyWith((message) => updates(message as GetMentionSuggestionsResponse)) as GetMentionSuggestionsResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetMentionSuggestionsResponse create() => GetMentionSuggestionsResponse._();
  GetMentionSuggestionsResponse createEmptyInstance() => create();
  static $pb.PbList<GetMentionSuggestionsResponse> createRepeated() => $pb.PbList<GetMentionSuggestionsResponse>();
  @$core.pragma('dart2js:noInline')
  static GetMentionSuggestionsResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetMentionSuggestionsResponse>(create);
  static GetMentionSuggestionsResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<MentionSuggestion> get suggestions => $_getList(1);
}

/// ========== Message Edit/Delete ==========
/// Message edit history
class MessageEdit extends $pb.GeneratedMessage {
  factory MessageEdit({
    $core.List<$core.int>? oldContent,
    $core.List<$core.int>? newContent,
    $fixnum.Int64? editedAt,
    $core.String? editedBy,
  }) {
    final $result = create();
    if (oldContent != null) {
      $result.oldContent = oldContent;
    }
    if (newContent != null) {
      $result.newContent = newContent;
    }
    if (editedAt != null) {
      $result.editedAt = editedAt;
    }
    if (editedBy != null) {
      $result.editedBy = editedBy;
    }
    return $result;
  }
  MessageEdit._() : super();
  factory MessageEdit.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageEdit.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageEdit', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..a<$core.List<$core.int>>(1, _omitFieldNames ? '' : 'oldContent', $pb.PbFieldType.OY)
    ..a<$core.List<$core.int>>(2, _omitFieldNames ? '' : 'newContent', $pb.PbFieldType.OY)
    ..aInt64(3, _omitFieldNames ? '' : 'editedAt')
    ..aOS(4, _omitFieldNames ? '' : 'editedBy')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageEdit clone() => MessageEdit()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageEdit copyWith(void Function(MessageEdit) updates) => super.copyWith((message) => updates(message as MessageEdit)) as MessageEdit;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageEdit create() => MessageEdit._();
  MessageEdit createEmptyInstance() => create();
  static $pb.PbList<MessageEdit> createRepeated() => $pb.PbList<MessageEdit>();
  @$core.pragma('dart2js:noInline')
  static MessageEdit getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageEdit>(create);
  static MessageEdit? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.int> get oldContent => $_getN(0);
  @$pb.TagNumber(1)
  set oldContent($core.List<$core.int> v) { $_setBytes(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasOldContent() => $_has(0);
  @$pb.TagNumber(1)
  void clearOldContent() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<$core.int> get newContent => $_getN(1);
  @$pb.TagNumber(2)
  set newContent($core.List<$core.int> v) { $_setBytes(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasNewContent() => $_has(1);
  @$pb.TagNumber(2)
  void clearNewContent() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get editedAt => $_getI64(2);
  @$pb.TagNumber(3)
  set editedAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasEditedAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearEditedAt() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get editedBy => $_getSZ(3);
  @$pb.TagNumber(4)
  set editedBy($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasEditedBy() => $_has(3);
  @$pb.TagNumber(4)
  void clearEditedBy() => clearField(4);
}

/// Extended message with edit/delete support
class ChatMessageFull extends $pb.GeneratedMessage {
  factory ChatMessageFull({
    $core.String? messageId,
    $core.String? senderId,
    $core.String? receiverId,
    ChannelType? channelType,
    $core.String? channelId,
    MsgType? msgType,
    $core.List<$core.int>? content,
    $fixnum.Int64? timestamp,
    $core.bool? isDeleted,
    $fixnum.Int64? deletedAt,
    $core.String? deletedBy,
    $core.bool? isEdited,
    $fixnum.Int64? editedAt,
    $core.int? editCount,
    $core.Iterable<MessageEdit>? editHistory,
    $core.String? replyToMessageId,
    $core.int? replyCount,
    $core.Iterable<MessageReaction>? reactions,
    $core.Iterable<Mention>? mentions,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (senderId != null) {
      $result.senderId = senderId;
    }
    if (receiverId != null) {
      $result.receiverId = receiverId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (msgType != null) {
      $result.msgType = msgType;
    }
    if (content != null) {
      $result.content = content;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    if (isDeleted != null) {
      $result.isDeleted = isDeleted;
    }
    if (deletedAt != null) {
      $result.deletedAt = deletedAt;
    }
    if (deletedBy != null) {
      $result.deletedBy = deletedBy;
    }
    if (isEdited != null) {
      $result.isEdited = isEdited;
    }
    if (editedAt != null) {
      $result.editedAt = editedAt;
    }
    if (editCount != null) {
      $result.editCount = editCount;
    }
    if (editHistory != null) {
      $result.editHistory.addAll(editHistory);
    }
    if (replyToMessageId != null) {
      $result.replyToMessageId = replyToMessageId;
    }
    if (replyCount != null) {
      $result.replyCount = replyCount;
    }
    if (reactions != null) {
      $result.reactions.addAll(reactions);
    }
    if (mentions != null) {
      $result.mentions.addAll(mentions);
    }
    return $result;
  }
  ChatMessageFull._() : super();
  factory ChatMessageFull.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChatMessageFull.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChatMessageFull', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'senderId')
    ..aOS(3, _omitFieldNames ? '' : 'receiverId')
    ..e<ChannelType>(4, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(5, _omitFieldNames ? '' : 'channelId')
    ..e<MsgType>(6, _omitFieldNames ? '' : 'msgType', $pb.PbFieldType.OE, defaultOrMaker: MsgType.TEXT, valueOf: MsgType.valueOf, enumValues: MsgType.values)
    ..a<$core.List<$core.int>>(7, _omitFieldNames ? '' : 'content', $pb.PbFieldType.OY)
    ..aInt64(8, _omitFieldNames ? '' : 'timestamp')
    ..aOB(9, _omitFieldNames ? '' : 'isDeleted')
    ..aInt64(10, _omitFieldNames ? '' : 'deletedAt')
    ..aOS(11, _omitFieldNames ? '' : 'deletedBy')
    ..aOB(12, _omitFieldNames ? '' : 'isEdited')
    ..aInt64(13, _omitFieldNames ? '' : 'editedAt')
    ..a<$core.int>(14, _omitFieldNames ? '' : 'editCount', $pb.PbFieldType.O3)
    ..pc<MessageEdit>(15, _omitFieldNames ? '' : 'editHistory', $pb.PbFieldType.PM, subBuilder: MessageEdit.create)
    ..aOS(16, _omitFieldNames ? '' : 'replyToMessageId')
    ..a<$core.int>(17, _omitFieldNames ? '' : 'replyCount', $pb.PbFieldType.O3)
    ..pc<MessageReaction>(18, _omitFieldNames ? '' : 'reactions', $pb.PbFieldType.PM, subBuilder: MessageReaction.create)
    ..pc<Mention>(19, _omitFieldNames ? '' : 'mentions', $pb.PbFieldType.PM, subBuilder: Mention.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChatMessageFull clone() => ChatMessageFull()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChatMessageFull copyWith(void Function(ChatMessageFull) updates) => super.copyWith((message) => updates(message as ChatMessageFull)) as ChatMessageFull;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChatMessageFull create() => ChatMessageFull._();
  ChatMessageFull createEmptyInstance() => create();
  static $pb.PbList<ChatMessageFull> createRepeated() => $pb.PbList<ChatMessageFull>();
  @$core.pragma('dart2js:noInline')
  static ChatMessageFull getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChatMessageFull>(create);
  static ChatMessageFull? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get senderId => $_getSZ(1);
  @$pb.TagNumber(2)
  set senderId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSenderId() => $_has(1);
  @$pb.TagNumber(2)
  void clearSenderId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get receiverId => $_getSZ(2);
  @$pb.TagNumber(3)
  set receiverId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasReceiverId() => $_has(2);
  @$pb.TagNumber(3)
  void clearReceiverId() => clearField(3);

  @$pb.TagNumber(4)
  ChannelType get channelType => $_getN(3);
  @$pb.TagNumber(4)
  set channelType(ChannelType v) { setField(4, v); }
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
  MsgType get msgType => $_getN(5);
  @$pb.TagNumber(6)
  set msgType(MsgType v) { setField(6, v); }
  @$pb.TagNumber(6)
  $core.bool hasMsgType() => $_has(5);
  @$pb.TagNumber(6)
  void clearMsgType() => clearField(6);

  @$pb.TagNumber(7)
  $core.List<$core.int> get content => $_getN(6);
  @$pb.TagNumber(7)
  set content($core.List<$core.int> v) { $_setBytes(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasContent() => $_has(6);
  @$pb.TagNumber(7)
  void clearContent() => clearField(7);

  @$pb.TagNumber(8)
  $fixnum.Int64 get timestamp => $_getI64(7);
  @$pb.TagNumber(8)
  set timestamp($fixnum.Int64 v) { $_setInt64(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasTimestamp() => $_has(7);
  @$pb.TagNumber(8)
  void clearTimestamp() => clearField(8);

  /// Edit/delete fields
  @$pb.TagNumber(9)
  $core.bool get isDeleted => $_getBF(8);
  @$pb.TagNumber(9)
  set isDeleted($core.bool v) { $_setBool(8, v); }
  @$pb.TagNumber(9)
  $core.bool hasIsDeleted() => $_has(8);
  @$pb.TagNumber(9)
  void clearIsDeleted() => clearField(9);

  @$pb.TagNumber(10)
  $fixnum.Int64 get deletedAt => $_getI64(9);
  @$pb.TagNumber(10)
  set deletedAt($fixnum.Int64 v) { $_setInt64(9, v); }
  @$pb.TagNumber(10)
  $core.bool hasDeletedAt() => $_has(9);
  @$pb.TagNumber(10)
  void clearDeletedAt() => clearField(10);

  @$pb.TagNumber(11)
  $core.String get deletedBy => $_getSZ(10);
  @$pb.TagNumber(11)
  set deletedBy($core.String v) { $_setString(10, v); }
  @$pb.TagNumber(11)
  $core.bool hasDeletedBy() => $_has(10);
  @$pb.TagNumber(11)
  void clearDeletedBy() => clearField(11);

  @$pb.TagNumber(12)
  $core.bool get isEdited => $_getBF(11);
  @$pb.TagNumber(12)
  set isEdited($core.bool v) { $_setBool(11, v); }
  @$pb.TagNumber(12)
  $core.bool hasIsEdited() => $_has(11);
  @$pb.TagNumber(12)
  void clearIsEdited() => clearField(12);

  @$pb.TagNumber(13)
  $fixnum.Int64 get editedAt => $_getI64(12);
  @$pb.TagNumber(13)
  set editedAt($fixnum.Int64 v) { $_setInt64(12, v); }
  @$pb.TagNumber(13)
  $core.bool hasEditedAt() => $_has(12);
  @$pb.TagNumber(13)
  void clearEditedAt() => clearField(13);

  @$pb.TagNumber(14)
  $core.int get editCount => $_getIZ(13);
  @$pb.TagNumber(14)
  set editCount($core.int v) { $_setSignedInt32(13, v); }
  @$pb.TagNumber(14)
  $core.bool hasEditCount() => $_has(13);
  @$pb.TagNumber(14)
  void clearEditCount() => clearField(14);

  @$pb.TagNumber(15)
  $core.List<MessageEdit> get editHistory => $_getList(14);

  /// Reply thread support
  @$pb.TagNumber(16)
  $core.String get replyToMessageId => $_getSZ(15);
  @$pb.TagNumber(16)
  set replyToMessageId($core.String v) { $_setString(15, v); }
  @$pb.TagNumber(16)
  $core.bool hasReplyToMessageId() => $_has(15);
  @$pb.TagNumber(16)
  void clearReplyToMessageId() => clearField(16);

  @$pb.TagNumber(17)
  $core.int get replyCount => $_getIZ(16);
  @$pb.TagNumber(17)
  set replyCount($core.int v) { $_setSignedInt32(16, v); }
  @$pb.TagNumber(17)
  $core.bool hasReplyCount() => $_has(16);
  @$pb.TagNumber(17)
  void clearReplyCount() => clearField(17);

  /// Reactions and mentions
  @$pb.TagNumber(18)
  $core.List<MessageReaction> get reactions => $_getList(17);

  @$pb.TagNumber(19)
  $core.List<Mention> get mentions => $_getList(18);
}

/// Edit message request
class EditMessageRequest extends $pb.GeneratedMessage {
  factory EditMessageRequest({
    $core.String? messageId,
    $core.String? userId,
    $core.List<$core.int>? newContent,
    $fixnum.Int64? editTimestamp,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (newContent != null) {
      $result.newContent = newContent;
    }
    if (editTimestamp != null) {
      $result.editTimestamp = editTimestamp;
    }
    return $result;
  }
  EditMessageRequest._() : super();
  factory EditMessageRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory EditMessageRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EditMessageRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..a<$core.List<$core.int>>(3, _omitFieldNames ? '' : 'newContent', $pb.PbFieldType.OY)
    ..aInt64(4, _omitFieldNames ? '' : 'editTimestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  EditMessageRequest clone() => EditMessageRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  EditMessageRequest copyWith(void Function(EditMessageRequest) updates) => super.copyWith((message) => updates(message as EditMessageRequest)) as EditMessageRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static EditMessageRequest create() => EditMessageRequest._();
  EditMessageRequest createEmptyInstance() => create();
  static $pb.PbList<EditMessageRequest> createRepeated() => $pb.PbList<EditMessageRequest>();
  @$core.pragma('dart2js:noInline')
  static EditMessageRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<EditMessageRequest>(create);
  static EditMessageRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.List<$core.int> get newContent => $_getN(2);
  @$pb.TagNumber(3)
  set newContent($core.List<$core.int> v) { $_setBytes(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasNewContent() => $_has(2);
  @$pb.TagNumber(3)
  void clearNewContent() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get editTimestamp => $_getI64(3);
  @$pb.TagNumber(4)
  set editTimestamp($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasEditTimestamp() => $_has(3);
  @$pb.TagNumber(4)
  void clearEditTimestamp() => clearField(4);
}

class EditMessageResponse extends $pb.GeneratedMessage {
  factory EditMessageResponse({
    $0.ErrorCode? code,
    ChatMessageFull? message,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (message != null) {
      $result.message = message;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  EditMessageResponse._() : super();
  factory EditMessageResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory EditMessageResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'EditMessageResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<ChatMessageFull>(2, _omitFieldNames ? '' : 'message', subBuilder: ChatMessageFull.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  EditMessageResponse clone() => EditMessageResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  EditMessageResponse copyWith(void Function(EditMessageResponse) updates) => super.copyWith((message) => updates(message as EditMessageResponse)) as EditMessageResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static EditMessageResponse create() => EditMessageResponse._();
  EditMessageResponse createEmptyInstance() => create();
  static $pb.PbList<EditMessageResponse> createRepeated() => $pb.PbList<EditMessageResponse>();
  @$core.pragma('dart2js:noInline')
  static EditMessageResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<EditMessageResponse>(create);
  static EditMessageResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  ChatMessageFull get message => $_getN(1);
  @$pb.TagNumber(2)
  set message(ChatMessageFull v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasMessage() => $_has(1);
  @$pb.TagNumber(2)
  void clearMessage() => clearField(2);
  @$pb.TagNumber(2)
  ChatMessageFull ensureMessage() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Delete message request
class DeleteMessageRequest extends $pb.GeneratedMessage {
  factory DeleteMessageRequest({
    $core.String? messageId,
    $core.String? userId,
    $core.bool? isHardDelete,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (isHardDelete != null) {
      $result.isHardDelete = isHardDelete;
    }
    return $result;
  }
  DeleteMessageRequest._() : super();
  factory DeleteMessageRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeleteMessageRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeleteMessageRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..aOB(3, _omitFieldNames ? '' : 'isHardDelete')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeleteMessageRequest clone() => DeleteMessageRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeleteMessageRequest copyWith(void Function(DeleteMessageRequest) updates) => super.copyWith((message) => updates(message as DeleteMessageRequest)) as DeleteMessageRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeleteMessageRequest create() => DeleteMessageRequest._();
  DeleteMessageRequest createEmptyInstance() => create();
  static $pb.PbList<DeleteMessageRequest> createRepeated() => $pb.PbList<DeleteMessageRequest>();
  @$core.pragma('dart2js:noInline')
  static DeleteMessageRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeleteMessageRequest>(create);
  static DeleteMessageRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get isHardDelete => $_getBF(2);
  @$pb.TagNumber(3)
  set isHardDelete($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasIsHardDelete() => $_has(2);
  @$pb.TagNumber(3)
  void clearIsHardDelete() => clearField(3);
}

class DeleteMessageResponse extends $pb.GeneratedMessage {
  factory DeleteMessageResponse({
    $0.ErrorCode? code,
    $fixnum.Int64? serverTime,
    $core.bool? wasPermanentlyDeleted,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    if (wasPermanentlyDeleted != null) {
      $result.wasPermanentlyDeleted = wasPermanentlyDeleted;
    }
    return $result;
  }
  DeleteMessageResponse._() : super();
  factory DeleteMessageResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory DeleteMessageResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'DeleteMessageResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aInt64(2, _omitFieldNames ? '' : 'serverTime')
    ..aOB(3, _omitFieldNames ? '' : 'wasPermanentlyDeleted')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  DeleteMessageResponse clone() => DeleteMessageResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  DeleteMessageResponse copyWith(void Function(DeleteMessageResponse) updates) => super.copyWith((message) => updates(message as DeleteMessageResponse)) as DeleteMessageResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static DeleteMessageResponse create() => DeleteMessageResponse._();
  DeleteMessageResponse createEmptyInstance() => create();
  static $pb.PbList<DeleteMessageResponse> createRepeated() => $pb.PbList<DeleteMessageResponse>();
  @$core.pragma('dart2js:noInline')
  static DeleteMessageResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<DeleteMessageResponse>(create);
  static DeleteMessageResponse? _defaultInstance;

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

  @$pb.TagNumber(3)
  $core.bool get wasPermanentlyDeleted => $_getBF(2);
  @$pb.TagNumber(3)
  set wasPermanentlyDeleted($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasWasPermanentlyDeleted() => $_has(2);
  @$pb.TagNumber(3)
  void clearWasPermanentlyDeleted() => clearField(3);
}

/// Bulk delete (for mods)
class BulkDeleteRequest extends $pb.GeneratedMessage {
  factory BulkDeleteRequest({
    $core.Iterable<$core.String>? messageIds,
    $core.String? requesterId,
    $core.String? channelId,
  }) {
    final $result = create();
    if (messageIds != null) {
      $result.messageIds.addAll(messageIds);
    }
    if (requesterId != null) {
      $result.requesterId = requesterId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    return $result;
  }
  BulkDeleteRequest._() : super();
  factory BulkDeleteRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BulkDeleteRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BulkDeleteRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..pPS(1, _omitFieldNames ? '' : 'messageIds')
    ..aOS(2, _omitFieldNames ? '' : 'requesterId')
    ..aOS(3, _omitFieldNames ? '' : 'channelId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BulkDeleteRequest clone() => BulkDeleteRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BulkDeleteRequest copyWith(void Function(BulkDeleteRequest) updates) => super.copyWith((message) => updates(message as BulkDeleteRequest)) as BulkDeleteRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BulkDeleteRequest create() => BulkDeleteRequest._();
  BulkDeleteRequest createEmptyInstance() => create();
  static $pb.PbList<BulkDeleteRequest> createRepeated() => $pb.PbList<BulkDeleteRequest>();
  @$core.pragma('dart2js:noInline')
  static BulkDeleteRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BulkDeleteRequest>(create);
  static BulkDeleteRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.String> get messageIds => $_getList(0);

  @$pb.TagNumber(2)
  $core.String get requesterId => $_getSZ(1);
  @$pb.TagNumber(2)
  set requesterId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasRequesterId() => $_has(1);
  @$pb.TagNumber(2)
  void clearRequesterId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get channelId => $_getSZ(2);
  @$pb.TagNumber(3)
  set channelId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelId() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelId() => clearField(3);
}

class BulkDeleteResponse extends $pb.GeneratedMessage {
  factory BulkDeleteResponse({
    $0.ErrorCode? code,
    $core.int? deletedCount,
    $core.Iterable<$core.String>? failedMessageIds,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (deletedCount != null) {
      $result.deletedCount = deletedCount;
    }
    if (failedMessageIds != null) {
      $result.failedMessageIds.addAll(failedMessageIds);
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  BulkDeleteResponse._() : super();
  factory BulkDeleteResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BulkDeleteResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BulkDeleteResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..a<$core.int>(2, _omitFieldNames ? '' : 'deletedCount', $pb.PbFieldType.O3)
    ..pPS(3, _omitFieldNames ? '' : 'failedMessageIds')
    ..aInt64(4, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BulkDeleteResponse clone() => BulkDeleteResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BulkDeleteResponse copyWith(void Function(BulkDeleteResponse) updates) => super.copyWith((message) => updates(message as BulkDeleteResponse)) as BulkDeleteResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BulkDeleteResponse create() => BulkDeleteResponse._();
  BulkDeleteResponse createEmptyInstance() => create();
  static $pb.PbList<BulkDeleteResponse> createRepeated() => $pb.PbList<BulkDeleteResponse>();
  @$core.pragma('dart2js:noInline')
  static BulkDeleteResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BulkDeleteResponse>(create);
  static BulkDeleteResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.int get deletedCount => $_getIZ(1);
  @$pb.TagNumber(2)
  set deletedCount($core.int v) { $_setSignedInt32(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasDeletedCount() => $_has(1);
  @$pb.TagNumber(2)
  void clearDeletedCount() => clearField(2);

  @$pb.TagNumber(3)
  $core.List<$core.String> get failedMessageIds => $_getList(2);

  @$pb.TagNumber(4)
  $fixnum.Int64 get serverTime => $_getI64(3);
  @$pb.TagNumber(4)
  set serverTime($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasServerTime() => $_has(3);
  @$pb.TagNumber(4)
  void clearServerTime() => clearField(4);
}

/// Message edit notification
class MessageEditedNotify extends $pb.GeneratedMessage {
  factory MessageEditedNotify({
    $core.String? messageId,
    $core.String? channelId,
    $core.List<$core.int>? newContent,
    $fixnum.Int64? editedAt,
    $core.String? editedBy,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (newContent != null) {
      $result.newContent = newContent;
    }
    if (editedAt != null) {
      $result.editedAt = editedAt;
    }
    if (editedBy != null) {
      $result.editedBy = editedBy;
    }
    return $result;
  }
  MessageEditedNotify._() : super();
  factory MessageEditedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageEditedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageEditedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..a<$core.List<$core.int>>(3, _omitFieldNames ? '' : 'newContent', $pb.PbFieldType.OY)
    ..aInt64(4, _omitFieldNames ? '' : 'editedAt')
    ..aOS(5, _omitFieldNames ? '' : 'editedBy')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageEditedNotify clone() => MessageEditedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageEditedNotify copyWith(void Function(MessageEditedNotify) updates) => super.copyWith((message) => updates(message as MessageEditedNotify)) as MessageEditedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageEditedNotify create() => MessageEditedNotify._();
  MessageEditedNotify createEmptyInstance() => create();
  static $pb.PbList<MessageEditedNotify> createRepeated() => $pb.PbList<MessageEditedNotify>();
  @$core.pragma('dart2js:noInline')
  static MessageEditedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageEditedNotify>(create);
  static MessageEditedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  $core.List<$core.int> get newContent => $_getN(2);
  @$pb.TagNumber(3)
  set newContent($core.List<$core.int> v) { $_setBytes(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasNewContent() => $_has(2);
  @$pb.TagNumber(3)
  void clearNewContent() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get editedAt => $_getI64(3);
  @$pb.TagNumber(4)
  set editedAt($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasEditedAt() => $_has(3);
  @$pb.TagNumber(4)
  void clearEditedAt() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get editedBy => $_getSZ(4);
  @$pb.TagNumber(5)
  set editedBy($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasEditedBy() => $_has(4);
  @$pb.TagNumber(5)
  void clearEditedBy() => clearField(5);
}

/// Message delete notification
class MessageDeletedNotify extends $pb.GeneratedMessage {
  factory MessageDeletedNotify({
    $core.String? messageId,
    $core.String? channelId,
    $core.bool? isHardDelete,
    $core.String? deletedBy,
    $fixnum.Int64? deletedAt,
  }) {
    final $result = create();
    if (messageId != null) {
      $result.messageId = messageId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (isHardDelete != null) {
      $result.isHardDelete = isHardDelete;
    }
    if (deletedBy != null) {
      $result.deletedBy = deletedBy;
    }
    if (deletedAt != null) {
      $result.deletedAt = deletedAt;
    }
    return $result;
  }
  MessageDeletedNotify._() : super();
  factory MessageDeletedNotify.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory MessageDeletedNotify.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'MessageDeletedNotify', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'messageId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..aOB(3, _omitFieldNames ? '' : 'isHardDelete')
    ..aOS(4, _omitFieldNames ? '' : 'deletedBy')
    ..aInt64(5, _omitFieldNames ? '' : 'deletedAt')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  MessageDeletedNotify clone() => MessageDeletedNotify()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  MessageDeletedNotify copyWith(void Function(MessageDeletedNotify) updates) => super.copyWith((message) => updates(message as MessageDeletedNotify)) as MessageDeletedNotify;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static MessageDeletedNotify create() => MessageDeletedNotify._();
  MessageDeletedNotify createEmptyInstance() => create();
  static $pb.PbList<MessageDeletedNotify> createRepeated() => $pb.PbList<MessageDeletedNotify>();
  @$core.pragma('dart2js:noInline')
  static MessageDeletedNotify getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<MessageDeletedNotify>(create);
  static MessageDeletedNotify? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get messageId => $_getSZ(0);
  @$pb.TagNumber(1)
  set messageId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasMessageId() => $_has(0);
  @$pb.TagNumber(1)
  void clearMessageId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get isHardDelete => $_getBF(2);
  @$pb.TagNumber(3)
  set isHardDelete($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasIsHardDelete() => $_has(2);
  @$pb.TagNumber(3)
  void clearIsHardDelete() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get deletedBy => $_getSZ(3);
  @$pb.TagNumber(4)
  set deletedBy($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasDeletedBy() => $_has(3);
  @$pb.TagNumber(4)
  void clearDeletedBy() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get deletedAt => $_getI64(4);
  @$pb.TagNumber(5)
  set deletedAt($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasDeletedAt() => $_has(4);
  @$pb.TagNumber(5)
  void clearDeletedAt() => clearField(5);
}

/// Typing indicator (broadcast to channel)
class TypingIndicator extends $pb.GeneratedMessage {
  factory TypingIndicator({
    $core.String? channelId,
    ChannelType? channelType,
    $core.String? userId,
    $core.String? username,
    $core.bool? isTyping,
    $fixnum.Int64? timestamp,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    if (username != null) {
      $result.username = username;
    }
    if (isTyping != null) {
      $result.isTyping = isTyping;
    }
    if (timestamp != null) {
      $result.timestamp = timestamp;
    }
    return $result;
  }
  TypingIndicator._() : super();
  factory TypingIndicator.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory TypingIndicator.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'TypingIndicator', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(3, _omitFieldNames ? '' : 'userId')
    ..aOS(4, _omitFieldNames ? '' : 'username')
    ..aOB(5, _omitFieldNames ? '' : 'isTyping')
    ..aInt64(6, _omitFieldNames ? '' : 'timestamp')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  TypingIndicator clone() => TypingIndicator()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  TypingIndicator copyWith(void Function(TypingIndicator) updates) => super.copyWith((message) => updates(message as TypingIndicator)) as TypingIndicator;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TypingIndicator create() => TypingIndicator._();
  TypingIndicator createEmptyInstance() => create();
  static $pb.PbList<TypingIndicator> createRepeated() => $pb.PbList<TypingIndicator>();
  @$core.pragma('dart2js:noInline')
  static TypingIndicator getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<TypingIndicator>(create);
  static TypingIndicator? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get userId => $_getSZ(2);
  @$pb.TagNumber(3)
  set userId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasUserId() => $_has(2);
  @$pb.TagNumber(3)
  void clearUserId() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get username => $_getSZ(3);
  @$pb.TagNumber(4)
  set username($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasUsername() => $_has(3);
  @$pb.TagNumber(4)
  void clearUsername() => clearField(4);

  @$pb.TagNumber(5)
  $core.bool get isTyping => $_getBF(4);
  @$pb.TagNumber(5)
  set isTyping($core.bool v) { $_setBool(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasIsTyping() => $_has(4);
  @$pb.TagNumber(5)
  void clearIsTyping() => clearField(5);

  @$pb.TagNumber(6)
  $fixnum.Int64 get timestamp => $_getI64(5);
  @$pb.TagNumber(6)
  set timestamp($fixnum.Int64 v) { $_setInt64(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasTimestamp() => $_has(5);
  @$pb.TagNumber(6)
  void clearTimestamp() => clearField(6);
}

/// Get currently typing users in a channel
class GetTypingUsersRequest extends $pb.GeneratedMessage {
  factory GetTypingUsersRequest({
    $core.String? channelId,
    ChannelType? channelType,
  }) {
    final $result = create();
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    return $result;
  }
  GetTypingUsersRequest._() : super();
  factory GetTypingUsersRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetTypingUsersRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetTypingUsersRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'channelId')
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetTypingUsersRequest clone() => GetTypingUsersRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetTypingUsersRequest copyWith(void Function(GetTypingUsersRequest) updates) => super.copyWith((message) => updates(message as GetTypingUsersRequest)) as GetTypingUsersRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetTypingUsersRequest create() => GetTypingUsersRequest._();
  GetTypingUsersRequest createEmptyInstance() => create();
  static $pb.PbList<GetTypingUsersRequest> createRepeated() => $pb.PbList<GetTypingUsersRequest>();
  @$core.pragma('dart2js:noInline')
  static GetTypingUsersRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetTypingUsersRequest>(create);
  static GetTypingUsersRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get channelId => $_getSZ(0);
  @$pb.TagNumber(1)
  set channelId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelId() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelId() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);
}

class GetTypingUsersResponse extends $pb.GeneratedMessage {
  factory GetTypingUsersResponse({
    $0.ErrorCode? code,
    $core.Iterable<$core.String>? typingUserIds,
    $core.Iterable<$core.String>? usernames,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (typingUserIds != null) {
      $result.typingUserIds.addAll(typingUserIds);
    }
    if (usernames != null) {
      $result.usernames.addAll(usernames);
    }
    return $result;
  }
  GetTypingUsersResponse._() : super();
  factory GetTypingUsersResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetTypingUsersResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetTypingUsersResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pPS(2, _omitFieldNames ? '' : 'typingUserIds')
    ..pPS(3, _omitFieldNames ? '' : 'usernames')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetTypingUsersResponse clone() => GetTypingUsersResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetTypingUsersResponse copyWith(void Function(GetTypingUsersResponse) updates) => super.copyWith((message) => updates(message as GetTypingUsersResponse)) as GetTypingUsersResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetTypingUsersResponse create() => GetTypingUsersResponse._();
  GetTypingUsersResponse createEmptyInstance() => create();
  static $pb.PbList<GetTypingUsersResponse> createRepeated() => $pb.PbList<GetTypingUsersResponse>();
  @$core.pragma('dart2js:noInline')
  static GetTypingUsersResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetTypingUsersResponse>(create);
  static GetTypingUsersResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<$core.String> get typingUserIds => $_getList(1);

  @$pb.TagNumber(3)
  $core.List<$core.String> get usernames => $_getList(2);
}

/// File info
class FileInfo extends $pb.GeneratedMessage {
  factory FileInfo({
    $core.String? fileId,
    $core.String? filename,
    $fixnum.Int64? fileSize,
    $core.String? mimeType,
    $core.String? checksum,
    $core.String? storageUrl,
    $fixnum.Int64? uploadedAt,
    $core.String? uploadedBy,
    $core.int? width,
    $core.int? height,
    $core.int? duration,
  }) {
    final $result = create();
    if (fileId != null) {
      $result.fileId = fileId;
    }
    if (filename != null) {
      $result.filename = filename;
    }
    if (fileSize != null) {
      $result.fileSize = fileSize;
    }
    if (mimeType != null) {
      $result.mimeType = mimeType;
    }
    if (checksum != null) {
      $result.checksum = checksum;
    }
    if (storageUrl != null) {
      $result.storageUrl = storageUrl;
    }
    if (uploadedAt != null) {
      $result.uploadedAt = uploadedAt;
    }
    if (uploadedBy != null) {
      $result.uploadedBy = uploadedBy;
    }
    if (width != null) {
      $result.width = width;
    }
    if (height != null) {
      $result.height = height;
    }
    if (duration != null) {
      $result.duration = duration;
    }
    return $result;
  }
  FileInfo._() : super();
  factory FileInfo.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FileInfo.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FileInfo', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'fileId')
    ..aOS(2, _omitFieldNames ? '' : 'filename')
    ..aInt64(3, _omitFieldNames ? '' : 'fileSize')
    ..aOS(4, _omitFieldNames ? '' : 'mimeType')
    ..aOS(5, _omitFieldNames ? '' : 'checksum')
    ..aOS(6, _omitFieldNames ? '' : 'storageUrl')
    ..aInt64(7, _omitFieldNames ? '' : 'uploadedAt')
    ..aOS(8, _omitFieldNames ? '' : 'uploadedBy')
    ..a<$core.int>(9, _omitFieldNames ? '' : 'width', $pb.PbFieldType.O3)
    ..a<$core.int>(10, _omitFieldNames ? '' : 'height', $pb.PbFieldType.O3)
    ..a<$core.int>(11, _omitFieldNames ? '' : 'duration', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FileInfo clone() => FileInfo()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FileInfo copyWith(void Function(FileInfo) updates) => super.copyWith((message) => updates(message as FileInfo)) as FileInfo;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FileInfo create() => FileInfo._();
  FileInfo createEmptyInstance() => create();
  static $pb.PbList<FileInfo> createRepeated() => $pb.PbList<FileInfo>();
  @$core.pragma('dart2js:noInline')
  static FileInfo getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FileInfo>(create);
  static FileInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get fileId => $_getSZ(0);
  @$pb.TagNumber(1)
  set fileId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasFileId() => $_has(0);
  @$pb.TagNumber(1)
  void clearFileId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get filename => $_getSZ(1);
  @$pb.TagNumber(2)
  set filename($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFilename() => $_has(1);
  @$pb.TagNumber(2)
  void clearFilename() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get fileSize => $_getI64(2);
  @$pb.TagNumber(3)
  set fileSize($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasFileSize() => $_has(2);
  @$pb.TagNumber(3)
  void clearFileSize() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get mimeType => $_getSZ(3);
  @$pb.TagNumber(4)
  set mimeType($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasMimeType() => $_has(3);
  @$pb.TagNumber(4)
  void clearMimeType() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get checksum => $_getSZ(4);
  @$pb.TagNumber(5)
  set checksum($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasChecksum() => $_has(4);
  @$pb.TagNumber(5)
  void clearChecksum() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get storageUrl => $_getSZ(5);
  @$pb.TagNumber(6)
  set storageUrl($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasStorageUrl() => $_has(5);
  @$pb.TagNumber(6)
  void clearStorageUrl() => clearField(6);

  @$pb.TagNumber(7)
  $fixnum.Int64 get uploadedAt => $_getI64(6);
  @$pb.TagNumber(7)
  set uploadedAt($fixnum.Int64 v) { $_setInt64(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasUploadedAt() => $_has(6);
  @$pb.TagNumber(7)
  void clearUploadedAt() => clearField(7);

  @$pb.TagNumber(8)
  $core.String get uploadedBy => $_getSZ(7);
  @$pb.TagNumber(8)
  set uploadedBy($core.String v) { $_setString(7, v); }
  @$pb.TagNumber(8)
  $core.bool hasUploadedBy() => $_has(7);
  @$pb.TagNumber(8)
  void clearUploadedBy() => clearField(8);

  @$pb.TagNumber(9)
  $core.int get width => $_getIZ(8);
  @$pb.TagNumber(9)
  set width($core.int v) { $_setSignedInt32(8, v); }
  @$pb.TagNumber(9)
  $core.bool hasWidth() => $_has(8);
  @$pb.TagNumber(9)
  void clearWidth() => clearField(9);

  @$pb.TagNumber(10)
  $core.int get height => $_getIZ(9);
  @$pb.TagNumber(10)
  set height($core.int v) { $_setSignedInt32(9, v); }
  @$pb.TagNumber(10)
  $core.bool hasHeight() => $_has(9);
  @$pb.TagNumber(10)
  void clearHeight() => clearField(10);

  @$pb.TagNumber(11)
  $core.int get duration => $_getIZ(10);
  @$pb.TagNumber(11)
  set duration($core.int v) { $_setSignedInt32(10, v); }
  @$pb.TagNumber(11)
  $core.bool hasDuration() => $_has(10);
  @$pb.TagNumber(11)
  void clearDuration() => clearField(11);
}

/// Upload file request (first step - get upload URL)
class PrepareFileUploadRequest extends $pb.GeneratedMessage {
  factory PrepareFileUploadRequest({
    $core.String? userId,
    $core.String? channelId,
    ChannelType? channelType,
    $core.String? filename,
    $fixnum.Int64? fileSize,
    $core.String? mimeType,
    $core.String? checksum,
  }) {
    final $result = create();
    if (userId != null) {
      $result.userId = userId;
    }
    if (channelId != null) {
      $result.channelId = channelId;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (filename != null) {
      $result.filename = filename;
    }
    if (fileSize != null) {
      $result.fileSize = fileSize;
    }
    if (mimeType != null) {
      $result.mimeType = mimeType;
    }
    if (checksum != null) {
      $result.checksum = checksum;
    }
    return $result;
  }
  PrepareFileUploadRequest._() : super();
  factory PrepareFileUploadRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PrepareFileUploadRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PrepareFileUploadRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'userId')
    ..aOS(2, _omitFieldNames ? '' : 'channelId')
    ..e<ChannelType>(3, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOS(4, _omitFieldNames ? '' : 'filename')
    ..aInt64(5, _omitFieldNames ? '' : 'fileSize')
    ..aOS(6, _omitFieldNames ? '' : 'mimeType')
    ..aOS(7, _omitFieldNames ? '' : 'checksum')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PrepareFileUploadRequest clone() => PrepareFileUploadRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PrepareFileUploadRequest copyWith(void Function(PrepareFileUploadRequest) updates) => super.copyWith((message) => updates(message as PrepareFileUploadRequest)) as PrepareFileUploadRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PrepareFileUploadRequest create() => PrepareFileUploadRequest._();
  PrepareFileUploadRequest createEmptyInstance() => create();
  static $pb.PbList<PrepareFileUploadRequest> createRepeated() => $pb.PbList<PrepareFileUploadRequest>();
  @$core.pragma('dart2js:noInline')
  static PrepareFileUploadRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PrepareFileUploadRequest>(create);
  static PrepareFileUploadRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get userId => $_getSZ(0);
  @$pb.TagNumber(1)
  set userId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUserId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get channelId => $_getSZ(1);
  @$pb.TagNumber(2)
  set channelId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelId() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelId() => clearField(2);

  @$pb.TagNumber(3)
  ChannelType get channelType => $_getN(2);
  @$pb.TagNumber(3)
  set channelType(ChannelType v) { setField(3, v); }
  @$pb.TagNumber(3)
  $core.bool hasChannelType() => $_has(2);
  @$pb.TagNumber(3)
  void clearChannelType() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get filename => $_getSZ(3);
  @$pb.TagNumber(4)
  set filename($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasFilename() => $_has(3);
  @$pb.TagNumber(4)
  void clearFilename() => clearField(4);

  @$pb.TagNumber(5)
  $fixnum.Int64 get fileSize => $_getI64(4);
  @$pb.TagNumber(5)
  set fileSize($fixnum.Int64 v) { $_setInt64(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasFileSize() => $_has(4);
  @$pb.TagNumber(5)
  void clearFileSize() => clearField(5);

  @$pb.TagNumber(6)
  $core.String get mimeType => $_getSZ(5);
  @$pb.TagNumber(6)
  set mimeType($core.String v) { $_setString(5, v); }
  @$pb.TagNumber(6)
  $core.bool hasMimeType() => $_has(5);
  @$pb.TagNumber(6)
  void clearMimeType() => clearField(6);

  @$pb.TagNumber(7)
  $core.String get checksum => $_getSZ(6);
  @$pb.TagNumber(7)
  set checksum($core.String v) { $_setString(6, v); }
  @$pb.TagNumber(7)
  $core.bool hasChecksum() => $_has(6);
  @$pb.TagNumber(7)
  void clearChecksum() => clearField(7);
}

class PrepareFileUploadResponse extends $pb.GeneratedMessage {
  factory PrepareFileUploadResponse({
    $0.ErrorCode? code,
    $core.String? uploadId,
    $core.String? uploadUrl,
    $fixnum.Int64? expiresAt,
    $core.String? fileId,
    $core.Map<$core.String, $core.String>? headers,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (uploadId != null) {
      $result.uploadId = uploadId;
    }
    if (uploadUrl != null) {
      $result.uploadUrl = uploadUrl;
    }
    if (expiresAt != null) {
      $result.expiresAt = expiresAt;
    }
    if (fileId != null) {
      $result.fileId = fileId;
    }
    if (headers != null) {
      $result.headers.addAll(headers);
    }
    return $result;
  }
  PrepareFileUploadResponse._() : super();
  factory PrepareFileUploadResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory PrepareFileUploadResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'PrepareFileUploadResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'uploadId')
    ..aOS(3, _omitFieldNames ? '' : 'uploadUrl')
    ..aInt64(4, _omitFieldNames ? '' : 'expiresAt')
    ..aOS(5, _omitFieldNames ? '' : 'fileId')
    ..m<$core.String, $core.String>(6, _omitFieldNames ? '' : 'headers', entryClassName: 'PrepareFileUploadResponse.HeadersEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.chat'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  PrepareFileUploadResponse clone() => PrepareFileUploadResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  PrepareFileUploadResponse copyWith(void Function(PrepareFileUploadResponse) updates) => super.copyWith((message) => updates(message as PrepareFileUploadResponse)) as PrepareFileUploadResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PrepareFileUploadResponse create() => PrepareFileUploadResponse._();
  PrepareFileUploadResponse createEmptyInstance() => create();
  static $pb.PbList<PrepareFileUploadResponse> createRepeated() => $pb.PbList<PrepareFileUploadResponse>();
  @$core.pragma('dart2js:noInline')
  static PrepareFileUploadResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PrepareFileUploadResponse>(create);
  static PrepareFileUploadResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get uploadId => $_getSZ(1);
  @$pb.TagNumber(2)
  set uploadId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUploadId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUploadId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get uploadUrl => $_getSZ(2);
  @$pb.TagNumber(3)
  set uploadUrl($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasUploadUrl() => $_has(2);
  @$pb.TagNumber(3)
  void clearUploadUrl() => clearField(3);

  @$pb.TagNumber(4)
  $fixnum.Int64 get expiresAt => $_getI64(3);
  @$pb.TagNumber(4)
  set expiresAt($fixnum.Int64 v) { $_setInt64(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasExpiresAt() => $_has(3);
  @$pb.TagNumber(4)
  void clearExpiresAt() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get fileId => $_getSZ(4);
  @$pb.TagNumber(5)
  set fileId($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasFileId() => $_has(4);
  @$pb.TagNumber(5)
  void clearFileId() => clearField(5);

  @$pb.TagNumber(6)
  $core.Map<$core.String, $core.String> get headers => $_getMap(5);
}

/// Confirm file upload (after upload completes)
class ConfirmFileUploadRequest extends $pb.GeneratedMessage {
  factory ConfirmFileUploadRequest({
    $core.String? uploadId,
    $core.String? fileId,
    $core.String? messageId,
  }) {
    final $result = create();
    if (uploadId != null) {
      $result.uploadId = uploadId;
    }
    if (fileId != null) {
      $result.fileId = fileId;
    }
    if (messageId != null) {
      $result.messageId = messageId;
    }
    return $result;
  }
  ConfirmFileUploadRequest._() : super();
  factory ConfirmFileUploadRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ConfirmFileUploadRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ConfirmFileUploadRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'uploadId')
    ..aOS(2, _omitFieldNames ? '' : 'fileId')
    ..aOS(3, _omitFieldNames ? '' : 'messageId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ConfirmFileUploadRequest clone() => ConfirmFileUploadRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ConfirmFileUploadRequest copyWith(void Function(ConfirmFileUploadRequest) updates) => super.copyWith((message) => updates(message as ConfirmFileUploadRequest)) as ConfirmFileUploadRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ConfirmFileUploadRequest create() => ConfirmFileUploadRequest._();
  ConfirmFileUploadRequest createEmptyInstance() => create();
  static $pb.PbList<ConfirmFileUploadRequest> createRepeated() => $pb.PbList<ConfirmFileUploadRequest>();
  @$core.pragma('dart2js:noInline')
  static ConfirmFileUploadRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ConfirmFileUploadRequest>(create);
  static ConfirmFileUploadRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get uploadId => $_getSZ(0);
  @$pb.TagNumber(1)
  set uploadId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasUploadId() => $_has(0);
  @$pb.TagNumber(1)
  void clearUploadId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get fileId => $_getSZ(1);
  @$pb.TagNumber(2)
  set fileId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasFileId() => $_has(1);
  @$pb.TagNumber(2)
  void clearFileId() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get messageId => $_getSZ(2);
  @$pb.TagNumber(3)
  set messageId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMessageId() => $_has(2);
  @$pb.TagNumber(3)
  void clearMessageId() => clearField(3);
}

class ConfirmFileUploadResponse extends $pb.GeneratedMessage {
  factory ConfirmFileUploadResponse({
    $0.ErrorCode? code,
    FileInfo? fileInfo,
    $fixnum.Int64? serverTime,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (fileInfo != null) {
      $result.fileInfo = fileInfo;
    }
    if (serverTime != null) {
      $result.serverTime = serverTime;
    }
    return $result;
  }
  ConfirmFileUploadResponse._() : super();
  factory ConfirmFileUploadResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ConfirmFileUploadResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ConfirmFileUploadResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOM<FileInfo>(2, _omitFieldNames ? '' : 'fileInfo', subBuilder: FileInfo.create)
    ..aInt64(3, _omitFieldNames ? '' : 'serverTime')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ConfirmFileUploadResponse clone() => ConfirmFileUploadResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ConfirmFileUploadResponse copyWith(void Function(ConfirmFileUploadResponse) updates) => super.copyWith((message) => updates(message as ConfirmFileUploadResponse)) as ConfirmFileUploadResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ConfirmFileUploadResponse create() => ConfirmFileUploadResponse._();
  ConfirmFileUploadResponse createEmptyInstance() => create();
  static $pb.PbList<ConfirmFileUploadResponse> createRepeated() => $pb.PbList<ConfirmFileUploadResponse>();
  @$core.pragma('dart2js:noInline')
  static ConfirmFileUploadResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ConfirmFileUploadResponse>(create);
  static ConfirmFileUploadResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  FileInfo get fileInfo => $_getN(1);
  @$pb.TagNumber(2)
  set fileInfo(FileInfo v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasFileInfo() => $_has(1);
  @$pb.TagNumber(2)
  void clearFileInfo() => clearField(2);
  @$pb.TagNumber(2)
  FileInfo ensureFileInfo() => $_ensure(1);

  @$pb.TagNumber(3)
  $fixnum.Int64 get serverTime => $_getI64(2);
  @$pb.TagNumber(3)
  set serverTime($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasServerTime() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerTime() => clearField(3);
}

/// Download file request (get download URL)
class GetFileDownloadRequest extends $pb.GeneratedMessage {
  factory GetFileDownloadRequest({
    $core.String? fileId,
    $core.String? userId,
  }) {
    final $result = create();
    if (fileId != null) {
      $result.fileId = fileId;
    }
    if (userId != null) {
      $result.userId = userId;
    }
    return $result;
  }
  GetFileDownloadRequest._() : super();
  factory GetFileDownloadRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetFileDownloadRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetFileDownloadRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'fileId')
    ..aOS(2, _omitFieldNames ? '' : 'userId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetFileDownloadRequest clone() => GetFileDownloadRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetFileDownloadRequest copyWith(void Function(GetFileDownloadRequest) updates) => super.copyWith((message) => updates(message as GetFileDownloadRequest)) as GetFileDownloadRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetFileDownloadRequest create() => GetFileDownloadRequest._();
  GetFileDownloadRequest createEmptyInstance() => create();
  static $pb.PbList<GetFileDownloadRequest> createRepeated() => $pb.PbList<GetFileDownloadRequest>();
  @$core.pragma('dart2js:noInline')
  static GetFileDownloadRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetFileDownloadRequest>(create);
  static GetFileDownloadRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get fileId => $_getSZ(0);
  @$pb.TagNumber(1)
  set fileId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasFileId() => $_has(0);
  @$pb.TagNumber(1)
  void clearFileId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get userId => $_getSZ(1);
  @$pb.TagNumber(2)
  set userId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearUserId() => clearField(2);
}

class GetFileDownloadResponse extends $pb.GeneratedMessage {
  factory GetFileDownloadResponse({
    $0.ErrorCode? code,
    $core.String? downloadUrl,
    $fixnum.Int64? expiresAt,
    FileInfo? fileInfo,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (downloadUrl != null) {
      $result.downloadUrl = downloadUrl;
    }
    if (expiresAt != null) {
      $result.expiresAt = expiresAt;
    }
    if (fileInfo != null) {
      $result.fileInfo = fileInfo;
    }
    return $result;
  }
  GetFileDownloadResponse._() : super();
  factory GetFileDownloadResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetFileDownloadResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetFileDownloadResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'downloadUrl')
    ..aInt64(3, _omitFieldNames ? '' : 'expiresAt')
    ..aOM<FileInfo>(4, _omitFieldNames ? '' : 'fileInfo', subBuilder: FileInfo.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetFileDownloadResponse clone() => GetFileDownloadResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetFileDownloadResponse copyWith(void Function(GetFileDownloadResponse) updates) => super.copyWith((message) => updates(message as GetFileDownloadResponse)) as GetFileDownloadResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetFileDownloadResponse create() => GetFileDownloadResponse._();
  GetFileDownloadResponse createEmptyInstance() => create();
  static $pb.PbList<GetFileDownloadResponse> createRepeated() => $pb.PbList<GetFileDownloadResponse>();
  @$core.pragma('dart2js:noInline')
  static GetFileDownloadResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetFileDownloadResponse>(create);
  static GetFileDownloadResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get downloadUrl => $_getSZ(1);
  @$pb.TagNumber(2)
  set downloadUrl($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasDownloadUrl() => $_has(1);
  @$pb.TagNumber(2)
  void clearDownloadUrl() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get expiresAt => $_getI64(2);
  @$pb.TagNumber(3)
  set expiresAt($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasExpiresAt() => $_has(2);
  @$pb.TagNumber(3)
  void clearExpiresAt() => clearField(3);

  @$pb.TagNumber(4)
  FileInfo get fileInfo => $_getN(3);
  @$pb.TagNumber(4)
  set fileInfo(FileInfo v) { setField(4, v); }
  @$pb.TagNumber(4)
  $core.bool hasFileInfo() => $_has(3);
  @$pb.TagNumber(4)
  void clearFileInfo() => clearField(4);
  @$pb.TagNumber(4)
  FileInfo ensureFileInfo() => $_ensure(3);
}

/// File attachment in message
class FileAttachment extends $pb.GeneratedMessage {
  factory FileAttachment({
    FileInfo? file,
    $core.bool? isSpoiler,
    $core.String? altText,
  }) {
    final $result = create();
    if (file != null) {
      $result.file = file;
    }
    if (isSpoiler != null) {
      $result.isSpoiler = isSpoiler;
    }
    if (altText != null) {
      $result.altText = altText;
    }
    return $result;
  }
  FileAttachment._() : super();
  factory FileAttachment.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FileAttachment.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FileAttachment', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOM<FileInfo>(1, _omitFieldNames ? '' : 'file', subBuilder: FileInfo.create)
    ..aOB(2, _omitFieldNames ? '' : 'isSpoiler')
    ..aOS(3, _omitFieldNames ? '' : 'altText')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FileAttachment clone() => FileAttachment()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FileAttachment copyWith(void Function(FileAttachment) updates) => super.copyWith((message) => updates(message as FileAttachment)) as FileAttachment;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FileAttachment create() => FileAttachment._();
  FileAttachment createEmptyInstance() => create();
  static $pb.PbList<FileAttachment> createRepeated() => $pb.PbList<FileAttachment>();
  @$core.pragma('dart2js:noInline')
  static FileAttachment getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FileAttachment>(create);
  static FileAttachment? _defaultInstance;

  @$pb.TagNumber(1)
  FileInfo get file => $_getN(0);
  @$pb.TagNumber(1)
  set file(FileInfo v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasFile() => $_has(0);
  @$pb.TagNumber(1)
  void clearFile() => clearField(1);
  @$pb.TagNumber(1)
  FileInfo ensureFile() => $_ensure(0);

  @$pb.TagNumber(2)
  $core.bool get isSpoiler => $_getBF(1);
  @$pb.TagNumber(2)
  set isSpoiler($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasIsSpoiler() => $_has(1);
  @$pb.TagNumber(2)
  void clearIsSpoiler() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get altText => $_getSZ(2);
  @$pb.TagNumber(3)
  set altText($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasAltText() => $_has(2);
  @$pb.TagNumber(3)
  void clearAltText() => clearField(3);
}

/// Message with file attachment
class FileMessage extends $pb.GeneratedMessage {
  factory FileMessage({
    ChatMessage? baseMessage,
    $core.Iterable<FileAttachment>? attachments,
  }) {
    final $result = create();
    if (baseMessage != null) {
      $result.baseMessage = baseMessage;
    }
    if (attachments != null) {
      $result.attachments.addAll(attachments);
    }
    return $result;
  }
  FileMessage._() : super();
  factory FileMessage.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory FileMessage.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'FileMessage', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOM<ChatMessage>(1, _omitFieldNames ? '' : 'baseMessage', subBuilder: ChatMessage.create)
    ..pc<FileAttachment>(2, _omitFieldNames ? '' : 'attachments', $pb.PbFieldType.PM, subBuilder: FileAttachment.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  FileMessage clone() => FileMessage()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  FileMessage copyWith(void Function(FileMessage) updates) => super.copyWith((message) => updates(message as FileMessage)) as FileMessage;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FileMessage create() => FileMessage._();
  FileMessage createEmptyInstance() => create();
  static $pb.PbList<FileMessage> createRepeated() => $pb.PbList<FileMessage>();
  @$core.pragma('dart2js:noInline')
  static FileMessage getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<FileMessage>(create);
  static FileMessage? _defaultInstance;

  @$pb.TagNumber(1)
  ChatMessage get baseMessage => $_getN(0);
  @$pb.TagNumber(1)
  set baseMessage(ChatMessage v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasBaseMessage() => $_has(0);
  @$pb.TagNumber(1)
  void clearBaseMessage() => clearField(1);
  @$pb.TagNumber(1)
  ChatMessage ensureBaseMessage() => $_ensure(0);

  @$pb.TagNumber(2)
  $core.List<FileAttachment> get attachments => $_getList(1);
}

/// 物品链接（msg_type = ITEM_LINK 时，metadata 序列化为此消息）
class ItemMetadata extends $pb.GeneratedMessage {
  factory ItemMetadata({
    $core.String? itemId,
    $core.String? itemName,
    $core.int? quality,
    $core.String? iconUrl,
    $core.int? count,
    $core.Map<$core.String, $core.String>? attrs,
  }) {
    final $result = create();
    if (itemId != null) {
      $result.itemId = itemId;
    }
    if (itemName != null) {
      $result.itemName = itemName;
    }
    if (quality != null) {
      $result.quality = quality;
    }
    if (iconUrl != null) {
      $result.iconUrl = iconUrl;
    }
    if (count != null) {
      $result.count = count;
    }
    if (attrs != null) {
      $result.attrs.addAll(attrs);
    }
    return $result;
  }
  ItemMetadata._() : super();
  factory ItemMetadata.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ItemMetadata.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ItemMetadata', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'itemId')
    ..aOS(2, _omitFieldNames ? '' : 'itemName')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'quality', $pb.PbFieldType.O3)
    ..aOS(4, _omitFieldNames ? '' : 'iconUrl')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'count', $pb.PbFieldType.O3)
    ..m<$core.String, $core.String>(6, _omitFieldNames ? '' : 'attrs', entryClassName: 'ItemMetadata.AttrsEntry', keyFieldType: $pb.PbFieldType.OS, valueFieldType: $pb.PbFieldType.OS, packageName: const $pb.PackageName('chirp.chat'))
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ItemMetadata clone() => ItemMetadata()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ItemMetadata copyWith(void Function(ItemMetadata) updates) => super.copyWith((message) => updates(message as ItemMetadata)) as ItemMetadata;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ItemMetadata create() => ItemMetadata._();
  ItemMetadata createEmptyInstance() => create();
  static $pb.PbList<ItemMetadata> createRepeated() => $pb.PbList<ItemMetadata>();
  @$core.pragma('dart2js:noInline')
  static ItemMetadata getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ItemMetadata>(create);
  static ItemMetadata? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get itemId => $_getSZ(0);
  @$pb.TagNumber(1)
  set itemId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasItemId() => $_has(0);
  @$pb.TagNumber(1)
  void clearItemId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get itemName => $_getSZ(1);
  @$pb.TagNumber(2)
  set itemName($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasItemName() => $_has(1);
  @$pb.TagNumber(2)
  void clearItemName() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get quality => $_getIZ(2);
  @$pb.TagNumber(3)
  set quality($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasQuality() => $_has(2);
  @$pb.TagNumber(3)
  void clearQuality() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get iconUrl => $_getSZ(3);
  @$pb.TagNumber(4)
  set iconUrl($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasIconUrl() => $_has(3);
  @$pb.TagNumber(4)
  void clearIconUrl() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get count => $_getIZ(4);
  @$pb.TagNumber(5)
  set count($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasCount() => $_has(4);
  @$pb.TagNumber(5)
  void clearCount() => clearField(5);

  @$pb.TagNumber(6)
  $core.Map<$core.String, $core.String> get attrs => $_getMap(5);
}

/// 技能链接（msg_type = SKILL_LINK 时）
class SkillMetadata extends $pb.GeneratedMessage {
  factory SkillMetadata({
    $core.String? skillId,
    $core.String? skillName,
    $core.int? level,
    $core.String? iconUrl,
    $core.String? description,
  }) {
    final $result = create();
    if (skillId != null) {
      $result.skillId = skillId;
    }
    if (skillName != null) {
      $result.skillName = skillName;
    }
    if (level != null) {
      $result.level = level;
    }
    if (iconUrl != null) {
      $result.iconUrl = iconUrl;
    }
    if (description != null) {
      $result.description = description;
    }
    return $result;
  }
  SkillMetadata._() : super();
  factory SkillMetadata.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SkillMetadata.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SkillMetadata', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'skillId')
    ..aOS(2, _omitFieldNames ? '' : 'skillName')
    ..a<$core.int>(3, _omitFieldNames ? '' : 'level', $pb.PbFieldType.O3)
    ..aOS(4, _omitFieldNames ? '' : 'iconUrl')
    ..aOS(5, _omitFieldNames ? '' : 'description')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SkillMetadata clone() => SkillMetadata()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SkillMetadata copyWith(void Function(SkillMetadata) updates) => super.copyWith((message) => updates(message as SkillMetadata)) as SkillMetadata;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SkillMetadata create() => SkillMetadata._();
  SkillMetadata createEmptyInstance() => create();
  static $pb.PbList<SkillMetadata> createRepeated() => $pb.PbList<SkillMetadata>();
  @$core.pragma('dart2js:noInline')
  static SkillMetadata getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SkillMetadata>(create);
  static SkillMetadata? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get skillId => $_getSZ(0);
  @$pb.TagNumber(1)
  set skillId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasSkillId() => $_has(0);
  @$pb.TagNumber(1)
  void clearSkillId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get skillName => $_getSZ(1);
  @$pb.TagNumber(2)
  set skillName($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasSkillName() => $_has(1);
  @$pb.TagNumber(2)
  void clearSkillName() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get level => $_getIZ(2);
  @$pb.TagNumber(3)
  set level($core.int v) { $_setSignedInt32(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasLevel() => $_has(2);
  @$pb.TagNumber(3)
  void clearLevel() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get iconUrl => $_getSZ(3);
  @$pb.TagNumber(4)
  set iconUrl($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasIconUrl() => $_has(3);
  @$pb.TagNumber(4)
  void clearIconUrl() => clearField(4);

  @$pb.TagNumber(5)
  $core.String get description => $_getSZ(4);
  @$pb.TagNumber(5)
  set description($core.String v) { $_setString(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasDescription() => $_has(4);
  @$pb.TagNumber(5)
  void clearDescription() => clearField(5);
}

/// 成就分享（msg_type = ACHIEVEMENT 时）
class AchievementMetadata extends $pb.GeneratedMessage {
  factory AchievementMetadata({
    $core.String? achievementId,
    $core.String? achievementName,
    $core.String? description,
    $core.String? iconUrl,
    $core.int? rarity,
  }) {
    final $result = create();
    if (achievementId != null) {
      $result.achievementId = achievementId;
    }
    if (achievementName != null) {
      $result.achievementName = achievementName;
    }
    if (description != null) {
      $result.description = description;
    }
    if (iconUrl != null) {
      $result.iconUrl = iconUrl;
    }
    if (rarity != null) {
      $result.rarity = rarity;
    }
    return $result;
  }
  AchievementMetadata._() : super();
  factory AchievementMetadata.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory AchievementMetadata.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'AchievementMetadata', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'achievementId')
    ..aOS(2, _omitFieldNames ? '' : 'achievementName')
    ..aOS(3, _omitFieldNames ? '' : 'description')
    ..aOS(4, _omitFieldNames ? '' : 'iconUrl')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'rarity', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  AchievementMetadata clone() => AchievementMetadata()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  AchievementMetadata copyWith(void Function(AchievementMetadata) updates) => super.copyWith((message) => updates(message as AchievementMetadata)) as AchievementMetadata;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AchievementMetadata create() => AchievementMetadata._();
  AchievementMetadata createEmptyInstance() => create();
  static $pb.PbList<AchievementMetadata> createRepeated() => $pb.PbList<AchievementMetadata>();
  @$core.pragma('dart2js:noInline')
  static AchievementMetadata getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<AchievementMetadata>(create);
  static AchievementMetadata? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get achievementId => $_getSZ(0);
  @$pb.TagNumber(1)
  set achievementId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasAchievementId() => $_has(0);
  @$pb.TagNumber(1)
  void clearAchievementId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get achievementName => $_getSZ(1);
  @$pb.TagNumber(2)
  set achievementName($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasAchievementName() => $_has(1);
  @$pb.TagNumber(2)
  void clearAchievementName() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get description => $_getSZ(2);
  @$pb.TagNumber(3)
  set description($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasDescription() => $_has(2);
  @$pb.TagNumber(3)
  void clearDescription() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get iconUrl => $_getSZ(3);
  @$pb.TagNumber(4)
  set iconUrl($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasIconUrl() => $_has(3);
  @$pb.TagNumber(4)
  void clearIconUrl() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get rarity => $_getIZ(4);
  @$pb.TagNumber(5)
  set rarity($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasRarity() => $_has(4);
  @$pb.TagNumber(5)
  void clearRarity() => clearField(5);
}

/// 交易状态（msg_type = TRADE_STATUS 时）
class TradeMetadata extends $pb.GeneratedMessage {
  factory TradeMetadata({
    $core.String? tradeId,
    $core.String? status,
    $fixnum.Int64? amount,
    $core.String? itemName,
    $core.int? itemCount,
  }) {
    final $result = create();
    if (tradeId != null) {
      $result.tradeId = tradeId;
    }
    if (status != null) {
      $result.status = status;
    }
    if (amount != null) {
      $result.amount = amount;
    }
    if (itemName != null) {
      $result.itemName = itemName;
    }
    if (itemCount != null) {
      $result.itemCount = itemCount;
    }
    return $result;
  }
  TradeMetadata._() : super();
  factory TradeMetadata.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory TradeMetadata.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'TradeMetadata', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'tradeId')
    ..aOS(2, _omitFieldNames ? '' : 'status')
    ..aInt64(3, _omitFieldNames ? '' : 'amount')
    ..aOS(4, _omitFieldNames ? '' : 'itemName')
    ..a<$core.int>(5, _omitFieldNames ? '' : 'itemCount', $pb.PbFieldType.O3)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  TradeMetadata clone() => TradeMetadata()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  TradeMetadata copyWith(void Function(TradeMetadata) updates) => super.copyWith((message) => updates(message as TradeMetadata)) as TradeMetadata;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TradeMetadata create() => TradeMetadata._();
  TradeMetadata createEmptyInstance() => create();
  static $pb.PbList<TradeMetadata> createRepeated() => $pb.PbList<TradeMetadata>();
  @$core.pragma('dart2js:noInline')
  static TradeMetadata getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<TradeMetadata>(create);
  static TradeMetadata? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get tradeId => $_getSZ(0);
  @$pb.TagNumber(1)
  set tradeId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasTradeId() => $_has(0);
  @$pb.TagNumber(1)
  void clearTradeId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get status => $_getSZ(1);
  @$pb.TagNumber(2)
  set status($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasStatus() => $_has(1);
  @$pb.TagNumber(2)
  void clearStatus() => clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get amount => $_getI64(2);
  @$pb.TagNumber(3)
  set amount($fixnum.Int64 v) { $_setInt64(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasAmount() => $_has(2);
  @$pb.TagNumber(3)
  void clearAmount() => clearField(3);

  @$pb.TagNumber(4)
  $core.String get itemName => $_getSZ(3);
  @$pb.TagNumber(4)
  set itemName($core.String v) { $_setString(3, v); }
  @$pb.TagNumber(4)
  $core.bool hasItemName() => $_has(3);
  @$pb.TagNumber(4)
  void clearItemName() => clearField(4);

  @$pb.TagNumber(5)
  $core.int get itemCount => $_getIZ(4);
  @$pb.TagNumber(5)
  set itemCount($core.int v) { $_setSignedInt32(4, v); }
  @$pb.TagNumber(5)
  $core.bool hasItemCount() => $_has(4);
  @$pb.TagNumber(5)
  void clearItemCount() => clearField(5);
}

/// NPC 对话（msg_type = NPC_DIALOG 时）
class NpcDialogMetadata extends $pb.GeneratedMessage {
  factory NpcDialogMetadata({
    $core.String? npcId,
    $core.String? npcName,
    $core.String? dialogId,
    $core.Iterable<$core.String>? options,
  }) {
    final $result = create();
    if (npcId != null) {
      $result.npcId = npcId;
    }
    if (npcName != null) {
      $result.npcName = npcName;
    }
    if (dialogId != null) {
      $result.dialogId = dialogId;
    }
    if (options != null) {
      $result.options.addAll(options);
    }
    return $result;
  }
  NpcDialogMetadata._() : super();
  factory NpcDialogMetadata.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory NpcDialogMetadata.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'NpcDialogMetadata', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'npcId')
    ..aOS(2, _omitFieldNames ? '' : 'npcName')
    ..aOS(3, _omitFieldNames ? '' : 'dialogId')
    ..pPS(4, _omitFieldNames ? '' : 'options')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  NpcDialogMetadata clone() => NpcDialogMetadata()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  NpcDialogMetadata copyWith(void Function(NpcDialogMetadata) updates) => super.copyWith((message) => updates(message as NpcDialogMetadata)) as NpcDialogMetadata;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static NpcDialogMetadata create() => NpcDialogMetadata._();
  NpcDialogMetadata createEmptyInstance() => create();
  static $pb.PbList<NpcDialogMetadata> createRepeated() => $pb.PbList<NpcDialogMetadata>();
  @$core.pragma('dart2js:noInline')
  static NpcDialogMetadata getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<NpcDialogMetadata>(create);
  static NpcDialogMetadata? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get npcId => $_getSZ(0);
  @$pb.TagNumber(1)
  set npcId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasNpcId() => $_has(0);
  @$pb.TagNumber(1)
  void clearNpcId() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get npcName => $_getSZ(1);
  @$pb.TagNumber(2)
  set npcName($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasNpcName() => $_has(1);
  @$pb.TagNumber(2)
  void clearNpcName() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get dialogId => $_getSZ(2);
  @$pb.TagNumber(3)
  set dialogId($core.String v) { $_setString(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasDialogId() => $_has(2);
  @$pb.TagNumber(3)
  void clearDialogId() => clearField(3);

  @$pb.TagNumber(4)
  $core.List<$core.String> get options => $_getList(3);
}

///  ---- 频道屏蔽（玩家级推送过滤） ----
///
///  玩家可关闭特定频道的推送（如关闭世界频道）。屏蔽只挡推送：历史仍可
///  拉取（屏蔽不是抹除），屏蔽生效后的频道消息也不再进入离线队列。
///  可屏蔽范围：WORLD / GUILD / TEAM——MARQUEE 与 SYSTEM_CHANNEL 是服务
///  广播不可关；「不想收到某人的私聊」归黑名单特性，不在此处。
class SetChannelMuteRequest extends $pb.GeneratedMessage {
  factory SetChannelMuteRequest({
    ChannelType? channelType,
    $core.bool? muted,
  }) {
    final $result = create();
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (muted != null) {
      $result.muted = muted;
    }
    return $result;
  }
  SetChannelMuteRequest._() : super();
  factory SetChannelMuteRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetChannelMuteRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetChannelMuteRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<ChannelType>(1, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOB(2, _omitFieldNames ? '' : 'muted')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetChannelMuteRequest clone() => SetChannelMuteRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetChannelMuteRequest copyWith(void Function(SetChannelMuteRequest) updates) => super.copyWith((message) => updates(message as SetChannelMuteRequest)) as SetChannelMuteRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetChannelMuteRequest create() => SetChannelMuteRequest._();
  SetChannelMuteRequest createEmptyInstance() => create();
  static $pb.PbList<SetChannelMuteRequest> createRepeated() => $pb.PbList<SetChannelMuteRequest>();
  @$core.pragma('dart2js:noInline')
  static SetChannelMuteRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetChannelMuteRequest>(create);
  static SetChannelMuteRequest? _defaultInstance;

  @$pb.TagNumber(1)
  ChannelType get channelType => $_getN(0);
  @$pb.TagNumber(1)
  set channelType(ChannelType v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelType() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelType() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get muted => $_getBF(1);
  @$pb.TagNumber(2)
  set muted($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasMuted() => $_has(1);
  @$pb.TagNumber(2)
  void clearMuted() => clearField(2);
}

class SetChannelMuteResponse extends $pb.GeneratedMessage {
  factory SetChannelMuteResponse({
    $0.ErrorCode? code,
    ChannelType? channelType,
    $core.bool? muted,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (muted != null) {
      $result.muted = muted;
    }
    return $result;
  }
  SetChannelMuteResponse._() : super();
  factory SetChannelMuteResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory SetChannelMuteResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'SetChannelMuteResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..e<ChannelType>(2, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOB(3, _omitFieldNames ? '' : 'muted')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  SetChannelMuteResponse clone() => SetChannelMuteResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  SetChannelMuteResponse copyWith(void Function(SetChannelMuteResponse) updates) => super.copyWith((message) => updates(message as SetChannelMuteResponse)) as SetChannelMuteResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SetChannelMuteResponse create() => SetChannelMuteResponse._();
  SetChannelMuteResponse createEmptyInstance() => create();
  static $pb.PbList<SetChannelMuteResponse> createRepeated() => $pb.PbList<SetChannelMuteResponse>();
  @$core.pragma('dart2js:noInline')
  static SetChannelMuteResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SetChannelMuteResponse>(create);
  static SetChannelMuteResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  ChannelType get channelType => $_getN(1);
  @$pb.TagNumber(2)
  set channelType(ChannelType v) { setField(2, v); }
  @$pb.TagNumber(2)
  $core.bool hasChannelType() => $_has(1);
  @$pb.TagNumber(2)
  void clearChannelType() => clearField(2);

  @$pb.TagNumber(3)
  $core.bool get muted => $_getBF(2);
  @$pb.TagNumber(3)
  set muted($core.bool v) { $_setBool(2, v); }
  @$pb.TagNumber(3)
  $core.bool hasMuted() => $_has(2);
  @$pb.TagNumber(3)
  void clearMuted() => clearField(3);
}

class ChannelMuteState extends $pb.GeneratedMessage {
  factory ChannelMuteState({
    ChannelType? channelType,
    $core.bool? muted,
  }) {
    final $result = create();
    if (channelType != null) {
      $result.channelType = channelType;
    }
    if (muted != null) {
      $result.muted = muted;
    }
    return $result;
  }
  ChannelMuteState._() : super();
  factory ChannelMuteState.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory ChannelMuteState.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'ChannelMuteState', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<ChannelType>(1, _omitFieldNames ? '' : 'channelType', $pb.PbFieldType.OE, defaultOrMaker: ChannelType.PRIVATE, valueOf: ChannelType.valueOf, enumValues: ChannelType.values)
    ..aOB(2, _omitFieldNames ? '' : 'muted')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  ChannelMuteState clone() => ChannelMuteState()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  ChannelMuteState copyWith(void Function(ChannelMuteState) updates) => super.copyWith((message) => updates(message as ChannelMuteState)) as ChannelMuteState;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ChannelMuteState create() => ChannelMuteState._();
  ChannelMuteState createEmptyInstance() => create();
  static $pb.PbList<ChannelMuteState> createRepeated() => $pb.PbList<ChannelMuteState>();
  @$core.pragma('dart2js:noInline')
  static ChannelMuteState getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<ChannelMuteState>(create);
  static ChannelMuteState? _defaultInstance;

  @$pb.TagNumber(1)
  ChannelType get channelType => $_getN(0);
  @$pb.TagNumber(1)
  set channelType(ChannelType v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasChannelType() => $_has(0);
  @$pb.TagNumber(1)
  void clearChannelType() => clearField(1);

  @$pb.TagNumber(2)
  $core.bool get muted => $_getBF(1);
  @$pb.TagNumber(2)
  set muted($core.bool v) { $_setBool(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasMuted() => $_has(1);
  @$pb.TagNumber(2)
  void clearMuted() => clearField(2);
}

class GetChannelMutesRequest extends $pb.GeneratedMessage {
  factory GetChannelMutesRequest() => create();
  GetChannelMutesRequest._() : super();
  factory GetChannelMutesRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetChannelMutesRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetChannelMutesRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetChannelMutesRequest clone() => GetChannelMutesRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetChannelMutesRequest copyWith(void Function(GetChannelMutesRequest) updates) => super.copyWith((message) => updates(message as GetChannelMutesRequest)) as GetChannelMutesRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetChannelMutesRequest create() => GetChannelMutesRequest._();
  GetChannelMutesRequest createEmptyInstance() => create();
  static $pb.PbList<GetChannelMutesRequest> createRepeated() => $pb.PbList<GetChannelMutesRequest>();
  @$core.pragma('dart2js:noInline')
  static GetChannelMutesRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetChannelMutesRequest>(create);
  static GetChannelMutesRequest? _defaultInstance;
}

class GetChannelMutesResponse extends $pb.GeneratedMessage {
  factory GetChannelMutesResponse({
    $0.ErrorCode? code,
    $core.Iterable<ChannelMuteState>? states,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (states != null) {
      $result.states.addAll(states);
    }
    return $result;
  }
  GetChannelMutesResponse._() : super();
  factory GetChannelMutesResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetChannelMutesResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetChannelMutesResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pc<ChannelMuteState>(2, _omitFieldNames ? '' : 'states', $pb.PbFieldType.PM, subBuilder: ChannelMuteState.create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetChannelMutesResponse clone() => GetChannelMutesResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetChannelMutesResponse copyWith(void Function(GetChannelMutesResponse) updates) => super.copyWith((message) => updates(message as GetChannelMutesResponse)) as GetChannelMutesResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetChannelMutesResponse create() => GetChannelMutesResponse._();
  GetChannelMutesResponse createEmptyInstance() => create();
  static $pb.PbList<GetChannelMutesResponse> createRepeated() => $pb.PbList<GetChannelMutesResponse>();
  @$core.pragma('dart2js:noInline')
  static GetChannelMutesResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetChannelMutesResponse>(create);
  static GetChannelMutesResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  /// 固定含全部三个可屏蔽频道（含未屏蔽的），顺序稳定（WORLD/GUILD/TEAM）。
  @$pb.TagNumber(2)
  $core.List<ChannelMuteState> get states => $_getList(1);
}

/// 黑名单（game_chat_features P0）：拉黑某人后，对方发给自己的世界/公会/
/// 队伍频道消息与私聊都不再投递——私聊对发送方静默成功（code=0，不暴露
/// 拉黑态），频道消息按成员逐个过滤；拉黑前已入离线队列的消息照常补投。
/// 与社交面的 BLOCK_USER（好友关系）互相独立，只影响消息投递。
class BlockMessageSenderRequest extends $pb.GeneratedMessage {
  factory BlockMessageSenderRequest({
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  BlockMessageSenderRequest._() : super();
  factory BlockMessageSenderRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BlockMessageSenderRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BlockMessageSenderRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BlockMessageSenderRequest clone() => BlockMessageSenderRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BlockMessageSenderRequest copyWith(void Function(BlockMessageSenderRequest) updates) => super.copyWith((message) => updates(message as BlockMessageSenderRequest)) as BlockMessageSenderRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BlockMessageSenderRequest create() => BlockMessageSenderRequest._();
  BlockMessageSenderRequest createEmptyInstance() => create();
  static $pb.PbList<BlockMessageSenderRequest> createRepeated() => $pb.PbList<BlockMessageSenderRequest>();
  @$core.pragma('dart2js:noInline')
  static BlockMessageSenderRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BlockMessageSenderRequest>(create);
  static BlockMessageSenderRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get targetUserId => $_getSZ(0);
  @$pb.TagNumber(1)
  set targetUserId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasTargetUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearTargetUserId() => clearField(1);
}

class BlockMessageSenderResponse extends $pb.GeneratedMessage {
  factory BlockMessageSenderResponse({
    $0.ErrorCode? code,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  BlockMessageSenderResponse._() : super();
  factory BlockMessageSenderResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory BlockMessageSenderResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'BlockMessageSenderResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  BlockMessageSenderResponse clone() => BlockMessageSenderResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  BlockMessageSenderResponse copyWith(void Function(BlockMessageSenderResponse) updates) => super.copyWith((message) => updates(message as BlockMessageSenderResponse)) as BlockMessageSenderResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static BlockMessageSenderResponse create() => BlockMessageSenderResponse._();
  BlockMessageSenderResponse createEmptyInstance() => create();
  static $pb.PbList<BlockMessageSenderResponse> createRepeated() => $pb.PbList<BlockMessageSenderResponse>();
  @$core.pragma('dart2js:noInline')
  static BlockMessageSenderResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<BlockMessageSenderResponse>(create);
  static BlockMessageSenderResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get targetUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set targetUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTargetUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTargetUserId() => clearField(2);
}

class UnblockMessageSenderRequest extends $pb.GeneratedMessage {
  factory UnblockMessageSenderRequest({
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  UnblockMessageSenderRequest._() : super();
  factory UnblockMessageSenderRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnblockMessageSenderRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnblockMessageSenderRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnblockMessageSenderRequest clone() => UnblockMessageSenderRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnblockMessageSenderRequest copyWith(void Function(UnblockMessageSenderRequest) updates) => super.copyWith((message) => updates(message as UnblockMessageSenderRequest)) as UnblockMessageSenderRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnblockMessageSenderRequest create() => UnblockMessageSenderRequest._();
  UnblockMessageSenderRequest createEmptyInstance() => create();
  static $pb.PbList<UnblockMessageSenderRequest> createRepeated() => $pb.PbList<UnblockMessageSenderRequest>();
  @$core.pragma('dart2js:noInline')
  static UnblockMessageSenderRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnblockMessageSenderRequest>(create);
  static UnblockMessageSenderRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get targetUserId => $_getSZ(0);
  @$pb.TagNumber(1)
  set targetUserId($core.String v) { $_setString(0, v); }
  @$pb.TagNumber(1)
  $core.bool hasTargetUserId() => $_has(0);
  @$pb.TagNumber(1)
  void clearTargetUserId() => clearField(1);
}

class UnblockMessageSenderResponse extends $pb.GeneratedMessage {
  factory UnblockMessageSenderResponse({
    $0.ErrorCode? code,
    $core.String? targetUserId,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (targetUserId != null) {
      $result.targetUserId = targetUserId;
    }
    return $result;
  }
  UnblockMessageSenderResponse._() : super();
  factory UnblockMessageSenderResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory UnblockMessageSenderResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'UnblockMessageSenderResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'targetUserId')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  UnblockMessageSenderResponse clone() => UnblockMessageSenderResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  UnblockMessageSenderResponse copyWith(void Function(UnblockMessageSenderResponse) updates) => super.copyWith((message) => updates(message as UnblockMessageSenderResponse)) as UnblockMessageSenderResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UnblockMessageSenderResponse create() => UnblockMessageSenderResponse._();
  UnblockMessageSenderResponse createEmptyInstance() => create();
  static $pb.PbList<UnblockMessageSenderResponse> createRepeated() => $pb.PbList<UnblockMessageSenderResponse>();
  @$core.pragma('dart2js:noInline')
  static UnblockMessageSenderResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<UnblockMessageSenderResponse>(create);
  static UnblockMessageSenderResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.String get targetUserId => $_getSZ(1);
  @$pb.TagNumber(2)
  set targetUserId($core.String v) { $_setString(1, v); }
  @$pb.TagNumber(2)
  $core.bool hasTargetUserId() => $_has(1);
  @$pb.TagNumber(2)
  void clearTargetUserId() => clearField(2);
}

class GetBlockedSendersRequest extends $pb.GeneratedMessage {
  factory GetBlockedSendersRequest() => create();
  GetBlockedSendersRequest._() : super();
  factory GetBlockedSendersRequest.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetBlockedSendersRequest.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetBlockedSendersRequest', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetBlockedSendersRequest clone() => GetBlockedSendersRequest()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetBlockedSendersRequest copyWith(void Function(GetBlockedSendersRequest) updates) => super.copyWith((message) => updates(message as GetBlockedSendersRequest)) as GetBlockedSendersRequest;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetBlockedSendersRequest create() => GetBlockedSendersRequest._();
  GetBlockedSendersRequest createEmptyInstance() => create();
  static $pb.PbList<GetBlockedSendersRequest> createRepeated() => $pb.PbList<GetBlockedSendersRequest>();
  @$core.pragma('dart2js:noInline')
  static GetBlockedSendersRequest getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetBlockedSendersRequest>(create);
  static GetBlockedSendersRequest? _defaultInstance;
}

class GetBlockedSendersResponse extends $pb.GeneratedMessage {
  factory GetBlockedSendersResponse({
    $0.ErrorCode? code,
    $core.Iterable<$core.String>? targetUserIds,
  }) {
    final $result = create();
    if (code != null) {
      $result.code = code;
    }
    if (targetUserIds != null) {
      $result.targetUserIds.addAll(targetUserIds);
    }
    return $result;
  }
  GetBlockedSendersResponse._() : super();
  factory GetBlockedSendersResponse.fromBuffer($core.List<$core.int> i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromBuffer(i, r);
  factory GetBlockedSendersResponse.fromJson($core.String i, [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) => create()..mergeFromJson(i, r);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : 'GetBlockedSendersResponse', package: const $pb.PackageName(_omitMessageNames ? '' : 'chirp.chat'), createEmptyInstance: create)
    ..e<$0.ErrorCode>(1, _omitFieldNames ? '' : 'code', $pb.PbFieldType.OE, defaultOrMaker: $0.ErrorCode.OK, valueOf: $0.ErrorCode.valueOf, enumValues: $0.ErrorCode.values)
    ..pPS(2, _omitFieldNames ? '' : 'targetUserIds')
    ..hasRequiredFields = false
  ;

  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
  'Will be removed in next major version')
  GetBlockedSendersResponse clone() => GetBlockedSendersResponse()..mergeFromMessage(this);
  @$core.Deprecated(
  'Using this can add significant overhead to your binary. '
  'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
  'Will be removed in next major version')
  GetBlockedSendersResponse copyWith(void Function(GetBlockedSendersResponse) updates) => super.copyWith((message) => updates(message as GetBlockedSendersResponse)) as GetBlockedSendersResponse;

  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetBlockedSendersResponse create() => GetBlockedSendersResponse._();
  GetBlockedSendersResponse createEmptyInstance() => create();
  static $pb.PbList<GetBlockedSendersResponse> createRepeated() => $pb.PbList<GetBlockedSendersResponse>();
  @$core.pragma('dart2js:noInline')
  static GetBlockedSendersResponse getDefault() => _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<GetBlockedSendersResponse>(create);
  static GetBlockedSendersResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $0.ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code($0.ErrorCode v) { setField(1, v); }
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => clearField(1);

  @$pb.TagNumber(2)
  $core.List<$core.String> get targetUserIds => $_getList(1);
}


const _omitFieldNames = $core.bool.fromEnvironment('protobuf.omit_field_names');
const _omitMessageNames = $core.bool.fromEnvironment('protobuf.omit_message_names');
