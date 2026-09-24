#include "ChirpClientSubsystem.h"

// Native core headers — only this .cc sees them; the UCLASS header above
// stays Unreal-pure (UnrealBuildTool's parser must not meet asio/proto).
#include "chirp/sdk_client.h"
#include "chirp/chat_event_listener.h"

#include <unordered_map>

#include "Async/TaskGraphInterfaces.h"

using chirp::sdk::ChatClient;
using chirp::sdk::ChatConfig;
using chirp::sdk::ConnectionState;
using chirp::sdk::NotifyHandle;

namespace
{
EChirpConnectionState ToBpState(ConnectionState s)
{
    switch (s)
    {
    case ConnectionState::Connecting:
        return EChirpConnectionState::Connecting;
    case ConnectionState::Connected:
        return EChirpConnectionState::Connected;
    case ConnectionState::LoggedIn:
        return EChirpConnectionState::LoggedIn;
    case ConnectionState::WaitingReconnect:
        return EChirpConnectionState::WaitingReconnect;
    case ConnectionState::Kicked:
        return EChirpConnectionState::Kicked;
    case ConnectionState::Disconnected:
    default:
        return EChirpConnectionState::Disconnected;
    }
}

FString ToFString(const std::string& s)
{
    return FString(UTF8_TO_TCHAR(s.c_str()));
}

std::string ToStd(const FString& s)
{
    FTCHARToUTF8 utf8(*s);
    return std::string(utf8.Get(), utf8.Length());
}

chirp::chat::ChannelType ToChannelType(EChirpChannelType t)
{
    // Wire enum values match EChirpChannelType one-to-one (see the UENUM).
    return static_cast<chirp::chat::ChannelType>(static_cast<int>(t));
}

FChirpChatEnvelope ToEnvelope(const chirp::chat::ChatMessage& m)
{
    FChirpChatEnvelope env;
    env.MessageId = ToFString(m.message_id());
    env.SenderId = ToFString(m.sender_id());
    env.ReceiverId = ToFString(m.receiver_id());
    env.Channel = static_cast<EChirpChannelType>(static_cast<int>(m.channel_type()));
    env.ChannelId = ToFString(m.channel_id());
    env.MsgType = static_cast<int32>(m.msg_type());
    env.Content = ToFString(m.content());
    env.TimestampMs = m.timestamp();
    env.ReplyToMessageId = ToFString(m.reply_to_message_id());
    const std::string& meta = m.metadata();
    env.Metadata.Append(reinterpret_cast<const uint8*>(meta.data()),
                        static_cast<int32>(meta.size()));
    return env;
}
} // namespace

// Game-thread pump for the core ChatEventListener surface. The three events
// the subsystem already surfaces via dedicated callbacks (disconnect / kick /
// login result) stay no-ops here — one broadcast per event, never two.
class FNativeEventListener final : public chirp::sdk::ChatEventListener
{
public:
    explicit FNativeEventListener(UChirpClientSubsystem* InOwner)
        : Owner(InOwner)
    {
    }

    void OnReconnecting(int attempt, int delay_ms) override
    {
        AsyncTask(ENamedThreads::GameThread, [Owner, a = attempt, d = delay_ms] {
            if (Owner.IsValid())
            {
                Owner->OnReconnecting.Broadcast(a, d);
            }
        });
    }

    void OnReconnected() override
    {
        AsyncTask(ENamedThreads::GameThread, [Owner] {
            if (Owner.IsValid())
            {
                Owner->OnReconnected.Broadcast();
            }
        });
    }

    void OnMessageReceived(const chirp::chat::ChatMessage& msg) override
    {
        FChirpChatEnvelope env = ToEnvelope(msg);
        AsyncTask(ENamedThreads::GameThread, [Owner, env = MoveTemp(env)] {
            if (Owner.IsValid())
            {
                Owner->OnChatEnvelope.Broadcast(env);
            }
        });
    }

private:
    // Weak on purpose: the core listener table outlives neither Deinitialize
    // (which destroys the whole native client, listener included) nor the
    // subsystem itself, but a queued AsyncTask can still land after both.
    TWeakObjectPtr<UChirpClientSubsystem> Owner;
};

// Native lives one layer down so the header stays free of chirp includes.
class FNativeClient
{
public:
    ChatClient Client;

