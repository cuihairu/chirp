import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:ffi/ffi.dart';

import 'chirp_ffi.dart';

/// High-level Chirp SDK client wrapper
/// Provides a Dart-friendly API for the Chirp real-time communication platform
class ChirpClient {
  // Singleton pattern
  ChirpClient._internal();

  static ChirpClient? _instance;
  bool _isInitialized = false;
  bool _isConnected = false;

  // Event controllers
  final _messageController = StreamController<ChirpMessage>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();
  final _responseController = StreamController<_ChirpResponse>.broadcast();

  // Response callbacks
  final Map<int, Function(bool, String)> _responseCallbacks = {};
  int _nextCallbackId = 0;

  /// Get the singleton instance
  static ChirpClient get instance {
    _instance ??= ChirpClient._internal();
    return _instance!;
  }

  /// Public streams
  Stream<ChirpMessage> get messageStream => _messageController.stream;
  Stream<bool> get connectionStream => _connectionController.stream;
  Stream<_ChirpResponse> get responseStream => _responseController.stream;

  /// Initialize the SDK
  Future<bool> initialize({String gatewayHost = '127.0.0.1', int gatewayPort = 7000}) async {
    if (_isInitialized) return true;

    final config = {
      'gateway_host': gatewayHost,
      'gateway_port': gatewayPort,
      'app_id': 'chirp_mobile'
    };

    final result = ChirpFFI.initialize(jsonEncode(config));
    _isInitialized = (result == ChirpFFI.CHIRP_OK);

    if (_isInitialized) {
      _setupNativeCallbacks();
    }

    return _isInitialized;
  }

  /// Shutdown the SDK
  void shutdown() {
    if (!_isInitialized) return;

    _messageController.close();
    _connectionController.close();
    _responseController.close();

    ChirpFFI.shutdown();
    _isInitialized = false;
    _isConnected = false;
  }

