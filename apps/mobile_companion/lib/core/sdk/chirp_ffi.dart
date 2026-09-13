import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// Native callback types (must live at the top level: Dart forbids
// typedefs inside classes)
typedef NativeMessageCallback = Void Function(Pointer<Utf8>);
typedef NativeResponseCallback = Void Function(Int32, Int32, Pointer<Utf8>);
typedef NativeConnectionCallback = Void Function(Int32, Int32);

// Dart callback types
typedef MessageCallback = void Function(Pointer<Utf8>);
typedef ResponseCallback = void Function(int, int, Pointer<Utf8>);
typedef ConnectionCallback = void Function(int, int);

/// Native FFI bindings for the Chirp C++ SDK
/// This class provides the interface to the native chirp library
class ChirpFFI {
  // Native library names by platform
  static const String _libName = 'chirp_unity';

  // Load the native library
  static DynamicLibrary _lib() {
    if (Platform.isAndroid || Platform.isLinux) {
      return DynamicLibrary.open('lib$_libName.so');
    } else if (Platform.isIOS || Platform.isMacOS) {
      return DynamicLibrary.open('$_libName.framework/$_libName');
    } else if (Platform.isWindows) {
      return DynamicLibrary.open('$_libName.dll');
    }
    throw UnsupportedError('Platform not supported');
  }

  // Error codes
  static const int CHIRP_OK = 0;
  static const int CHIRP_ERROR_UNKNOWN = -1;
  static const int CHIRP_ERROR_NOT_INITIALIZED = -2;
  static const int CHIRP_ERROR_ALREADY_INITIALIZED = -3;
  static const int CHIRP_ERROR_NOT_CONNECTED = -4;
  static const int CHIRP_ERROR_ALREADY_CONNECTED = -5;
  static const int CHIRP_ERROR_INVALID_PARAM = -6;
  static const int CHIRP_ERROR_AUTH_FAILED = -7;
  static const int CHIRP_ERROR_NETWORK = -8;
  static const int CHIRP_ERROR_TIMEOUT = -9;
  static const int CHIRP_ERROR_RATE_LIMITED = -10;
  static const int CHIRP_ERROR_SESSION_EXPIRED = -11;

  // Keep references to callbacks to prevent garbage collection
  static final List<Pointer<NativeFunction>> _callbacks = [];

  // Handler storage for the static trampolines below: dart:ffi only
  // allows Pointer.fromFunction with static functions, so closures are
  // parked here and invoked through the trampolines.
  static MessageCallback? _messageHandler;
  static ResponseCallback? _responseHandler;
  static ConnectionCallback? _connectionHandler;

  static void _messageTrampoline(Pointer<Utf8> messageJson) {
    _messageHandler?.call(messageJson);
  }

  static void _responseTrampoline(
      int callbackId, int success, Pointer<Utf8> data) {
    _responseHandler?.call(callbackId, success, data);
  }

  static void _connectionTrampoline(int connected, int errorCode) {
    _connectionHandler?.call(connected, errorCode);
  }