    // Raw-notify routing: MsgId -> native subscription handle. Written from
    // the game thread (WatchNotify), read on the io thread (dispatch) — the
    // client's subscription table is internally locked, this map only tracks
    // which ids we already subscribed to, so game-thread-only access with a
    // double-check inside the callback keeps it simple.
    TMap<int32, NotifyHandle> RawSubs;

    FNativeClient(const FString& Host, int32 Port, UChirpClientSubsystem* Owner)
        : Client([=] {
              ChatConfig config;
              config.gateway_host = ToStd(Host);
              config.gateway_port = static_cast<uint16_t>(Port);
              return config;
          }())
    {
        // The core table holds the shared_ptr; this listener's lifetime is
        // bounded by the client's dtor, which joins its io thread first.
        Client.AddListener(std::make_shared<FNativeEventListener>(Owner));
    }
};

void UChirpClientSubsystem::Deinitialize()
{
    if (Native)
    {
        // The native client's dtor joins its io thread; disconnect first so
        // no in-flight callback marshals onto a dying subsystem.
        Native->Client.Disconnect();
        delete Native;
        Native = nullptr;
    }
    Super::Deinitialize();
}

void UChirpClientSubsystem::Connect(const FString& Host, int32 Port)
{
    if (!Native)
    {
        Native = new FNativeClient(Host, Port, this);
        ChatClient& client = Native->Client;

        // ---- wire callbacks: io thread -> game thread ----
        client.SetDisconnectCallback([this](const std::error_code& ec) {
            AsyncTask(ENamedThreads::GameThread, [this, msg = FString(ec.message().c_str())] {
                RaiseDisconnected(msg);
            });
        });
        client.SetKickCallback([this](const std::string& reason) {
            AsyncTask(ENamedThreads::GameThread, [this, r = ToFString(reason)] {
                RaiseKicked(r);
            });
        });
        client.SetMessageCallback([this](const std::string& sender, const std::string& content) {
            AsyncTask(ENamedThreads::GameThread,
                      [this, s = ToFString(sender), c = ToFString(content)] {
                          RaiseChatMessage(s, c);
                      });
        });

        // Raw notify pump: subscriptions are per-msg-id handles; capture the
        // msg id along so RaiseNotify knows which route to broadcast.
        // (WatchNotify registers ids below; this lambda is the common body.)
    }

    Native->Client.Connect();
}

void UChirpClientSubsystem::Disconnect()
{
    if (Native)
    {
        Native->Client.Disconnect();
    }
}

void UChirpClientSubsystem::Login(const FString& Token)
{
    if (!Native)
    {
        RaiseLoginResult(FString());
        return;
    }

    Native->Client.Login(ToStd(Token), [this](const std::error_code& ec, const std::string& user_id) {
        AsyncTask(ENamedThreads::GameThread, [this, ok = !ec, uid = ToFString(user_id)] {
            RaiseLoginResult(ok ? uid : FString());
        });
    });
}

void UChirpClientSubsystem::Logout()
{
    if (Native)
    {
        Native->Client.Logout();
    }
}

void UChirpClientSubsystem::SendChatMessage(const FString& Receiver, const FString& Content)
{
    if (Native)
    {
        Native->Client.SendMessage(ToStd(Receiver), ToStd(Content));
    }
}

void UChirpClientSubsystem::SendChatMessageEx(const FChirpSendOptions& Options,
                                              const FString& Content)
{
    if (!Native)
    {
        // Same posture as SendChatMessage before Connect: silent no-op.
        return;
    }

    chirp::sdk::ChatClient::SendOptions opts;
    opts.channel_type = ToChannelType(Options.Channel);
    opts.channel_id = ToStd(Options.ChannelId);
    opts.receiver_id = ToStd(Options.ReceiverId);
    opts.reply_to_message_id = ToStd(Options.ReplyToMessageId);

    Native->Client.SendMessage(
        opts, ToStd(Content), [this](const std::error_code& ec,
                                     const chirp::chat::SendMessageResponse& resp) {
            // Local failures (not connected, timeout) report ServerCode -1;
            // server rounds report the wire ErrorCode (0 = ok).
            const bool ok = !ec && resp.code() == chirp::common::OK;
            const int32 code = ec ? -1 : static_cast<int32>(resp.code());
            AsyncTask(ENamedThreads::GameThread,
                      [this, ok, code, mid = ToFString(resp.message_id())] {
                          RaiseSendResult(ok, code, mid);
                      });
        });
}

