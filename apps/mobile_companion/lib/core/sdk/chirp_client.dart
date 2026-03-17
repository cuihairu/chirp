import 'dart:async';
import 'dart:convert';
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

    final result = ChirpFFI._initialize(jsonEncode(config));
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

    ChirpFFI._shutdown();
    _isInitialized = false;
    _isConnected = false;
  }

  /// Connect to the server
  Future<bool> connect() async {
    if (!_isInitialized) return false;

    final result = ChirpFFI._connect();
    _isConnected = (result == ChirpFFI.CHIRP_OK);
    return _isConnected;
  }

  /// Disconnect from the server
  void disconnect() {
    if (!_isInitialized) return;

    ChirpFFI._disconnect();
    _isConnected = false;
  }

  /// Check connection status
  bool get isConnected {
    if (!_isInitialized) return false;
    return ChirpFFI._isConnected() != 0;
  }

  /// Login with user credentials
  Future<bool> login(String userId, String token, {String deviceId = ''}) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI._login(
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
    ChirpFFI._logout();
  }

  /// Get current user ID
  String get userId {
    if (!_isInitialized) return '';

    final buffer = calloc.allocate<Uint8>(256);
    try {
      final result = ChirpFFI._getUserId(buffer.cast<Char>(), 256);
      if (result == ChirpFFI.CHIRP_OK) {
        final userIdStr = buffer.cast<Utf8>().toDartString(length: 256);
        return userIdStr.split('\x00')[0];
      }
    } finally {
      calloc.free(buffer);
    }
    return '';
  }

  /// Get current session ID
  String get sessionId {
    if (!_isInitialized) return '';

    final buffer = calloc.allocate<Uint8>(256);
    try {
      final result = ChirpFFI._getSessionId(buffer.cast<Char>(), 256);
      if (result == ChirpFFI.CHIRP_OK) {
        final sessionIdStr = buffer.cast<Utf8>().toDartString(length: 256);
        return sessionIdStr.split('\x00')[0];
      }
    } finally {
      calloc.free(buffer);
    }
    return '';
  }

  /// Send a text message
  Future<bool> sendMessage(String toUserId, String content) async {
    if (!_isInitialized || !isConnected) return false;

    final callbackId = _nextCallbackId++;
    final result = ChirpFFI._sendMessage(toUserId, '', 0, content, callbackId);

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

    ChirpFFI._getHistory(channelId, channelType.index, 0, limit, callbackId);

    return completer.future;
  }

  /// Mark messages as read
  Future<bool> markRead(String channelId, ChannelType channelType, String messageId) async {
    if (!_isInitialized || !isConnected) return false;

    final result = ChirpFFI._markRead(channelId, channelType.index, messageId);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Get unread count
  int getUnreadCount() {
    if (!_isInitialized) return 0;

    final countPtr = calloc.allocate<Int32>();
    try {
      final result = ChirpFFI._getUnreadCount(countPtr);
      if (result == ChirpFFI.CHIRP_OK) {
        return countPtr.value;
      }
    } finally {
      calloc.free(countPtr);
    }
    return 0;
  }

  /// Join a voice room
  Future<bool> joinVoiceRoom(String roomId) async {
    if (!_isInitialized || !isConnected) return false;

    final callbackId = _nextCallbackId++;
    final result = ChirpFFI._joinVoiceRoom(roomId, callbackId);

    return result == ChirpFFI.CHIRP_OK;
  }

  /// Leave voice room
  Future<bool> leaveVoiceRoom() async {
    if (!_isInitialized) return false;

    final result = ChirpFFI._leaveVoiceRoom();
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Set microphone muted state
  Future<bool> setMicMuted(bool muted) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI._setMicMuted(muted ? 1 : 0);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Set speaker muted state
  Future<bool> setSpeakerMuted(bool muted) async {
    if (!_isInitialized) return false;

    final result = ChirpFFI._setSpeakerMuted(muted ? 1 : 0);
    return result == ChirpFFI.CHIRP_OK;
  }

  /// Check if mic is muted
  bool get isMicMuted {
    if (!_isInitialized) return true;
    return ChirpFFI._isMicMuted() != 0;
  }

  /// Check if speaker is muted
  bool get isSpeakerMuted {
    if (!_isInitialized) return true;
    return ChirpFFI._isSpeakerMuted() != 0;
  }

  // Private methods

  void _setupNativeCallbacks() {
    // Message callback
    final messageCallback = Pointer.fromFunction<MessageCallbackNative>((messageJsonPtr) {
      final messageJson = messageJsonPtr.cast<Utf8>().toDartString();
      _handleMessageCallback(messageJson);
      return;
    });
    ChirpFFI._setMessageCallback(messageCallback);

    // Response callback
    final responseCallback = Pointer.fromFunction<ResponseCallbackNative>((callbackId, success, dataJsonPtr) {
      _handleResponseCallback(callbackId, success != 0, dataJsonPtr.cast<Utf8>().toDartString());
      return;
    });
    ChirpFFI._setResponseCallback(responseCallback);

    // Connection callback
    final connectionCallback = Pointer.fromFunction<ConnectionCallbackNative>((connected, errorCode) {
      _handleConnectionCallback(connected != 0, errorCode);
      return;
    });
    ChirpFFI._setConnectionCallback(connectionCallback);
  }

  void _handleMessageCallback(String messageJson) {
    try {
      final message = ChirpMessage.fromJson(messageJson);
      _messageController.add(message);
    } catch (e) {
      print('Error parsing message: $e');
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
      return jsonList.map((msg) => ChirpMessage.fromJson(jsonEncode(msg))).toList();
    } catch (e) {
      print('Error parsing history: $e');
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
