using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Chirp
{
    /// <summary>
    /// Main Chirp SDK class for Unity integration.
    /// Provides access to chat, social, and voice features.
    /// </summary>
    public class ChirpSDK
    {
        private static ChirpSDK _instance;
        private bool _isInitialized = false;
        private bool _isConnected = false;

        // Events
        public event Action<ChatMessage> OnMessageReceived;
        public event Action<int, bool, string> OnResponse;
        public event Action<bool, int> OnConnectionStateChanged;
        public event Action<string, string> OnVoiceEvent;

        // Platform-specific library names
#if UNITY_IOS && !UNITY_EDITOR
        private const string DLL_NAME = "__Internal";
#elif UNITY_ANDROID && !UNITY_EDITOR
        private const string DLL_NAME = "chirp_unity";
#else
        private const string DLL_NAME = "chirp_unity";  // Windows, macOS, Linux
#endif

        // ============================================================================
        // DllImport Declarations
        // ============================================================================

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_Initialize(string configJson);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern void Chirp_Shutdown();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_Connect();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern void Chirp_Disconnect();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_IsConnected();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_Login(string userId, string token,
            string deviceId, string platform);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern void Chirp_Logout();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_GetUserId(StringBuilder buffer, uint bufferSize);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_GetSessionId(StringBuilder buffer, uint bufferSize);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_SendMessage(string toUserId, string channelId,
            int channelType, string content, int callbackId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_GetHistory(string channelId, int channelType,
            long beforeTimestamp, int limit, int callbackId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_MarkRead(string channelId, int channelType,
            string messageId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_GetUnreadCount(ref int count);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern void Chirp_SetMessageCallback(IntPtr callback);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern void Chirp_SetResponseCallback(IntPtr callback);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern void Chirp_SetConnectionCallback(IntPtr callback);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_JoinVoiceRoom(string roomId, int callbackId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_LeaveVoiceRoom();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_SetMicMuted(int muted);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_SetSpeakerMuted(int muted);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_IsMicMuted();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        private static extern int Chirp_IsSpeakerMuted();

        // ============================================================================
        // Delegate Declarations (for callbacks from native code)
        // ============================================================================

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void MessageCallbackDelegate(string messageJson);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void ResponseCallbackDelegate(int callbackId, int success, string dataJson);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void ConnectionCallbackDelegate(int connected, int errorCode);

        // Native callback delegates (must be kept alive to prevent GC)
        private MessageCallbackDelegate _messageCallbackDelegate;
        private ResponseCallbackDelegate _responseCallbackDelegate;
        private ConnectionCallbackDelegate _connectionCallbackDelegate;

        // ============================================================================
        // Singleton Instance
        // ============================================================================

        public static ChirpSDK Instance
        {
            get
            {
                if (_instance == null)
                {
                    _instance = new ChirpSDK();
                }
                return _instance;
            }
        }

        private ChirpSDK()
        {
            // Initialize callback delegates
            _messageCallbackDelegate = OnNativeMessage;
            _responseCallbackDelegate = OnNativeResponse;
            _connectionCallbackDelegate = OnNativeConnectionState;
        }

        // ============================================================================
        // Core API
        // ============================================================================

        /// <summary>
        /// Initialize the Chirp SDK with configuration.
        /// </summary>
        public bool Initialize(ChirpConfig config)
        {
            if (_isInitialized)
            {
                UnityEngine.Debug.LogWarning("[ChirpSDK] Already initialized");
                return true;
            }

            string configJson = config.ToJson();
            int result = Chirp_Initialize(configJson);

            if (result == 0)  // CHIRP_OK
            {
                _isInitialized = true;

                // Set up native callbacks
                Chirp_SetMessageCallback(Marshal.GetFunctionPointerForDelegate(_messageCallbackDelegate));
                Chirp_SetResponseCallback(Marshal.GetFunctionPointerForDelegate(_responseCallbackDelegate));
                Chirp_SetConnectionCallback(Marshal.GetFunctionPointerForDelegate(_connectionCallbackDelegate));

                UnityEngine.Debug.Log("[ChirpSDK] Initialized successfully");
                return true;
            }
            else
            {
                UnityEngine.Debug.LogError($"[ChirpSDK] Initialization failed with error: {result}");
                return false;
            }
        }

        /// <summary>
        /// Shutdown the Chirp SDK.
        /// </summary>
        public void Shutdown()
        {
            if (!_isInitialized) return;

            Chirp_Shutdown();
            _isInitialized = false;
            _isConnected = false;
            UnityEngine.Debug.Log("[ChirpSDK] Shutdown complete");
        }

        /// <summary>
        /// Connect to the Chirp server.
        /// </summary>
        public bool Connect()
        {
            if (!_isInitialized)
            {
                UnityEngine.Debug.LogError("[ChirpSDK] Not initialized");
                return false;
            }

            int result = Chirp_Connect();
            return result == 0;
        }

        /// <summary>
        /// Disconnect from the Chirp server.
        /// </summary>
        public void Disconnect()
        {
            Chirp_Disconnect();
        }

        /// <summary>
        /// Check if connected to the server.
        /// </summary>
        public bool IsConnected
        {
            get { return Chirp_IsConnected() != 0; }
        }

        /// <summary>
        /// Login with user credentials.
        /// </summary>
        public bool Login(string userId, string token, string deviceId = "")
        {
            if (!_isInitialized)
            {
                UnityEngine.Debug.LogError("[ChirpSDK] Not initialized");
                return false;
            }

            string platform = GetPlatform();
            int result = Chirp_Login(userId, token, deviceId, platform);
            return result == 0;
        }

        /// <summary>
        /// Logout from current session.
        /// </summary>
        public void Logout()
        {
            Chirp_Logout();
        }

        /// <summary>
        /// Get current user ID.
        /// </summary>
        public string GetUserId()
        {
            StringBuilder sb = new StringBuilder(256);
            int result = Chirp_GetUserId(sb, 256);
            return result == 0 ? sb.ToString() : "";
        }

        /// <summary>
        /// Get current session ID.
        /// </summary>
        public string GetSessionId()
        {
            StringBuilder sb = new StringBuilder(256);
            int result = Chirp_GetSessionId(sb, 256);
            return result == 0 ? sb.ToString() : "";
        }

        // ============================================================================
        // Chat API
        // ============================================================================

        /// <summary>
        /// Send a text message.
        /// </summary>
        public bool SendMessage(string toUserId, string content,
            Action<bool, string> callback = null)
        {
            if (!_isInitialized || !_isConnected)
            {
                UnityEngine.Debug.LogError("[ChirpSDK] Not connected");
                return false;
            }

            int callbackId = callback != null ? RegisterCallback(callback) : -1;
            int result = Chirp_SendMessage(toUserId, "", 0, content, callbackId);
            return result == 0;
        }

        /// <summary>
        /// Get chat history.
        /// </summary>
        public bool GetHistory(string channelId, ChannelType channelType,
            int limit, Action<bool, ChatMessage[]> callback)
        {
            if (!_isInitialized || !_isConnected)
            {
                return false;
            }

            int callbackId = RegisterCallback((success, data) => {
                if (success && callback != null)
                {
                    var messages = ParseHistoryMessages(data);
                    callback(true, messages);
                }
                else
                {
                    callback(false, new ChatMessage[0]);
                }
            });

            int result = Chirp_GetHistory(channelId, (int)channelType, 0, limit, callbackId);
            return result == 0;
        }

        /// <summary>
        /// Mark messages as read.
        /// </summary>
        public bool MarkRead(string channelId, ChannelType channelType, string messageId)
        {
            if (!_isInitialized || !_isConnected)
            {
                return false;
            }

            int result = Chirp_MarkRead(channelId, (int)channelType, messageId);
            return result == 0;
        }

        /// <summary>
        /// Get total unread message count.
        /// </summary>
        public int GetUnreadCount()
        {
            int count = 0;
            Chirp_GetUnreadCount(ref count);
            return count;
        }

        // ============================================================================
        // Voice API
        // ============================================================================

        /// <summary>
        /// Join a voice room.
        /// </summary>
        public bool JoinVoiceRoom(string roomId, Action<bool> callback = null)
        {
            if (!_isInitialized || !_isConnected)
            {
                return false;
            }

            int callbackId = callback != null ? RegisterCallback((success, data) => callback(success)) : -1;
            int result = Chirp_JoinVoiceRoom(roomId, callbackId);
            return result == 0;
        }

        /// <summary>
        /// Leave the current voice room.
        /// </summary>
        public bool LeaveVoiceRoom()
        {
            if (!_isInitialized) return false;

            int result = Chirp_LeaveVoiceRoom();
            return result == 0;
        }

        /// <summary>
        /// Set microphone mute state.
        /// </summary>
        public bool SetMicMuted(bool muted)
        {
            if (!_isInitialized) return false;

            int result = Chirp_SetMicMuted(muted ? 1 : 0);
            return result == 0;
        }

        /// <summary>
        /// Set speaker mute state.
        /// </summary>
        public bool SetSpeakerMuted(bool muted)
        {
            if (!_isInitialized) return false;

            int result = Chirp_SetSpeakerMuted(muted ? 1 : 0);
            return result == 0;
        }

        /// <summary>
        /// Check if microphone is muted.
        /// </summary>
        public bool IsMicMuted()
        {
            if (!_isInitialized) return false;

            return Chirp_IsMicMuted() != 0;
        }

        /// <summary>
        /// Check if speaker is muted.
        /// </summary>
        public bool IsSpeakerMuted()
        {
            if (!_isInitialized) return false;

            return Chirp_IsSpeakerMuted() != 0;
        }

        // ============================================================================
        // Native Callback Handlers
        // ============================================================================

        [AOT.MonoPInvokeCallback(typeof(MessageCallbackDelegate))]
        private static void OnNativeMessage(string messageJson)
        {
            if (_instance != null && _instance.OnMessageReceived != null)
            {
                var message = ParseChatMessage(messageJson);
                _instance.OnMessageReceived(message);
            }
        }

        [AOT.MonoPInvokeCallback(typeof(ResponseCallbackDelegate))]
        private static void OnNativeResponse(int callbackId, int success, string dataJson)
        {
            if (_instance != null && _instance.OnResponse != null)
            {
                _instance.OnResponse(callbackId, success != 0, dataJson);
            }
            _instance?.InvokeCallback(callbackId, success != 0, dataJson);
        }

        [AOT.MonoPInvokeCallback(typeof(ConnectionCallbackDelegate))]
        private static void OnNativeConnectionState(int connected, int errorCode)
        {
            if (_instance != null)
            {
                _instance._isConnected = connected != 0;
                if (_instance.OnConnectionStateChanged != null)
                {
                    _instance.OnConnectionStateChanged(connected != 0, errorCode);
                }
            }
        }

        // ============================================================================
        // Utility Methods
        // ============================================================================

        private string GetPlatform()
        {
#if UNITY_IOS
            return "ios";
#elif UNITY_ANDROID
            return "android";
#elif UNITY_WEBGL
            return "web";
#else
            return "pc";
#endif
        }

        private int _nextCallbackId = 0;
        private System.Collections.Generic.Dictionary<int, System.Action<bool, string>> _callbacks
            = new System.Collections.Generic.Dictionary<int, System.Action<bool, string>>();

        private int RegisterCallback(System.Action<bool, string> callback)
        {
            int id = ++_nextCallbackId;
            _callbacks[id] = callback;
            return id;
        }

        private void InvokeCallback(int callbackId, bool success, string data)
        {
            if (_callbacks.TryGetValue(callbackId, out var callback))
            {
                _callbacks.Remove(callbackId);
                callback?.Invoke(success, data);
            }
        }

        private ChatMessage ParseChatMessage(string json)
        {
            // Simple JSON parsing (use JsonUtility in production)
            return new ChatMessage
            {
                MessageId = "",
                SenderId = "",
                Content = json,
                Timestamp = DateTime.UtcNow.Ticks
            };
        }

        private ChatMessage[] ParseHistoryMessages(string json)
        {
            // Parse JSON array
            return new ChatMessage[0];
        }
    }

    // ============================================================================
    // Supporting Classes
    // ============================================================================

    /// <summary>
    /// Chirp SDK configuration.
    /// </summary>
    [Serializable]
    public class ChirpConfig
    {
        public string gatewayHost = "127.0.0.1";
        public int gatewayPort = 7000;
        public string appId = "chirp_unity";

        public string ToJson()
        {
            return $"{{\"gateway_host\":\"{gatewayHost}\",\"gateway_port\":{gatewayPort},\"app_id\":\"{appId}\"}}";
        }
    }

    /// <summary>
    /// Chat message data.
    /// </summary>
    [Serializable]
    public class ChatMessage
    {
        public string MessageId;
        public string SenderId;
        public string ReceiverId;
        public string ChannelId;
        public ChannelType ChannelType;
        public MsgType MsgType;
        public string Content;
        public long Timestamp;
    }

    /// <summary>
    /// Channel types.
    /// </summary>
    public enum ChannelType
    {
        Private = 0,
        Team = 1,
        Guild = 2,
        World = 3
    }

    /// <summary>
    /// Message types.
    /// </summary>
    public enum MsgType
    {
        Text = 0,
        Emoji = 1,
        Voice = 2,
        Image = 3,
        System = 99
    }
}
