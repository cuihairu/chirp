import 'dart:ffi';
import 'package:ffi/ffi.dart';

/// Native FFI bindings for the Chirp C++ SDK
/// This class provides the interface to the native chirp_unity bridge library
class ChirpFFI {
  // Native library names by platform
  static const String _libName = 'chirp_unity';

  // Load the native library
  static DynamicLibrary _lib = DynamicLibrary.open(_libName);

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

  // Core API functions
  late final int Function(String) _initialize = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>)>>('Chirp_Initialize')
      .asFunction();

  late final void Function() _shutdown = _lib
      .lookup<NativeFunction<Void Function()>>('Chirp_Shutdown')
      .asFunction();

  late final int Function() _connect = _lib
      .lookup<NativeFunction<Int32 Function()>>('Chirp_Connect')
      .asFunction();

  late final void Function() _disconnect = _lib
      .lookup<NativeFunction<Void Function()>>('Chirp_Disconnect')
      .asFunction();

  late final int Function() _isConnected = _lib
      .lookup<NativeFunction<Int32 Function()>>('Chirp_IsConnected')
      .asFunction();

  late final int Function(String, String, String, String) _login = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>, Pointer<Char>, Pointer<Char>, Pointer<Char>)>>('Chirp_Login')
      .asFunction();

  late final void Function() _logout = _lib
      .lookup<NativeFunction<Void Function()>>('Chirp_Logout')
      .asFunction();

  late final int Function(Pointer<Char>, Uint32) _getUserId = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>, Uint32)>>('Chirp_GetUserId')
      .asFunction();

  late final int Function(Pointer<Char>, Uint32) _getSessionId = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>, Uint32)>>('Chirp_GetSessionId')
      .asFunction();

  // Chat API functions
  late final int Function(String, String, Int32, String, Int32) _sendMessage = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>, Pointer<Char>, Int32, Pointer<Char>, Int32)>>('Chirp_SendMessage')
      .asFunction();

  late final int Function(String, Int32, Int64, Int32, Int32) _getHistory = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>, Int32, Int64, Int32, Int32)>>('Chirp_GetHistory')
      .asFunction();

  late final int Function(String, Int32, String) _markRead = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>, Int32, Pointer<Char>)>>('Chirp_MarkRead')
      .asFunction();

  late final int Function(Pointer<Int32>) _getUnreadCount = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Int32>)>>('Chirp_GetUnreadCount')
      .asFunction();

  // Voice API functions
  late final int Function(String, Int32) _joinVoiceRoom = _lib
      .lookup<NativeFunction<Int32 Function(Pointer<Char>, Int32)>>('Chirp_JoinVoiceRoom')
      .asFunction();

  late final int Function() _leaveVoiceRoom = _lib
      .lookup<NativeFunction<Int32 Function()>>('Chirp_LeaveVoiceRoom')
      .asFunction();

  late final int Function(Int32) _setMicMuted = _lib
      .lookup<NativeFunction<Int32 Function(Int32)>>('Chirp_SetMicMuted')
      .asFunction();

  late final int Function(Int32) _setSpeakerMuted = _lib
      .lookup<NativeFunction<Int32 Function(Int32)>>('Chirp_SetSpeakerMuted')
      .asFunction();

  late final int Function() _isMicMuted = _lib
      .lookup<NativeFunction<Int32 Function()>>('Chirp_IsMicMuted')
      .asFunction();

  late final int Function() _isSpeakerMuted = _lib
      .lookup<NativeFunction<Int32 Function()>>('Chirp_IsSpeakerMuted')
      .asFunction();

  // Callback setters
  late final void Function<Pointer> _setMessageCallback = _lib
      .lookup<NativeFunction<Void Function<Pointer>>>('Chirp_SetMessageCallback')
      .asFunction();

  late final void Function<Pointer> _setResponseCallback = _lib
      .lookup<NativeFunction<Void Function<Pointer>>>('Chirp_SetResponseCallback')
      .asFunction();

  late final void Function<Pointer> _setConnectionCallback = _lib
      .lookup<NativeFunction<Void Function<Pointer>>>('Chirp_SetConnectionCallback')
      .asFunction();

  // Native callback types
  typedef Void Function(Pointer<Char>) MessageCallbackNative;
  typedef Void Function(Int32, Int32, Pointer<Char>) ResponseCallbackNative;
  typedef Void Function(Int32, Int32) ConnectionCallbackNative;

  // Keep references to callbacks to prevent garbage collection
  final List<Pointer<NativeFunction<>>> _callbacks = [];
}