  /// Connect to the server
  Future<bool> connect() async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.connect();
    _isConnected = (result == ChirpFFI.CHIRP_OK);
    return _isConnected;
  }

  /// Disconnect from the server
  void disconnect() {
    if (!_isInitialized) return;

    ChirpFFI.disconnect();
    _isConnected = false;
  }

  /// Check connection status
  bool get isConnected {
    if (!_isInitialized) return false;
    return ChirpFFI.isConnected();
  }

  /// Login with user credentials
  Future<bool> login(String userId, String token, {String deviceId = ''}) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.login(
      userId,
      token,
      deviceId,
      'mobile', // platform
    );

    return result == ChirpFFI.CHIRP_OK;
  }

  /// Logout
  void logout() {
    if (!_isInitialized) return;
    ChirpFFI.logout();
  }

  /// Get current user ID
  String get userId {
    if (!_isInitialized) return '';
    return ChirpFFI.getUserId() ?? '';
  }

  /// Get current session ID
  String get sessionId {
    if (!_isInitialized) return '';
    return ChirpFFI.getSessionId() ?? '';
  }

  /// Send a text message
  Future<bool> sendMessage(String toUserId, String content) async {
    if (!_isInitialized || !isConnected) return false;

    final callbackId = _nextCallbackId++;
    final result = ChirpFFI.sendMessage(toUserId, '', 0, content, callbackId);

    return result == ChirpFFI.CHIRP_OK;
  }

  /// Get chat history
  Future<List<ChirpMessage>> getHistory(String channelId, ChannelType channelType, {int limit = 50}) async {
    if (!_isInitialized || !isConnected) return [];

    final callbackId = _nextCallbackId++;

    // This will be populated by the response callback
    final completer = Completer<List<ChirpMessage>>();
    _responseCallbacks[callbackId] = (success, data) {
      if (success) {
        try {
          final messages = _parseHistoryMessages(data);
          completer.complete(messages);
        } catch (e) {
          completer.completeError(e);
        }
      } else {
        completer.complete([]);
      }
    };

    ChirpFFI.getHistory(channelId, channelType.index, 0, limit, callbackId);

    return completer.future;
  }

  /// Mark messages as read
  Future<bool> markRead(String channelId, ChannelType channelType, String messageId) async {
    if (!_isInitialized || !isConnected) return false;

    final result = ChirpFFI.markRead(channelId, channelType.index, messageId);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Get unread count
  int getUnreadCount() {
    if (!_isInitialized) return 0;
    return ChirpFFI.getUnreadCount() ?? 0;
  }

  /// Join a voice room
  Future<bool> joinVoiceRoom(String roomId) async {
    if (!_isInitialized || !isConnected) return false;

    final callbackId = _nextCallbackId++;
    final result = ChirpFFI.joinVoiceRoom(roomId, callbackId);

    return result == ChirpFFI.CHIRP_OK;
  }

  /// Leave voice room
  Future<bool> leaveVoiceRoom() async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.leaveVoiceRoom();
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Set microphone muted state
  Future<bool> setMicMuted(bool muted) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.setMicMuted(muted ? 1 : 0);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Set speaker muted state
  Future<bool> setSpeakerMuted(bool muted) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.setSpeakerMuted(muted ? 1 : 0);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Check if mic is muted
  bool get isMicMuted {
    if (!_isInitialized) return true;
    return ChirpFFI.isMicMuted();
  }

  /// Check if speaker is muted
  bool get isSpeakerMuted {
    if (!_isInitialized) return true;
    return ChirpFFI.isSpeakerMuted();
  }

  // Voice room methods with enhanced functionality

  /// Get voice room info
  Future<VoiceRoomInfo?> getVoiceRoomInfo(String roomId) async {
    if (!_isInitialized || !isConnected) return null;

    final callbackId = _nextCallbackId++;
    final completer = Completer<VoiceRoomInfo?>();

    _responseCallbacks[callbackId] = (success, data) {
      if (success && data.isNotEmpty) {
        try {
          final json = jsonDecode(data);
          final info = VoiceRoomInfo(
            roomId: json['room_id'] ?? roomId,
            roomName: json['room_name'] ?? '',
            roomType: VoiceRoomType.values[json['room_type'] ?? 0],
            participants: (json['participants'] as List? ?? [])
                .map((p) => VoiceParticipant(
                      userId: p['user_id'] ?? '',
                      username: p['username'] ?? '',
                      isMuted: p['state'] == 2,
                      isSpeaking: p['is_speaking'] ?? false,
                    ))
                .toList(),
          );
          completer.complete(info);
        } catch (e) {
          completer.complete(null);
        }
      } else {
        completer.complete(null);
      }
    };

    ChirpFFI.getVoiceRoomInfo(roomId, callbackId);

    return completer.future.timeout(
      const Duration(seconds: 5),
      onTimeout: () => null,
    );
  }

  /// Join voice room with room type
  Future<bool> joinVoiceRoom(String roomId, VoiceRoomType roomType) async {
    if (!_isInitialized || !isConnected) return false;

    final callbackId = _nextCallbackId++;
    final result = ChirpFFI.joinVoiceRoomWithType(roomId, roomType.index, callbackId);

    return result == ChirpFFI.CHIRP_OK;
  }

  /// Leave voice room
  Future<bool> leaveVoiceRoom(String roomId) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.leaveVoiceRoomById(roomId);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Set voice mute state (notifies server)
  Future<bool> setVoiceMute(String roomId, bool muted) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.setVoiceMute(roomId, muted ? 1 : 0);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Send ICE candidate through signaling
  Future<bool> sendIceCandidate(
    String roomId,
    String candidate,
    String sdpMid,
    int sdpMLineIndex, {
    String? toUserId,
  }) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.sendIceCandidate(
      roomId,
      toUserId ?? '',
      candidate,
      sdpMid,
      sdpMLineIndex,
    );
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Send SDP answer through signaling
  Future<bool> sendSdpAnswer(
    String roomId,
    String sdpAnswer,
    String toUserId,
  ) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI.sendSdpAnswer(roomId, toUserId, sdpAnswer);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Create voice room
  Future<String?> createVoiceRoom(VoiceRoomType roomType, String roomName, int maxParticipants) async {
    if (!_isInitialized || !isConnected) return null;

    final callbackId = _nextCallbackId++;
    final completer = Completer<String?>();

    _responseCallbacks[callbackId] = (success, data) {
      if (success && data.isNotEmpty) {
        try {
          final json = jsonDecode(data);
          completer.complete(json['room_id'] as String?);
        } catch (e) {
          completer.complete(null);
        }
      } else {
        completer.complete(null);
      }
    };

    ChirpFFI.createVoiceRoom(roomType.index, roomName, maxParticipants, callbackId);

    return completer.future.timeout(
      const Duration(seconds: 5),
      onTimeout: () => null,
    );
  }

  // Voice event streams

  final _voiceParticipantJoinedController = StreamController<VoiceParticipant>.broadcast();
  final _voiceParticipantLeftController = StreamController<String>.broadcast();
  final _voiceSpeakingController = StreamController<VoiceSpeakingEvent>.broadcast();
  final _voiceIceCandidateController = StreamController<VoiceIceCandidateEvent>.broadcast();
  final _voiceSdpOfferController = StreamController<VoiceSdpOfferEvent>.broadcast();

  Stream<VoiceParticipant> get voiceParticipantJoinedStream => _voiceParticipantJoinedController.stream;
  Stream<String> get voiceParticipantLeftStream => _voiceParticipantLeftController.stream;
  Stream<VoiceSpeakingEvent> get voiceSpeakingStream => _voiceSpeakingController.stream;
  Stream<VoiceIceCandidateEvent> get voiceIceCandidateStream => _voiceIceCandidateController.stream;
  Stream<VoiceSdpOfferEvent> get voiceSdpOfferStream => _voiceSdpOfferController.stream;

  // Private methods

  void _setupNativeCallbacks() {
    // Message callback
    ChirpFFI.setMessageCallback((messageJsonPtr) {
      final messageJson = messageJsonPtr.toDartString();
      _handleMessageCallback(messageJson);
    });

    // Response callback
    ChirpFFI.setResponseCallback((callbackId, success, dataJsonPtr) {
      _handleResponseCallback(callbackId, success != 0, dataJsonPtr.toDartString());
    });

    // Connection callback
    ChirpFFI.setConnectionCallback((connected, errorCode) {
      _handleConnectionCallback(connected != 0, errorCode);
    });
  }

  void _handleMessageCallback(String messageJson) {
    try {
      final message = ChirpMessage.fromJson(jsonDecode(messageJson));
      _messageController.add(message);
    } catch (e) {
      // Ignore parse errors
    }
  }

  void _handleResponseCallback(int callbackId, bool success, String data) {
    final callback = _responseCallbacks.remove(callbackId);
    callback?.call(success, data);
  }

  void _handleConnectionCallback(bool connected, int errorCode) {
    _isConnected = connected;
    _connectionController.add(connected);
  }

  List<ChirpMessage> _parseHistoryMessages(String json) {
    try {
      final List<dynamic> jsonList = jsonDecode(json);
      return jsonList.map((msg) => ChirpMessage.fromJson(msg as Map<String, dynamic>)).toList();
    } catch (e) {
      return [];
    }
  }
}