  // Core API functions
  static int Function(Pointer<Utf8>) get _initialize {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>)>>(
            'Chirp_Initialize')
        .asFunction();
  }

  static void Function() get _shutdown {
    return _lib()
        .lookup<NativeFunction<Void Function()>>('Chirp_Shutdown')
        .asFunction();
  }

  static int Function() get _connect {
    return _lib()
        .lookup<NativeFunction<Int32 Function()>>('Chirp_Connect')
        .asFunction();
  }

  static void Function() get _disconnect {
    return _lib()
        .lookup<NativeFunction<Void Function()>>('Chirp_Disconnect')
        .asFunction();
  }

  static int Function() get _isConnected {
    return _lib()
        .lookup<NativeFunction<Int32 Function()>>('Chirp_IsConnected')
        .asFunction();
  }

  static int Function(
      Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>) get _login {
    return _lib()
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>,
                    Pointer<Utf8>)>>('Chirp_Login')
        .asFunction();
  }

  static void Function() get _logout {
    return _lib()
        .lookup<NativeFunction<Void Function()>>('Chirp_Logout')
        .asFunction();
  }

  static int Function(Pointer<Utf8>, int) get _getUserId {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>, Uint32)>>(
            'Chirp_GetUserId')
        .asFunction();
  }

  static int Function(Pointer<Utf8>, int) get _getSessionId {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>, Uint32)>>(
            'Chirp_GetSessionId')
        .asFunction();
  }

  // Chat API functions
  static int Function(Pointer<Utf8>, Pointer<Utf8>, int, Pointer<Utf8>, int)
      get _sendMessage {
    return _lib()
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32,
                    Pointer<Utf8>, Int32)>>('Chirp_SendMessage')
        .asFunction();
  }

  static int Function(Pointer<Utf8>, int, int, int, int) get _getHistory {
    return _lib()
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Utf8>, Int32, Int64, Int32,
                    Int32)>>('Chirp_GetHistory')
        .asFunction();
  }

  static int Function(Pointer<Utf8>, int, Pointer<Utf8>) get _markRead {
    return _lib()
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Utf8>, Int32, Pointer<Utf8>)>>('Chirp_MarkRead')
        .asFunction();
  }

  static int Function(Pointer<Int32>) get _getUnreadCount {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Int32>)>>(
            'Chirp_GetUnreadCount')
        .asFunction();
  }

  // Voice API functions
  static int Function(Pointer<Utf8>, int) get _joinVoiceRoom {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>, Int32)>>(
            'Chirp_JoinVoiceRoom')
        .asFunction();
  }

  static int Function() get _leaveVoiceRoom {
    return _lib()
        .lookup<NativeFunction<Int32 Function()>>('Chirp_LeaveVoiceRoom')
        .asFunction();
  }

  static int Function(int) get _setMicMuted {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Int32)>>('Chirp_SetMicMuted')
        .asFunction();
  }

  static int Function(int) get _setSpeakerMuted {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Int32)>>('Chirp_SetSpeakerMuted')
        .asFunction();
  }

  static int Function() get _isMicMuted {
    return _lib()
        .lookup<NativeFunction<Int32 Function()>>('Chirp_IsMicMuted')
        .asFunction();
  }

  static int Function() get _isSpeakerMuted {
    return _lib()
        .lookup<NativeFunction<Int32 Function()>>('Chirp_IsSpeakerMuted')
        .asFunction();
  }

  // Enhanced voice API functions
  static int Function(Pointer<Utf8>, int, int) get _joinVoiceRoomWithType {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>, Int32, Int32)>>(
            'Chirp_JoinVoiceRoomWithType')
        .asFunction();
  }

  static int Function(Pointer<Utf8>) get _leaveVoiceRoomById {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>)>>(
            'Chirp_LeaveVoiceRoomById')
        .asFunction();
  }

  static int Function(Pointer<Utf8>, int) get _setVoiceMute {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>, Int32)>>(
            'Chirp_SetVoiceMute')
        .asFunction();
  }

  static int Function(
          Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int)
      get _sendIceCandidate {
    return _lib()
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>,
                    Pointer<Utf8>, Int32)>>('Chirp_SendIceCandidate')
        .asFunction();
  }

  static int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>)
      get _sendSdpAnswer {
    return _lib()
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Utf8>, Pointer<Utf8>,
                    Pointer<Utf8>)>>('Chirp_SendSdpAnswer')
        .asFunction();
  }

  static int Function(int, Pointer<Utf8>, int, int) get _createVoiceRoom {
    return _lib()
        .lookup<
            NativeFunction<
                Int32 Function(Int32, Pointer<Utf8>, Int32,
                    Int32)>>('Chirp_CreateVoiceRoom')
        .asFunction();
  }

  static int Function(Pointer<Utf8>, int) get _getVoiceRoomInfo {
    return _lib()
        .lookup<NativeFunction<Int32 Function(Pointer<Utf8>, Int32)>>(
            'Chirp_GetVoiceRoomInfo')
        .asFunction();
  }

  // Callback setters
  static void Function(Pointer<NativeFunction<NativeMessageCallback>>)
      get _setMessageCallback {
    return _lib()
        .lookup<
                NativeFunction<
                    Void Function(
                        Pointer<NativeFunction<NativeMessageCallback>>)>>(
            'Chirp_SetMessageCallback')
        .asFunction();
  }

  static void Function(Pointer<NativeFunction<NativeResponseCallback>>)
      get _setResponseCallback {
    return _lib()
        .lookup<
                NativeFunction<
                    Void Function(
                        Pointer<NativeFunction<NativeResponseCallback>>)>>(
            'Chirp_SetResponseCallback')
        .asFunction();
  }

  static void Function(Pointer<NativeFunction<NativeConnectionCallback>>)
      get _setConnectionCallback {
    return _lib()
        .lookup<
                NativeFunction<
                    Void Function(
                        Pointer<NativeFunction<NativeConnectionCallback>>)>>(
            'Chirp_SetConnectionCallback')
        .asFunction();
  }

  // Public API methods that call the native functions

  static int initialize(String configJson) {
    final configPtr = configJson.toNativeUtf8();
    try {
      return _initialize(configPtr);
    } finally {
      calloc.free(configPtr);
    }
  }

  static void shutdown() {
    _shutdown();
  }

  static int connect() {
    return _connect();
  }

  static void disconnect() {
    _disconnect();
  }

  static bool isConnected() {
    return _isConnected() != 0;
  }

  static int login(
      String userId, String token, String deviceId, String platform) {
    final userIdPtr = userId.toNativeUtf8();
    final tokenPtr = token.toNativeUtf8();
    final deviceIdPtr = deviceId.toNativeUtf8();
    final platformPtr = platform.toNativeUtf8();
    try {
      return _login(userIdPtr, tokenPtr, deviceIdPtr, platformPtr);
    } finally {
      calloc.free(userIdPtr);
      calloc.free(tokenPtr);
      calloc.free(deviceIdPtr);
      calloc.free(platformPtr);
    }
  }

  static void logout() {
    _logout();
  }

  static String? getUserId() {
    final buffer = calloc.allocate<Utf8>(256);
    try {
      final result = _getUserId(buffer.cast<Utf8>(), 256);
      if (result == CHIRP_OK) {
        return buffer.toDartString(length: 256).split('\x00')[0];
      }
    } finally {
      calloc.free(buffer);
    }
    return null;
  }

  static String? getSessionId() {
    final buffer = calloc.allocate<Utf8>(256);
    try {
      final result = _getSessionId(buffer.cast<Utf8>(), 256);
      if (result == CHIRP_OK) {
        return buffer.toDartString(length: 256).split('\x00')[0];
      }
    } finally {
      calloc.free(buffer);
    }
    return null;
  }

  static int sendMessage(String toUser, String channelId, int msgType,
      String content, int callbackId) {
    final toUserPtr = toUser.toNativeUtf8();
    final channelIdPtr = channelId.toNativeUtf8();
    final contentPtr = content.toNativeUtf8();
    try {
      return _sendMessage(
          toUserPtr, channelIdPtr, msgType, contentPtr, callbackId);
    } finally {
      calloc.free(toUserPtr);
      calloc.free(channelIdPtr);
      calloc.free(contentPtr);
    }
  }

  static int getHistory(String channelId, int channelType, int beforeTime,
      int limit, int callbackId) {
    final channelIdPtr = channelId.toNativeUtf8();
    try {
      return _getHistory(
          channelIdPtr, channelType, beforeTime, limit, callbackId);
    } finally {
      calloc.free(channelIdPtr);
    }
  }

  static int markRead(String channelId, int channelType, String messageId) {
    final channelIdPtr = channelId.toNativeUtf8();
    final messageIdPtr = messageId.toNativeUtf8();
    try {
      return _markRead(channelIdPtr, channelType, messageIdPtr);
    } finally {
      calloc.free(channelIdPtr);
      calloc.free(messageIdPtr);
    }
  }

  static int? getUnreadCount() {
    final countPtr = calloc.allocate<Int32>(1);
    try {
      final result = _getUnreadCount(countPtr);
      if (result == CHIRP_OK) {
        return countPtr.value;
      }
    } finally {
      calloc.free(countPtr);
    }
    return null;
  }

  static int joinVoiceRoom(String roomId, int callbackId) {
    final roomIdPtr = roomId.toNativeUtf8();
    try {
      return _joinVoiceRoom(roomIdPtr, callbackId);
    } finally {
      calloc.free(roomIdPtr);
    }
  }

  static int leaveVoiceRoom() {
    return _leaveVoiceRoom();
  }

  static int setMicMuted(int muted) {
    return _setMicMuted(muted);
  }

  static int setSpeakerMuted(int muted) {
    return _setSpeakerMuted(muted);
  }

  static bool isMicMuted() {
    return _isMicMuted() != 0;
  }

  static bool isSpeakerMuted() {
    return _isSpeakerMuted() != 0;
  }

  static int joinVoiceRoomWithType(
      String roomId, int roomType, int callbackId) {
    final roomIdPtr = roomId.toNativeUtf8();
    try {
      return _joinVoiceRoomWithType(roomIdPtr, roomType, callbackId);
    } finally {
      calloc.free(roomIdPtr);
    }
  }

  static int leaveVoiceRoomById(String roomId) {
    final roomIdPtr = roomId.toNativeUtf8();
    try {
      return _leaveVoiceRoomById(roomIdPtr);
    } finally {
      calloc.free(roomIdPtr);
    }
  }

  static int setVoiceMute(String roomId, int muted) {
    final roomIdPtr = roomId.toNativeUtf8();
    try {
      return _setVoiceMute(roomIdPtr, muted);
    } finally {
      calloc.free(roomIdPtr);
    }
  }

  static int sendIceCandidate(String roomId, String toUser, String candidate,
      String sdpMid, int sdpMLineIndex) {
    final roomIdPtr = roomId.toNativeUtf8();
    final toUserPtr = toUser.toNativeUtf8();
    final candidatePtr = candidate.toNativeUtf8();
    final sdpMidPtr = sdpMid.toNativeUtf8();
    try {
      return _sendIceCandidate(
          roomIdPtr, toUserPtr, candidatePtr, sdpMidPtr, sdpMLineIndex);
    } finally {
      calloc.free(roomIdPtr);
      calloc.free(toUserPtr);
      calloc.free(candidatePtr);
      calloc.free(sdpMidPtr);
    }
  }

  static int sendSdpAnswer(String roomId, String toUser, String sdpAnswer) {
    final roomIdPtr = roomId.toNativeUtf8();
    final toUserPtr = toUser.toNativeUtf8();
    final sdpAnswerPtr = sdpAnswer.toNativeUtf8();
    try {
      return _sendSdpAnswer(roomIdPtr, toUserPtr, sdpAnswerPtr);
    } finally {
      calloc.free(roomIdPtr);
      calloc.free(toUserPtr);
      calloc.free(sdpAnswerPtr);
    }
  }

  static int createVoiceRoom(
      int roomType, String roomName, int maxParticipants, int callbackId) {
    final roomNamePtr = roomName.toNativeUtf8();
    try {
      return _createVoiceRoom(
          roomType, roomNamePtr, maxParticipants, callbackId);
    } finally {
      calloc.free(roomNamePtr);
    }
  }

  static int getVoiceRoomInfo(String roomId, int callbackId) {
    final roomIdPtr = roomId.toNativeUtf8();
    try {
      return _getVoiceRoomInfo(roomIdPtr, callbackId);
    } finally {
      calloc.free(roomIdPtr);
    }
  }

  static void setMessageCallback(MessageCallback callback) {
    _messageHandler = callback;
    final pointer =
        Pointer.fromFunction<NativeMessageCallback>(_messageTrampoline);
    _callbacks.add(pointer);
    _setMessageCallback(pointer);
  }

  static void setResponseCallback(ResponseCallback callback) {
    _responseHandler = callback;
    final pointer =
        Pointer.fromFunction<NativeResponseCallback>(_responseTrampoline);
    _callbacks.add(pointer);
    _setResponseCallback(pointer);
  }

  static void setConnectionCallback(ConnectionCallback callback) {
    _connectionHandler = callback;
    final pointer =
        Pointer.fromFunction<NativeConnectionCallback>(_connectionTrampoline);
    _callbacks.add(pointer);
    _setConnectionCallback(pointer);
  }
}
