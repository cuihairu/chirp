// Chirp Unreal SDK — game-thread facade over chirp::sdk::ChatClient.
//
// All events below are raised ON THE GAME THREAD (the native client's
// callbacks arrive on its own io thread and are marshalled here with
// AsyncTask), so Blueprint handlers can touch UWorld/UWidget freely.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChirpClientSubsystem.generated.h"

// The UCLASS header stays Unreal-pure (UBT's parser must not meet
// asio/proto headers): only the .cpp includes the native core.
namespace chirp::sdk
{
class ChatClient;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChirpDisconnectedEvent, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChirpKickedEvent, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChirpLoginResultEvent, const FString&, UserId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChirpChatMessageEvent, const FString&, Sender,
                                             const FString&, Content);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChirpNotifyEvent, int32, MsgId,
                                             const TArray<uint8>&, Body);

// Native client state, mirrored for Blueprint polling.
UENUM(BlueprintType)
enum class EChirpConnectionState : uint8
{
    Disconnected,
    Connecting,
    Connected,
    LoggedIn,
    WaitingReconnect,
    Kicked
};

// Channel kinds — numeric values mirror chirp::chat::ChannelType on the wire.
UENUM(BlueprintType)
enum class EChirpChannelType : uint8
{
    Private,
    Team,
    Guild,
    World,
    SystemChannel,
    Marquee
};

/** One chat message rendered for Blueprint; mirrors chirp::chat::ChatMessage.
 * Emitted on OnChatEnvelope after interceptor rewriting (same semantics as the
 * C#/C++ listener OnMessageReceived). Metadata carries arbitrary bytes. */
USTRUCT(BlueprintType)
struct FChirpChatEnvelope
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    FString MessageId;

    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    FString SenderId;

    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    FString ReceiverId;

    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    EChirpChannelType Channel = EChirpChannelType::Private;

    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    FString ChannelId;

    /** Wire MsgType enum value (0 = text). */
    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    int32 MsgType = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    FString Content;

    /** Server-assigned epoch ms. */
    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    int64 TimestampMs = 0;

    /** Message this one replies to; empty = not a reply. */
    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    FString ReplyToMessageId;

    UPROPERTY(BlueprintReadWrite, Category = "Chirp")
    TArray<uint8> Metadata;
};

/** Send parameters for SendChatMessageEx. Private requires ReceiverId (the
 * wire channel id is derived by sorting the two user ids); every other
 * channel requires an explicit ChannelId. */
USTRUCT(BlueprintType)
struct FChirpSendOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chirp")
    EChirpChannelType Channel = EChirpChannelType::Private;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chirp")
    FString ChannelId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chirp")
    FString ReceiverId;

    /** Message id being replied to; empty = not a reply. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chirp")
    FString ReplyToMessageId;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChirpReconnectingEvent, int32, Attempt,
                                             int32, DelayMs);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FChirpReconnectedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FChirpSendResultEvent, bool, bOk, int32, ServerCode,
                                               const FString&, MessageId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChirpChatEnvelopeEvent, const FChirpChatEnvelope&,
                                            Envelope);

/**
 * GameInstance subsystem wrapping one chirp::sdk::ChatClient. Get it from any
 * Blueprint via "Get Chirp Client", call Connect/Login, and bind the On*
 * events. The heavy lifting (framing, heartbeats, backoff reconnect, kick
 * handling) lives in the native core — this class only marshals onto the
 * game thread and exposes Blueprint types. There is deliberately no
 * OnConnected event: poll GetState() or rely on OnLoginResult.
 */