// Supporting classes

class _ChirpResponse {
  final bool success;
  final String data;

  _ChirpResponse(this.success, this.data);
}

/// Chat message model
class ChirpMessage {
  final String messageId;
  final String senderId;
  final String receiverId;
  final String channelId;
  final ChannelType channelType;
  final MsgType msgType;
  final String content;
  final int timestamp;

  ChirpMessage({
    required this.messageId,
    required this.senderId,
    required this.receiverId,
    required this.channelId,
    required this.channelType,
    required this.msgType,
    required this.content,
    required this.timestamp,
  });

  factory ChirpMessage.fromJson(Map<String, dynamic> json) {
    return ChirpMessage(
      messageId: json['message_id'] ?? '',
      senderId: json['sender_id'] ?? '',
      receiverId: json['receiver_id'] ?? '',
      channelId: json['channel_id'] ?? '',
      channelType: ChannelType.values[json['channel_type'] ?? 0],
      msgType: MsgType.values[json['msg_type'] ?? 0],
      content: json['content'] ?? '',
      timestamp: json['timestamp'] ?? 0,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'message_id': messageId,
      'sender_id': senderId,
      'receiver_id': receiverId,
      'channel_id': channelId,
      'channel_type': channelType.index,
      'msg_type': msgType.index,
      'content': content,
      'timestamp': timestamp,
    };
  }
}

/// Channel types
enum ChannelType {
  private,
  team,
  guild,
  world,
}

/// Message types
enum MsgType {
  text,
  emoji,
  voice,
  image,
  system,
}

// Voice room supporting types

/// Voice room types
enum VoiceRoomType {
  peerToPeer,
  group,
  channel,
}

/// Voice participant info
class VoiceParticipant {
  final String userId;
  final String username;
  final bool isSpeaking;
  final bool isMuted;

  VoiceParticipant({
    required this.userId,
    required this.username,
    this.isSpeaking = false,
    this.isMuted = false,
  });
}

/// Voice room info
class VoiceRoomInfo {
  final String roomId;
  final String roomName;
  final VoiceRoomType roomType;
  final List<VoiceParticipant> participants;

  VoiceRoomInfo({
    required this.roomId,
    required this.roomName,
    required this.roomType,
    required this.participants,
  });
}

/// Voice ICE candidate event
class VoiceIceCandidateEvent {
  final String fromUserId;
  final String candidate;
  final String sdpMid;
  final int sdpMLineIndex;

  VoiceIceCandidateEvent({
    required this.fromUserId,
    required this.candidate,
    required this.sdpMid,
    required this.sdpMLineIndex,
  });

  factory VoiceIceCandidateEvent.fromJson(Map<String, dynamic> json) {
    return VoiceIceCandidateEvent(
      fromUserId: json['from_user_id'] ?? '',
      candidate: json['candidate'] ?? '',
      sdpMid: json['sdp_mid'] ?? '',
      sdpMLineIndex: json['sdp_mline_index'] ?? 0,
    );
  }
}

/// Voice SDP offer event
class VoiceSdpOfferEvent {
  final String fromUserId;
  final String sdpOffer;

  VoiceSdpOfferEvent({
    required this.fromUserId,
    required this.sdpOffer,
  });

  factory VoiceSdpOfferEvent.fromJson(Map<String, dynamic> json) {
    return VoiceSdpOfferEvent(
      fromUserId: json['from_user_id'] ?? '',
      sdpOffer: json['sdp_offer'] ?? '',
    );
  }
}

/// Voice speaking state event
class VoiceSpeakingEvent {
  final String userId;
  final bool isSpeaking;

  VoiceSpeakingEvent({
    required this.userId,
    required this.isSpeaking,
  });

  factory VoiceSpeakingEvent.fromJson(Map<String, dynamic> json) {
    return VoiceSpeakingEvent(
      userId: json['user_id'] ?? '',
      isSpeaking: json['speaking'] ?? false,
    );
  }
}
