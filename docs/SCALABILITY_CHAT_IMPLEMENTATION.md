# Chat 服务分布式改造示例

## 改造后的 Chat 服务主文件

```cpp
// services/chat/src/main_distributed.cc
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "common/logger.h"
#include "network/message_router.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

uint16_t ParseU16Arg(int argc, char** argv, const std::string& key, uint16_t def) {
  return static_cast<uint16_t>(std::atoi(GetArg(argc, argv, key, std::to_string(def)).c_str()));
}

std::string GenerateMessageId() {
  static std::atomic<uint64_t> counter{1};
  return "msg_" + std::to_string(NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

/// @brief 分布式聊天状态管理
struct DistributedChatState {
  std::mutex mu;

  // 本地用户会话映射 (user_id -> session)
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> local_sessions;

  // 会话到用户的反向映射
  std::unordered_map<void*, std::string> session_to_user;

  // 当前实例ID
  std::string instance_id;

  /// @brief 添加本地会话
  void AddSession(const std::string& user_id, std::shared_ptr<chirp::network::Session> session) {
    std::lock_guard<std::mutex> lock(mu);
    local_sessions[user_id] = session;
    session_to_user[session.get()] = user_id;
  }

  /// @brief 移除会话
  void RemoveSession(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = session_to_user.find(session);
    if (it != session_to_user.end()) {
      local_sessions.erase(it->second);
      session_to_user.erase(it);
    }
  }

  /// @brief 获取本地会话
  std::shared_ptr<chirp::network::Session> GetLocalSession(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = local_sessions.find(user_id);
    if (it != local_sessions.end()) {
      return it->second.lock();
    }
    return nullptr;
  }

  /// @brief 检查用户是否在本地
  bool IsUserLocal(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = local_sessions.find(user_id);
    return it != local_sessions.end() && !it->second.expired();
  }
};

/// @brief 消息存储（使用 Redis + MySQL）
struct DistributedMessageStore {
  std::shared_ptr<chirp::network::RedisClient> redis;
  std::string instance_id;

  std::string OfflineKey(const std::string& user_id) {
    return "chirp:chat:offline:" + user_id;
  }

  std::string HistoryKey(const std::string& channel_id) {
    return "chirp:chat:history:" + channel_id;
  }

  /// @brief 添加离线消息到 Redis
  void AddOffline(const std::string& receiver_id, const std::string& message) {
    if (!redis) {
      return;
    }
    redis->RPush(OfflineKey(receiver_id), message);
    redis->Expire(OfflineKey(receiver_id), 86400 * 7);  // 7天过期
  }

  /// @brief 获取并清空离线消息
  std::vector<std::string> PopOffline(const std::string& user_id) {
    if (!redis) {
      return {};
    }
    auto messages = redis->LRange(OfflineKey(user_id), 0, -1);
    redis->Del(OfflineKey(user_id));
    return messages;
  }

  /// @brief 保存消息到历史记录
  void AddToHistory(const std::string& channel_id, const std::string& message) {
    if (!redis) {
      return;
    }
    redis->RPush(HistoryKey(channel_id), message);
    redis->LTrim(HistoryKey(channel_id), -100, -1);  // 只保留最近100条
  }

  /// @brief 获取历史消息
  std::vector<std::string> GetHistory(const std::string& channel_id, int limit) {
    if (!redis) {
      return {};
    }
    return redis->LRange(HistoryKey(channel_id), -limit, -1);
  }
};

/// @brief 发送数据包到会话
void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

/// @brief 发送聊天通知
void SendChatNotify(const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::chat::ChatMessage& msg) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(msg.SerializeAsString());
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

/// @brief 处理私聊消息发送
void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                       const std::shared_ptr<chirp::network::Session>& sender_session,
                       const std::shared_ptr<DistributedChatState>& state,
                       const std::shared_ptr<DistributedMessageStore>& store,
                       const std::shared_ptr<chirp::network::MessageRouter>& router,
                       int64_t seq) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id(GenerateMessageId());
  msg.set_sender_id(req.sender_id());
  msg.set_receiver_id(req.receiver_id());
  msg.set_channel_type(chirp::chat::PRIVATE);
  msg.set_msg_type(req.msg_type());
  msg.set_content(req.content());
  msg.set_timestamp(NowMs());
  msg.set_channel_id(req.sender_id() < req.receiver_id()
                         ? req.sender_id() + "|" + req.receiver_id()
                         : req.receiver_id() + "|" + req.sender_id());

  // 保存到历史
  store->AddToHistory(msg.channel_id(), msg.SerializeAsString());

  // 响应发送者
  chirp::chat::SendMessageResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_message_id(msg.message_id());
  resp.set_server_timestamp(msg.timestamp());
  SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  // 智能路由消息到接收者
  router->SendChatMessage(req.receiver_id(), msg.SerializeAsString(),
    [&](const std::string& user_id) -> bool {
      // 尝试本地投递
      auto recv_session = state->GetLocalSession(user_id);
      if (recv_session) {
        SendChatNotify(recv_session, msg);
        return true;
      }
      return false;
    });

  // 如果接收者不在线，保存离线消息
  if (!state->IsUserLocal(req.receiver_id())) {
    store->AddOffline(req.receiver_id(), msg.SerializeAsString());
  }
}

/// @brief 处理用户登录
void HandleLogin(const chirp::auth::LoginRequest& req,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const std::shared_ptr<DistributedChatState>& state,
                 const std::shared_ptr<DistributedMessageStore>& store,
                 const std::shared_ptr<chirp::network::MessageRouter>& router,
                 int64_t seq) {
  const std::string user_id = req.token();

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(NowMs()));

    // 添加到本地会话
    state->AddSession(user_id, session);

    // 订阅用户的聊天消息频道
    router->SubscribeUserChat(user_id, [session, state](const std::string& msg_data) {
      auto s = session;  // 捕获 shared_ptr 保持连接
      if (s) {
        chirp::chat::ChatMessage msg;
        if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
          SendChatNotify(s, msg);
        }
      }
    });

    // 发送离线消息
    auto offline_msgs = store->PopOffline(user_id);
    for (const auto& msg_data : offline_msgs) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        SendChatNotify(session, msg);
      }
    }
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(NowMs());

  SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "127.0.0.1");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  std::string instance_id = GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = "chat_" + std::to_string(GetCurrentProcessId());
  }

  Logger::Instance().Info("chirp_chat_distributed starting instance=" + instance_id +
                          " tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          " redis=" + redis_host + ":" + std::to_string(redis_port));

  asio::io_context io;

  // 初始化组件
  auto state = std::make_shared<DistributedChatState>();
  state->instance_id = instance_id;

  auto redis_client = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  auto store = std::make_shared<DistributedMessageStore>();
  store->redis = redis_client;
  store->instance_id = instance_id;

  auto router = std::make_shared<chirp::network::MessageRouter>(io, redis_host, redis_port);
  if (!router->Start()) {
    Logger::Instance().Error("Failed to start message router");
    return 1;
  }

  // TCP 服务器
  chirp::network::TcpServer server(
      io, port,
      [&](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          Logger::Instance().Warn("Failed to parse packet");
          return;
        }

        switch (pkt.msg_id()) {
        case chirp::gateway::LOGIN_REQ: {
          chirp::auth::LoginRequest req;
          if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            HandleLogin(req, session, state, store, router, pkt.sequence());
          }
          break;
        }
        case chirp::gateway::SEND_MESSAGE_REQ: {
          chirp::chat::SendMessageRequest req;
          if (req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            HandleSendMessage(req, session, state, store, router, pkt.sequence());
          }
          break;
        }
        case chirp::gateway::HEARTBEAT_PING: {
          chirp::gateway::HeartbeatPong pong;
          pong.set_timestamp(NowMs());
          SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(),
                     pong.SerializeAsString());
          break;
        }
        default:
          break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        state->RemoveSession(session.get());
      });

  // WebSocket 服务器
  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [&](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        // 处理逻辑与 TCP 相同
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        state->RemoveSession(session.get());
      });

  server.Start();
  ws_server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code&, int) {
    Logger::Instance().Info("Shutting down...");
    server.Stop();
    ws_server.Stop();
    router->Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_chat_distributed exited");
  return 0;
}
```

