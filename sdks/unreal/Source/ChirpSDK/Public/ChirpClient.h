#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ChirpClient.generated.h"

class UChirpClientImpl;

// Forward declarations for delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChirpConnectionStateChanged, EChirpConnectionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChirpMessageReceived, const FChirpMessage&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChirpUnreadCountChanged, const FString&, ChannelId, int32, NewCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChirpFriendRequestReceived, const FString&, FromUserId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChirpVoiceRoomParticipantJoined, const FString&, RoomId, const FString&, UserId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChirpVoiceRoomParticipantLeft, const FString&, RoomId, const FString&, UserId);

#include "ChirpSDK/Public/ChirpBlueprintFunctionLibrary.h"

/**
 * Main Chirp SDK Client for Unreal Engine
 * Handles all communication with the Chirp backend
 */
UCLASS(BlueprintType, ClassGroup = "Chirp")
class CHIRPSDK_API UChirpClient : public UObject
{
	GENERATED_BODY()

public:
	UChirpClient();

	virtual void BeginDestroy() override;

	// ============================================================================
	// Event Delegates (Blueprint-callable)
	// ============================================================================

	/** Called when connection state changes */
	UPROPERTY(BlueprintAssignable, Category = "Chirp|Events")
	FOnChirpConnectionStateChanged OnConnectionStateChanged;

	/** Called when a chat message is received */
	UPROPERTY(BlueprintAssignable, Category = "Chirp|Events|Chat")
	FOnChirpMessageReceived OnMessageReceived;

	/** Called when unread count changes */
	UPROPERTY(BlueprintAssignable, Category = "Chirp|Events|Chat")
	FOnChirpUnreadCountChanged OnUnreadCountChanged;

	/** Called when a friend request is received */
	UPROPERTY(BlueprintAssignable, Category = "Chirp|Events|Social")
	FOnChirpFriendRequestReceived OnFriendRequestReceived;

	/** Called when a participant joins a voice room */
	UPROPERTY(BlueprintAssignable, Category = "Chirp|Events|Voice")
	FOnChirpVoiceRoomParticipantJoined OnVoiceRoomParticipantJoined;

	/** Called when a participant leaves a voice room */
	UPROPERTY(BlueprintAssignable, Category = "Chirp|Events|Voice")
	FOnChirpVoiceRoomParticipantLeft OnVoiceRoomParticipantLeft;

	// ============================================================================
	// Core Functions
	// ============================================================================

	/**
	 * Initialize the client with configuration
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	bool Initialize(const FChirpConfig& Config);

	/**
	 * Shutdown the client
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	void Shutdown();

	/**
	 * Connect to server
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	bool Connect();

	/**
	 * Disconnect from server
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	void Disconnect();

	/**
	 * Check connection status
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Core")
	bool IsConnected() const;

	/**
	 * Login with credentials
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	bool Login(const FString& UserId, const FString& Token, const FString& DeviceId = TEXT(""));

	/**
	 * Logout
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	void Logout();

	/**
	 * Get current user ID
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Core")
	FString GetUserId() const;

	// ============================================================================
	// Chat Functions
	// ============================================================================

	/**
	 * Send a text message
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	bool SendTextMessage(const FString& ToUserId, const FString& Content);

	/**
	 * Send channel message
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	bool SendChannelMessage(const FString& ChannelId, EChirpChannelType ChannelType, const FString& Content);

	/**
	 * Get message history
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	TArray<FChirpMessage> GetHistory(const FString& ChannelId, EChirpChannelType ChannelType, int32 Limit = 50);

	/**
	 * Mark messages as read
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	bool MarkRead(const FString& ChannelId, const FString& MessageId);

	/**
	 * Get unread count for channel
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Chat")
	int32 GetUnreadCount(const FString& ChannelId);

	/**
	 * Get total unread count
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Chat")
	int32 GetTotalUnreadCount();

	// ============================================================================
	// Social Functions
	// ============================================================================

	/**
	 * Send friend request
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	bool SendFriendRequest(const FString& UserId, const FString& Message = TEXT(""));

	/**
	 * Accept friend request
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	bool AcceptFriendRequest(const FString& RequestId);

	/**
	 * Remove friend
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	bool RemoveFriend(const FString& UserId);

	/**
	 * Get friend list
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	TArray<FChirpFriendInfo> GetFriendList();

	/**
	 * Set presence
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	bool SetPresence(const FString& StatusText);

	// ============================================================================
	// Voice Functions
	// ============================================================================

	/**
	 * Join voice room
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	bool JoinVoiceRoom(const FString& RoomId);

	/**
	 * Leave voice room
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	bool LeaveVoiceRoom();

	/**
	 * Set mic muted
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	void SetMicMuted(bool bMuted);

	/**
	 * Set speaker muted
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	void SetSpeakerMuted(bool bMuted);

	/**
	 * Check if mic is muted
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Voice")
	bool IsMicMuted() const;

	/**
	 * Check if speaker is muted
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Voice")
	bool IsSpeakerMuted() const;

private:
	// Implementation pointer (PIMPL pattern)
	UChirpClientImpl* Impl;
};
