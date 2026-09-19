// Chirp Unreal SDK — game-thread facade over chirp::sdk::ChatClient.
//
// All events below are raised ON THE GAME THREAD (the native client's
// callbacks arrive on its own io thread and are marshalled here with
// AsyncTask), so Blueprint handlers can touch UWorld/UWidget freely.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChirpClientSubsystem.generated.h"

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

    /** Starts forwarding server notifies with this MsgId onto OnRawNotify
     * (one native subscription per MsgId, created lazily). */
    UFUNCTION(BlueprintCallable, Category = "Chirp")
    void WatchNotify(int32 MsgId);

    UFUNCTION(BlueprintPure, Category = "Chirp")
    EChirpConnectionState GetState() const;

    UFUNCTION(BlueprintPure, Category = "Chirp")
    bool IsLoggedIn() const;

private:
    class FNativeClient* Native = nullptr;

    void RaiseDisconnected(const FString& Reason);
    void RaiseKicked(const FString& Reason);
    void RaiseLoginResult(const FString& UserId);
    void RaiseChatMessage(const FString& Sender, const FString& Content);
    void RaiseNotify(int32 MsgId, TArray<uint8> Body);
};