UCLASS()
class CHIRPSDK_API UChirpClientSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** Transport dropped (auto-reconnect will follow unless Disconnect was called). */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpDisconnectedEvent OnDisconnected;

    /** Another login took over the session; terminal until Connect again. */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpKickedEvent OnKicked;

    /** Login round-trip finished; UserId is empty on failure. */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpLoginResultEvent OnLoginResult;

    /** Live chat push (sender + UTF-8 content). */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpChatMessageEvent OnChatMessage;

    /** Raw notify for advanced use (presence, party, ...): MsgId plus the
     * protobuf payload bytes. Bind it after calling WatchNotify(MsgId). */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpNotifyEvent OnRawNotify;

    /** Backoff reconnect starting: Attempt (1-based) and DelayMs. */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpReconnectingEvent OnReconnecting;

    /** A reconnect attempt reached Connected again. */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpReconnectedEvent OnReconnected;

    /** SendChatMessageEx finished: ServerCode is the wire ErrorCode (0 = ok),
     * or -1 for a local failure (not connected / timed out). */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpSendResultEvent OnSendResult;

    /** Full chat envelope push (after interceptor rewriting, replies included).
     * Overlaps OnChatMessage — that one keeps the legacy sender/content form. */
    UPROPERTY(BlueprintAssignable, Category = "Chirp")
    FChirpChatEnvelopeEvent OnChatEnvelope;

    virtual void Deinitialize() override;

    /** Opens the TCP connection to the chat plane (5000 in the dev topology). */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void Connect(const FString& Host, int32 Port);

    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void Disconnect();

    /** LOGIN with a token (scaffold mode: token == user id). Result arrives
     * on OnLoginResult. */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void Login(const FString& Token);

    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void Logout();

    /** Fire-and-forget private text message. */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void SendChatMessage(const FString& Receiver, const FString& Content);

    /** Full-pipeline send: command routing ('/' prefix), interceptors and the
     * local archive all apply (mirrors ChirpClient.SendMessageAsync on
     * Unity/.NET). Private requires ReceiverId; other channels require
     * ChannelId. Blocked locally? OnSendResult still fires, with ServerCode
     * -1 and an empty MessageId. Reply support via ReplyToMessageId. */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void SendChatMessageEx(const FChirpSendOptions& Options, const FString& Content);

    /** Newest-first slice of the local archive (empty when no store is set —
     * see the core README for the C++ hook used to install one). */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    TArray<FChirpChatEnvelope> LoadHistory(EChirpChannelType Type, const FString& ChannelId,
                                           int32 Limit = 20, int64 BeforeTimestampMs = 0);

    /** Unread counter from the local archive store (0 without one). */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    int32 GetUnreadCount(EChirpChannelType Type, const FString& ChannelId);

    /** Forwards into the archive store; no-op without one. */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void MarkRead(EChirpChannelType Type, const FString& ChannelId, const FString& MessageId);

    /** Drops archive entries older than the epoch-ms cutoff. */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void CleanupMessages(int64 OlderThanMs);

    /** Starts forwarding server notifies with this MsgId onto OnRawNotify
     * (one native subscription per MsgId, created lazily). */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void WatchNotify(int32 MsgId);

    UFUNCTION(BlueprintPure, Category = "Chirp")
    EChirpConnectionState GetState() const;

    UFUNCTION(BlueprintPure, Category = "Chirp")
    bool IsLoggedIn() const;

    /** Direct access to the native core for C++ games: install hooks here
     * (SetMessageInterceptor / SetAuthProvider / SetMessageStore / AddListener
     * / RegisterCommand) exactly as the core README shows. Hooks run on the
     * core's io thread — marshal back with AsyncTask if you touch UObject.
     * Lazily creates the client with default config (localhost:5000). */
    chirp::sdk::ChatClient& NativeClient();

private:
    class FNativeClient* Native = nullptr;

    void RaiseDisconnected(const FString& Reason);
    void RaiseKicked(const FString& Reason);
    void RaiseLoginResult(const FString& UserId);
    void RaiseChatMessage(const FString& Sender, const FString& Content);
    void RaiseNotify(int32 MsgId, TArray<uint8> Body);
    void RaiseSendResult(bool bOk, int32 ServerCode, FString MessageId);
};
