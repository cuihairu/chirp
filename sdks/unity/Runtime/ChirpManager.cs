using UnityEngine;

namespace Chirp
{
    /// <summary>
    /// MonoBehaviour component for easy Chirp SDK integration in Unity scenes.
    /// Attach this to a GameObject to enable Chirp functionality.
    /// </summary>
    [AddComponentMenu("Chirp/Chirp Manager")]
    public class ChirpManager : MonoBehaviour
    {
        [Header("Configuration")]
        [SerializeField] private string gatewayHost = "127.0.0.1";
        [SerializeField] private int gatewayPort = 7000;
        [SerializeField] private string appId = "chirp_unity";

        [Header("Auto-Connect")]
        [SerializeField] private bool autoConnect = false;
        [SerializeField] private bool autoLogin = false;
        [SerializeField] private string testUserId = "";
        [SerializeField] private string testToken = "";

        [Header("Voice Settings")]
        [SerializeField] private bool micMuted = false;
        [SerializeField] private bool speakerMuted = false;

        // Public events for UI binding
        public event System.Action<bool> OnConnectedChanged;
        public event System.Action<ChatMessage> OnChatMessage;
        public event System.Action<int> OnUnreadCountChanged;

        private ChirpSDK sdk;
        private int lastUnreadCount = 0;

        // ============================================================================
        // Unity Lifecycle
        // ============================================================================

        private void Awake()
        {
            // Ensure singleton pattern
            if (FindObjectOfType<ChirpManager>() != this)
            {
                Destroy(gameObject);
                return;
            }

            DontDestroyOnLoad(gameObject);

            // Initialize SDK
            sdk = ChirpSDK.Instance;
            InitializeSDK();
        }

        private void Start()
        {
            if (autoConnect)
            {
                Connect();

                if (autoLogin && !string.IsNullOrEmpty(testUserId) && !string.IsNullOrEmpty(testToken))
                {
                    Login(testUserId, testToken);
                }
            }
        }

        private void Update()
        {
            // Poll for unread count changes
            if (sdk != null && sdk.IsConnected)
            {
                int unreadCount = sdk.GetUnreadCount();
                if (unreadCount != lastUnreadCount)
                {
                    lastUnreadCount = unreadCount;
                    OnUnreadCountChanged?.Invoke(unreadCount);
                }
            }
        }

        private void OnDestroy()
        {
            if (sdk != null)
            {
                sdk.Disconnect();
                sdk.Shutdown();
            }
        }

        private void OnApplicationPause(bool pauseStatus)
        {
            if (pauseStatus)
            {
                // App going to background
                Debug.Log("[ChirpManager] App pausing, disconnecting...");
                sdk?.Disconnect();
            }
            else
            {
                // App returning from background
                Debug.Log("[ChirpManager] App resuming, reconnecting...");
                sdk?.Connect();
            }
        }

        // ============================================================================
        // SDK Initialization
        // ============================================================================

        private void InitializeSDK()
        {
            var config = new ChirpConfig
            {
                gatewayHost = gatewayHost,
                gatewayPort = gatewayPort,
                appId = appId
            };

            if (sdk.Initialize(config))
            {
                // Set up event handlers
                sdk.OnConnectionStateChanged += OnConnectionStateChanged;
                sdk.OnMessageReceived += OnMessageReceived;
                sdk.OnResponse += OnResponse;

                Debug.Log("[ChirpManager] SDK initialized successfully");
            }
            else
            {
                Debug.LogError("[ChirpManager] SDK initialization failed");
            }
        }

        // ============================================================================
        // Connection Methods
        // ============================================================================

        public void Connect()
        {
            if (sdk != null)
            {
                bool connected = sdk.Connect();
                Debug.Log($"[ChirpManager] Connect result: {connected}");
            }
        }

        public void Disconnect()
        {
            if (sdk != null)
            {
                sdk.Disconnect();
            }
        }

        public void Login(string userId, string token, string deviceId = "")
        {
            if (sdk != null)
            {
                bool success = sdk.Login(userId, token, deviceId);
                Debug.Log($"[ChirpManager] Login result: {success}");
            }
        }

        public void Logout()
        {
            if (sdk != null)
            {
                sdk.Logout();
            }
        }

        // ============================================================================
        // Chat Methods
        // ============================================================================

        public void SendMessage(string toUserId, string content, System.Action<bool, string> callback = null)
        {
            if (sdk != null && sdk.IsConnected)
            {
                sdk.SendMessage(toUserId, content, (success, data) =>
                {
                    callback?.Invoke(success, data);
                    Debug.Log($"[ChirpManager] Send message result: {success}");
                });
            }
            else
            {
                Debug.LogWarning("[ChirpManager] Cannot send message: not connected");
                callback?.Invoke(false, "Not connected");
            }
        }

        public void GetHistory(string channelId, ChannelType channelType, int limit,
            System.Action<bool, ChatMessage[]> callback)
        {
            if (sdk != null && sdk.IsConnected)
            {
                sdk.GetHistory(channelId, channelType, limit, callback);
            }
        }

        public void MarkAsRead(string channelId, ChannelType channelType, string messageId)
        {
            if (sdk != null && sdk.IsConnected)
            {
                sdk.MarkRead(channelId, channelType, messageId);
            }
        }

        public int GetUnreadCount()
        {
            return sdk?.GetUnreadCount() ?? 0;
        }

        // ============================================================================
        // Voice Methods
        // ============================================================================

        public void JoinVoiceRoom(string roomId, System.Action<bool> callback = null)
        {
            if (sdk != null && sdk.IsConnected)
            {
                sdk.JoinVoiceRoom(roomId, callback);
            }
        }

        public void LeaveVoiceRoom()
        {
            if (sdk != null)
            {
                sdk.LeaveVoiceRoom();
            }
        }

        public void SetMicMuted(bool muted)
        {
            if (sdk != null)
            {
                micMuted = sdk.SetMicMuted(muted);
            }
        }

        public void SetSpeakerMuted(bool muted)
        {
            if (sdk != null)
            {
                speakerMuted = sdk.SetSpeakerMuted(muted);
            }
        }

        public bool IsMicMuted()
        {
            return sdk?.IsMicMuted() ?? true;
        }

        public bool IsSpeakerMuted()
        {
            return sdk?.IsSpeakerMuted() ?? true;
        }

        // ============================================================================
        // Event Handlers
        // ============================================================================

        private void OnConnectionStateChanged(bool connected, int errorCode)
        {
            Debug.Log($"[ChirpManager] Connection state changed: {connected}, error: {errorCode}");
            OnConnectedChanged?.Invoke(connected);
        }

        private void OnMessageReceived(ChatMessage message)
        {
            Debug.Log($"[ChirpManager] Message received from {message.SenderId}: {message.Content}");
            OnChatMessage?.Invoke(message);
        }

        private void OnResponse(int callbackId, bool success, string data)
        {
            Debug.Log($"[ChirpManager] Response callback {callbackId}: {success}");
        }

        // ============================================================================
        // Properties
        // ============================================================================

        public bool IsConnected => sdk?.IsConnected ?? false;

        public string UserId => sdk?.GetUserId() ?? "";

        public string SessionId => sdk?.GetSessionId() ?? "";
    }
}