## Docker Compose 集群部署

```yaml
# docker-compose.cluster.yml
version: '3.8'

services:
  # Redis Cluster
  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"
    command: redis-server --appendonly yes

  # MySQL 主库
  mysql-master:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: chirp_root
      MYSQL_DATABASE: chirp
      MYSQL_USER: chirp
      MYSQL_PASSWORD: chirp_pass
    ports:
      - "3306:3306"
    volumes:
      - mysql_master_data:/var/lib/mysql
      - ./scripts/init_db.sql:/docker-entrypoint-initdb.d/init_db.sql

  # 负载均衡器
  haproxy:
    image: haproxy:2.8-alpine
    ports:
      - "5000:5000"   # Gateway TCP
      - "5001:5001"   # Gateway WebSocket
      - "7000:7000"   # Chat TCP
      - "7001:7001"   # Chat WebSocket
    volumes:
      - ./haproxy.cfg:/usr/local/etc/haproxy/haproxy.cfg:ro
    depends_on:
      - gateway-1
      - gateway-2
      - gateway-3
      - chat-1
      - chat-2
      - chat-3

  # Gateway 实例 1
  gateway-1:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: gateway
    command:
      - "--port=5000"
      - "--ws_port=5001"
      - "--auth_host=auth-1"
      - "--auth_port=6000"
      - "--redis_host=redis"
      - "--redis_port=6379"
      - "--instance_id=gw_1"
    depends_on:
      - redis
      - auth-1
    networks:
      - chirp_net

  # Gateway 实例 2
  gateway-2:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: gateway
    command:
      - "--port=5000"
      - "--ws_port=5001"
      - "--auth_host=auth-2"
      - "--auth_port=6000"
      - "--redis_host=redis"
      - "--redis_port=6379"
      - "--instance_id=gw_2"
    depends_on:
      - redis
      - auth-2
    networks:
      - chirp_net

  # Gateway 实例 3
  gateway-3:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: gateway
    command:
      - "--port=5000"
      - "--ws_port=5001"
      - "--auth_host=auth-3"
      - "--auth_port=6000"
      - "--redis_host=redis"
      - "--redis_port=6379"
      - "--instance_id=gw_3"
    depends_on:
      - redis
      - auth-3
    networks:
      - chirp_net

  # Auth 实例
  auth-1:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: auth
    command: ["--port=6000", "--jwt_secret=prod_secret_change_me"]
    networks:
      - chirp_net

  auth-2:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: auth
    command: ["--port=6000", "--jwt_secret=prod_secret_change_me"]
    networks:
      - chirp_net

  auth-3:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: auth
    command: ["--port=6000", "--jwt_secret=prod_secret_change_me"]
    networks:
      - chirp_net

  # Chat 实例 1
  chat-1:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: chat
    command:
      - "--port=7000"
      - "--ws_port=7001"
      - "--redis_host=redis"
      - "--redis_port=6379"
      - "--mysql_host=mysql-master"
      - "--mysql_port=3306"
      - "--mysql_db=chirp"
      - "--mysql_user=chirp"
      - "--mysql_pass=chirp_pass"
      - "--instance_id=chat_1"
    depends_on:
      - redis
      - mysql-master
    networks:
      - chirp_net

  # Chat 实例 2
  chat-2:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: chat
    command:
      - "--port=7000"
      - "--ws_port=7001"
      - "--redis_host=redis"
      - "--redis_port=6379"
      - "--mysql_host=mysql-master"
      - "--mysql_port=3306"
      - "--mysql_db=chirp"
      - "--mysql_user=chirp"
      - "--mysql_pass=chirp_pass"
      - "--instance_id=chat_2"
    depends_on:
      - redis
      - mysql-master
    networks:
      - chirp_net

  # Chat 实例 3
  chat-3:
    build:
      context: .
      dockerfile: docker/Dockerfile.service
      args:
        SERVICE: chat
    command:
      - "--port=7000"
      - "--ws_port=7001"
      - "--redis_host=redis"
      - "--redis_port=6379"
      - "--mysql_host=mysql-master"
      - "--mysql_port=3306"
      - "--mysql_db=chirp"
      - "--mysql_user=chirp"
      - "--mysql_pass=chirp_pass"
      - "--instance_id=chat_3"
    depends_on:
      - redis
      - mysql-master
    networks:
      - chirp_net

networks:
  chirp_net:
    driver: bridge

volumes:
  mysql_master_data:
```