TArray<FChirpChatEnvelope> UChirpClientSubsystem::LoadHistory(EChirpChannelType Type,
                                                              const FString& ChannelId,
                                                              int32 Limit,
                                                              int64 BeforeTimestampMs)
{
    TArray<FChirpChatEnvelope> Out;
    if (!Native)
    {
        return Out;
    }
    for (const chirp::chat::ChatMessage& msg :
         Native->Client.LoadHistory(ToChannelType(Type), ToStd(ChannelId), Limit,
                                    BeforeTimestampMs))
    {
        Out.Add(ToEnvelope(msg));
    }
    return Out;
}

int32 UChirpClientSubsystem::GetUnreadCount(EChirpChannelType Type, const FString& ChannelId)
{
    return Native ? Native->Client.GetUnreadCount(ToChannelType(Type), ToStd(ChannelId)) : 0;
}

void UChirpClientSubsystem::MarkRead(EChirpChannelType Type, const FString& ChannelId,
                                     const FString& MessageId)
{
    if (Native)
    {
        Native->Client.MarkRead(ToChannelType(Type), ToStd(ChannelId), ToStd(MessageId));
    }
}

void UChirpClientSubsystem::CleanupMessages(int64 OlderThanMs)
{
    if (Native)
    {
        Native->Client.CleanupMessages(OlderThanMs);
    }
}

chirp::sdk::ChatClient& UChirpClientSubsystem::NativeClient()
{
    if (!Native)
    {
        // Default config (localhost:5000) — Connect() can still be used
        // afterwards to point at another host; the config is fixed at
        // construction, so call NativeClient() first for hooks, or Connect()
        // first for a custom target.
        Native = new FNativeClient(TEXT("localhost"), 5000, this);
    }
    return Native->Client;
}

void UChirpClientSubsystem::WatchNotify(int32 MsgId)
{
    if (!Native)
    {
        return;
    }
    if (Native->RawSubs.Contains(MsgId))
    {
        return;
    }

    // Subscribing before Connect() is fine: the client queues subs until a
    // connection exists, and they survive reconnects.
    const auto handle = Native->Client.OnNotify(
        static_cast<uint32_t>(MsgId), [this, MsgId](const std::string& body) {
            TArray<uint8> bytes;
            const auto size = static_cast<int32>(body.size());
            bytes.Append(reinterpret_cast<const uint8*>(body.data()), size);
            AsyncTask(ENamedThreads::GameThread, [this, MsgId, bytes = MoveTemp(bytes)] {
                RaiseNotify(MsgId, MoveTemp(bytes));
            });
        });
    Native->RawSubs.Add(MsgId, handle);
}

EChirpConnectionState UChirpClientSubsystem::GetState() const
{
    return Native ? ToBpState(Native->Client.GetState()) : EChirpConnectionState::Disconnected;
}

bool UChirpClientSubsystem::IsLoggedIn() const
{
    return Native && Native->Client.GetState() == ConnectionState::LoggedIn;
}

// ---- game-thread raise helpers: subsystem events are BP delegates ----
void UChirpClientSubsystem::RaiseDisconnected(const FString& Reason) { OnDisconnected.Broadcast(Reason); }
void UChirpClientSubsystem::RaiseKicked(const FString& Reason) { OnKicked.Broadcast(Reason); }
void UChirpClientSubsystem::RaiseLoginResult(const FString& UserId) { OnLoginResult.Broadcast(UserId); }
void UChirpClientSubsystem::RaiseChatMessage(const FString& Sender, const FString& Content)
{
    OnChatMessage.Broadcast(Sender, Content);
}
void UChirpClientSubsystem::RaiseNotify(int32 MsgId, TArray<uint8> Body)
{
    OnRawNotify.Broadcast(MsgId, MoveTemp(Body));
}
void UChirpClientSubsystem::RaiseSendResult(bool bOk, int32 ServerCode, FString MessageId)
{
    OnSendResult.Broadcast(bOk, ServerCode, MoveTemp(MessageId));
}
