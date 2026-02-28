# Chirp API Documentation

## Table of Contents

- [Protocol Reference](#protocol-reference)
- [Service APIs](#service-apis)
- [SDK APIs](#sdk-apis)
- [Message Flows](#message-flows)

---

## Protocol Reference

### Common Types

```protobuf
enum ErrorCode {
  OK = 0;
  INTERNAL_ERROR = 1;
  INVALID_PARAM = 2;
  AUTH_FAILED = 3;
  SESSION_EXPIRED = 4;
  USER_NOT_FOUND = 5;
  TARGET_OFFLINE = 6;
}
```

### Gateway Protocol

```protobuf
enum MsgID {
  HEARTBEAT_PING = 1001;
  HEARTBEAT_PONG = 1002;
  LOGIN_REQ = 1003;
  LOGIN_RESP = 1004;
  KICK_NOTIFY = 1005;
  LOGOUT_REQ = 1006;
  LOGOUT_RESP = 1007;

  // Chat: 2000-2099
  SEND_MESSAGE_REQ = 2001;
  SEND_MESSAGE_RESP = 2002;
  GET_HISTORY_REQ = 2003;
  GET_HISTORY_RESP = 2004;
  CHAT_MESSAGE_NOTIFY = 2005;

  // Social: 3000-3099
  ADD_FRIEND_REQ = 3001;
  SET_PRESENCE_REQ = 3017;
  PRESENCE_NOTIFY = 3021;

  // Voice: 4000-4099
  CREATE_ROOM_REQ = 4001;
  JOIN_ROOM_REQ = 4003;
  LEAVE_ROOM_REQ = 4005;
}
```

### Auth Protocol

**LoginRequest**
```protobuf
message LoginRequest {
  string token = 1;           // JWT token or user_id
  string device_id = 2;
  string platform = 3;        // "ios", "android", "web", "pc"
}
```

**LoginResponse**
```protobuf
message LoginResponse {
  ErrorCode code = 1;
  string session_id = 2;
  int64 server_time = 3;
  string user_id = 4;
  bool kick_previous = 5;
  KickNotify kick = 6;
}
```

### Chat Protocol

**Message Types**
```protobuf
enum MsgType {
  TEXT = 0;
  EMOJI = 1;
  VOICE = 2;
  IMAGE = 3;
  SYSTEM = 99;
}
```

**Channel Types**
```protobuf
enum ChannelType {
  PRIVATE = 0;   // 1v1 private chat
  TEAM = 1;      // Team/group chat
  GUILD = 2;     // Guild chat
  WORLD = 3;     // World/global chat
}
```

**SendMessage**
```protobuf
message SendMessageRequest {
  string sender_id = 1;
  string receiver_id = 2;
  ChannelType channel_type = 3;
  string channel_id = 4;
  MsgType msg_type = 5;
  bytes content = 6;
  int64 client_timestamp = 7;
}
```

**ChatMessage**
```protobuf
message ChatMessage {
  string message_id = 1;
  string sender_id = 2;
  string receiver_id = 3;
  string channel_id = 4;
  ChannelType channel_type = 5;
  MsgType msg_type = 6;
  bytes content = 7;
  int64 timestamp = 8;
}
```

### Social Protocol

**Presence Status**
```protobuf
enum PresenceStatus {
  OFFLINE = 0;
  ONLINE = 1;
  AWAY = 2;
  DND = 3;
  IN_GAME = 4;
  IN_BATTLE = 5;
}
```

**SetPresence**
```protobuf
message SetPresenceRequest {
  string user_id = 1;
  PresenceStatus status = 2;
  string status_message = 3;
  map<string, string> metadata = 4;
}
```

**AddFriend**
```protobuf
message AddFriendRequest {
  string user_id = 1;
  string target_user_id = 2;
  string message = 3;
}
```

### Voice Protocol

**Room Types**
```protobuf
enum RoomType {
  PEER_TO_PEER = 0;
  GROUP = 1;
  CHANNEL = 2;
}
```

**CreateRoom**
```protobuf
message CreateRoomRequest {
  string user_id = 1;
  RoomType room_type = 2;
  string room_name = 3;
  int32 max_participants = 4;
  map<string, string> metadata = 5;
}
```

---

## Service APIs

### Gateway Service (ports 5000/5001)

**Endpoints:**
- TCP: `localhost:5000`
- WebSocket: `localhost:5001`

**Responsibilities:**
- Protocol message routing
- Connection management
- Session lifecycle
- Multi-device kick support

**Message Flow:**
```
Client → Gateway → Auth Service (login)
Client → Gateway → Chat/Social/Voice (requests)
Client ← Gateway ← Chat/Social/Voice (responses)
```

### Auth Service (port 6000)

**Endpoints:**
- TCP: `localhost:6000`

**Operations:**

| Operation | Request | Response |
|-----------|---------|----------|
| Login | `LOGIN_REQ` | `LOGIN_RESP` |
| Logout | `LOGOUT_REQ` | `LOGOUT_RESP` |

**Multi-Device Handling:**
- Last-login-wins policy
- Kick notification to previous device
- Session validation

### Chat Service (ports 7000/7001)

**Endpoints:**
- TCP: `localhost:7000`
- WebSocket: `localhost:7001`

**Operations:**

| Operation | Request | Response | Notify |
|-----------|---------|----------|--------|
| Send Message | `SEND_MESSAGE_REQ` | `SEND_MESSAGE_RESP` | `CHAT_MESSAGE_NOTIFY` |
| Get History | `GET_HISTORY_REQ` | `GET_HISTORY_RESP` | - |
| Mark Read | `MARK_READ_REQ` | `MARK_READ_RESP` | `MESSAGE_READ_NOTIFY` |
| Get Unread | `GET_UNREAD_COUNT_REQ` | `GET_UNREAD_COUNT_RESP` | - |
| Create Group | `CREATE_GROUP_REQ` | `CREATE_GROUP_RESP` | `GROUP_CREATED_NOTIFY` |
| Join Group | `JOIN_GROUP_REQ` | `JOIN_GROUP_RESP` | `GROUP_MEMBER_JOINED_NOTIFY` |
| Leave Group | `LEAVE_GROUP_REQ` | `LEAVE_GROUP_RESP` | `GROUP_MEMBER_LEFT_NOTIFY` |

**Offline Message Handling:**
- Messages stored in Redis when recipient offline
- Delivered on next login
- TTL: 7 days (configurable)

### Social Service (ports 8000/8001)

**Endpoints:**
- TCP: `localhost:8000`
- WebSocket: `localhost:8001`

**Operations:**

| Operation | Request | Response | Notify |
|-----------|---------|----------|--------|
| Add Friend | `ADD_FRIEND_REQ` | `ADD_FRIEND_RESP` | `FRIEND_REQUEST_NOTIFY` |
| Accept Friend | `FRIEND_REQUEST_ACTION_REQ` | `FRIEND_REQUEST_ACTION_RESP` | `FRIEND_ACCEPTED_NOTIFY` |
| Remove Friend | `REMOVE_FRIEND_REQ` | `REMOVE_FRIEND_RESP` | `FRIEND_REMOVED_NOTIFY` |
| Set Presence | `SET_PRESENCE_REQ` | `SET_PRESENCE_RESP` | `PRESENCE_NOTIFY` |
| Get Presence | `GET_PRESENCE_REQ` | `GET_PRESENCE_RESP` | - |
| Block User | `BLOCK_USER_REQ` | `BLOCK_USER_RESP` | - |

**Presence Propagation:**
- Redis Pub/Sub for cross-instance sync
- Broadcast to online friends
- Last seen tracking

### Voice Service (ports 9000/9001)

**Endpoints:**
- TCP: `localhost:9000`
- WebSocket: `localhost:9001`

**Operations:**

| Operation | Request | Response | Notify |
|-----------|---------|----------|--------|
| Create Room | `CREATE_ROOM_REQ` | `CREATE_ROOM_RESP` | - |
| Join Room | `JOIN_ROOM_REQ` | `JOIN_ROOM_RESP` | `PARTICIPANT_JOINED_NOTIFY` |
| Leave Room | `LEAVE_ROOM_REQ` | `LEAVE_ROOM_RESP` | `PARTICIPANT_LEFT_NOTIFY` |
| ICE Candidate | `ICE_CANDIDATE_MSG` | - | Relayed to peer |
| SDP Offer/Answer | `SDP_OFFER_MSG/SDP_ANSWER_MSG` | - | Relayed to peer |
| Set Mute | `SET_MUTE_REQ` | `SET_MUTE_RESP` | `PARTICIPANT_STATE_CHANGED_NOTIFY` |

**WebRTC Signaling:**
- SDP offer/answer relay between peers
- ICE candidate exchange
- Room participant tracking

---

## SDK APIs

### Core SDK

```cpp
namespace chirp::core {

// SDK singleton
class SDK {
public:
  static bool Initialize(const Config& config);
  static void Shutdown();
  static Client* GetClient();
  static bool IsInitialized();
};

// Configuration
struct Config {
  string app_id;
  string user_id;
  string device_id;
  string platform;

  ConnectionConfig connection;
  bool enable_logging;
  uint32_t thread_pool_size;
};

}
```

### Client Interface

```cpp
class Client {
public:
  // Connection
  virtual bool Connect() = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;
  virtual ConnectionState GetConnectionState() const = 0;

  // Authentication
  virtual bool Login(const std::string& user_id,
                     const std::string& token = "") = 0;
  virtual void Logout() = 0;

  // Module access
  virtual ChatModule* GetChatModule() = 0;
  virtual SocialModule* GetSocialModule() = 0;
  virtual VoiceModule* GetVoiceModule() = 0;

  // Events
  virtual void SetConnectionStateCallback(
    ConnectionStateCallback callback) = 0;
};
```

### Chat Module

```cpp
class ChatModule {
public:
  // Send messages
  virtual void SendMessage(
    const std::string& to_user,
    MessageType type,
    const std::string& content,
    SendMessageCallback callback) = 0;

  virtual void SendChannelMessage(
    const std::string& channel_id,
    ChannelType channel_type,
    MessageType type,
    const std::string& content,
    SendMessageCallback callback) = 0;

  // History
  virtual void GetHistory(
    const std::string& channel_id,
    ChannelType channel_type,
    int64_t before_timestamp,
    int32_t limit,
    GetHistoryCallback callback) = 0;

  // Read receipts
  virtual void MarkRead(
    const std::string& channel_id,
    ChannelType channel_type,
    const std::string& message_id) = 0;

  virtual void GetUnreadCount(
    std::function<void(int32_t)> callback) = 0;

  // Events
  virtual void SetMessageCallback(
    MessageCallback callback) = 0;
  virtual void SetMessageReadCallback(
    MessageReadCallback callback) = 0;
  virtual void SetTypingCallback(
    TypingCallback callback) = 0;

  // Groups
  virtual void CreateGroup(
    const std::string& name,
    const std::vector<std::string>& members,
    std::function<void(const std::string& group_id)> callback) = 0;
};
```

### Social Module

```cpp
class SocialModule {
public:
  // Friends
  virtual void AddFriend(
    const std::string& user_id,
    const std::string& message,
    AddFriendCallback callback) = 0;

  virtual void RemoveFriend(
    const std::string& user_id,
    SimpleCallback callback) = 0;

  virtual void GetFriendList(
    int32_t limit,
    int32_t offset,
    FriendListCallback callback) = 0;

  // Presence
  virtual void SetPresence(
    PresenceStatus status,
    const std::string& status_message,
    const std::string& game_name) = 0;

  virtual void GetPresence(
    const std::vector<std::string>& user_ids,
    PresenceCallback callback) = 0;

  // Block
  virtual void BlockUser(
    const std::string& user_id,
    SimpleCallback callback) = 0;
};
```

### Voice Module

```cpp
class VoiceModule {
public:
  // Rooms
  virtual void CreateRoom(
    RoomType type,
    const std::string& name,
    int32_t max_participants,
    CreateRoomCallback callback) = 0;

  virtual void JoinRoom(
    const std::string& room_id,
    const std::string& sdp_offer,
    JoinRoomCallback callback) = 0;

  virtual void LeaveRoom(
    const std::string& room_id,
    SimpleCallback callback) = 0;

  // Audio control
  virtual void SetMuted(bool muted) = 0;
  virtual void SetDeafened(bool deafened) = 0;
  virtual bool IsMuted() const = 0;
  virtual bool IsDeafened() const = 0;

  // Events
  virtual void SetParticipantJoinedCallback(
    ParticipantJoinedCallback callback) = 0;
  virtual void SetSpeakingCallback(
    SpeakingCallback callback) = 0;
};
```

---

## Message Flows

### Authentication Flow

```
Client                  Gateway               Auth
  │                       │                    │
  ├─────LOGIN_REQ────────▶│                    │
  │                       ├─────────LOGIN_REQ─────▶│
  │                       │                    │
  │                       │◀────LOGIN_RESP────────┤
  │◀────LOGIN_RESP────────┤                    │
  │                       │                    │
```

### Message Delivery Flow

```
Client1                 Chat                  Client2
  │                       │                     │
  ├─────SEND───────REQ──▶│                     │
  │                       │                     │
  │◀────SEND───────RESP──┤                     │
  │                       ├────MSG_NOTIFY────────▶│
  │                       │                     │
```

### Presence Update Flow

```
Client1                 Social                Client2
  │                       │                     │
  ├─────SET_PRESENCE────REQ──▶│                     │
  │                       │                     │
  │◀────SET_PRESENCE────RESP──┤                     │
  │                       │                     │
  │                       ├────PRESENCE──NOTIFY──▶│
  │                       │                     │
```

### Voice Room Flow

```
Client1                 Voice                 Client2
  │                       │                     │
  ├─────CREATE_ROOM────REQ──▶│                     │
  │                       │                     │
  │◀────CREATE_ROOM────RESP──┤                     │
  │                       │                     │
  ├─────JOIN_ROOM──────REQ──▶│                     │
  │                       │◀────SDP_OFFER────────┤
  │                       │                     │
  │                       ├────SDP_OFFER────────▶│
  │                       │                     │
```

---

## Error Handling

All responses include an `ErrorCode` field:

| Code | Meaning |
|------|---------|
| `OK = 0` | Success |
| `INVALID_PARAM = 2` | Missing or invalid parameters |
| `AUTH_FAILED = 3` | Authentication failed |
| `SESSION_EXPIRED = 4` | Session no longer valid |
| `USER_NOT_FOUND = 5` | Target user doesn't exist |
| `TARGET_OFFLINE = 6` | Recipient is offline |

---

## Rate Limiting

Default limits (configurable):

| Operation | Limit | Window |
|-----------|-------|--------|
| Send Message | 100/sec | Rolling |
| Login | 10/min | Rolling |
| Presence Updates | 10/sec | Rolling |

---

## WebSocket URL Format

```
ws://localhost:5001/  # Gateway WebSocket
ws://localhost:7001/  # Chat WebSocket
ws://localhost:8001/  # Social WebSocket
ws://localhost:9001/  # Voice WebSocket
```

---

## Data Types

**Message Content:**
- TEXT: UTF-8 string
- EMOJI: UTF-8 emoji sequence
- VOICE: Binary audio data (base64 encoded)
- IMAGE: URL or base64 encoded data
- SYSTEM: UTF-8 string

**Timestamps:**
- All timestamps are milliseconds since Unix epoch
- `client_timestamp`: Client's local time
- `server_timestamp`: Server's time

**IDs:**
- `message_id`: Server-assigned, format: `msg_{timestamp}_{counter}`
- `session_id`: 32 hex characters
- `group_id`: Format: `group_{timestamp}_{counter}`
- `room_id`: Format: `room_{8_hex_chars}`
