#include "ChirpClientSubsystem.h"

// Native core headers — only this .cc sees them; the UCLASS header above
// stays Unreal-pure (UnrealBuildTool's parser must not meet asio/proto).
#include "chirp/sdk_client.h"

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
} // namespace

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

    FNativeClient(const FString& Host, int32 Port)
        : Client([=] {
              ChatConfig config;
              config.gateway_host = ToStd(Host);
              config.gateway_port = static_cast<uint16_t>(Port);
              return config;
          }())
    {
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
        Native = new FNativeClient(Host, Port);
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