## HAProxy 配置

```
# haproxy.cfg
defaults
    timeout connect 5000ms
    timeout client  60m
    timeout server  60m

# Gateway TCP 负载均衡
listen gateway_tcp
    bind *:5000
    mode tcp
    balance leastconn
    option tcplog
    server gateway-1 gateway-1:5000 check
    server gateway-2 gateway-2:5000 check
    server gateway-3 gateway-3:5000 check

# Gateway WebSocket 负载均衡
listen gateway_ws
    bind *:5001
    mode tcp
    balance leastconn
    option tcplog
    server gateway-1 gateway-1:5001 check
    server gateway-2 gateway-2:5001 check
    server gateway-3 gateway-3:5001 check

# Chat TCP 负载均衡
listen chat_tcp
    bind *:7000
    mode tcp
    balance leastconn
    option tcplog
    server chat-1 chat-1:7000 check
    server chat-2 chat-2:7000 check
    server chat-3 chat-3:7000 check

# Chat WebSocket 负载均衡
listen chat_ws
    bind *:7001
    mode tcp
    balance leastconn
    option tcplog
    server chat-1 chat-1:7001 check
    server chat-2 chat-2:7001 check
    server chat-3 chat-3:7001 check
```

## 部署步骤

```bash
# 1. 构建镜像
docker compose -f docker-compose.cluster.yml build

# 2. 启动集群
docker compose -f docker-compose.cluster.yml up -d

# 3. 查看状态
docker compose -f docker-compose.cluster.yml ps

# 4. 查看日志
docker compose -f docker-compose.cluster.yml logs -f

# 5. 扩展服务（如需更多实例）
docker compose -f docker-compose.cluster.yml up -d --scale gateway=5 --scale chat=5
```
