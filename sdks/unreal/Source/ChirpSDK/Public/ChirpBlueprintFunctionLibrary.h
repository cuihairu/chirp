#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ChirpSDK.generated.h"

class UChirpClient;

UENUM(BlueprintType)
enum class EChirpConnectionState : uint8
{
	Disconnected	UMETA(DisplayName = "Disconnected"),
	Connecting		UMETA(DisplayName = "Connecting"),
	Connected		UMETA(DisplayName = "Connected"),
	Reconnecting	UMETA(DisplayName = "Reconnecting")
};

UENUM(BlueprintType)
enum class EChirpChannelType : uint8
{
	Private		UMETA(DisplayName = "Private (1v1)"),
	Team		UMETA(DisplayName = "Team"),
	Guild		UMETA(DisplayName = "Guild"),
	World		UMETA(DisplayName = "World")
};

UENUM(BlueprintType)
enum class EChirpMessageType : uint8
{
	Text		UMETA(DisplayName = "Text"),
	Emoji		UMETA(DisplayName = "Emoji"),
	Voice		UMETA(DisplayName = "Voice"),
	Image		UMETA(DisplayName = "Image"),
	System		UMETA(DisplayName = "System")
};

USTRUCT(BlueprintType)
struct FChirpConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	FString GatewayHost = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	int32 GatewayPort = 7000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	FString AppId = TEXT("chirp_unreal");
};

USTRUCT(BlueprintType)
struct FChirpMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	FString MessageId;

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	FString SenderId;

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	FString ReceiverId;

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	FString ChannelId;

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	EChirpChannelType ChannelType;

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	EChirpMessageType MessageType;

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	FString Content;

	UPROPERTY(BlueprintReadOnly, Category = "Message")
	int64 Timestamp;
};

USTRUCT(BlueprintType)
struct FChirpFriendInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Friend")
	FString UserId;

	UPROPERTY(BlueprintReadOnly, Category = "Friend")
	FString Username;

	UPROPERTY(BlueprintReadOnly, Category = "Friend")
	FString AvatarUrl;

	UPROPERTY(BlueprintReadOnly, Category = "Friend")
	bool bIsOnline;

	UPROPERTY(BlueprintReadOnly, Category = "Friend")
	FString StatusText;
};

USTRUCT(BlueprintType)
struct FChirpVoiceRoomInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	FString RoomId;

	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	FString RoomName;

	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	int32 ParticipantCount;

	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	bool bIsActive;
};

// Declaration for dynamic multicast delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChirpConnectionStateChanged, EChirpConnectionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChirpMessageReceived, const FChirpMessage&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChirpUnreadCountChanged, const FString&, ChannelId, int32, NewCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChirpFriendRequestReceived, const FString&, FromUserId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChirpVoiceRoomParticipantJoined, const FString&, RoomId, const FString&, UserId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChirpVoiceRoomParticipantLeft, const FString&, RoomId, const FString&, UserId);

/**
 * Blueprint Function Library for Chirp SDK
 * Provides static functions for Blueprint access
 */
UCLASS()
class CHIRPSDK_API UChirpBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ============================================================================
	// Core SDK Functions
	// ============================================================================

	/**
	 * Initialize the Chirp SDK with configuration
	 * @param Config SDK configuration
	 * @return True if initialization succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	static bool InitializeSDK(const FChirpConfig& Config);

	/**
	 * Shutdown the Chirp SDK
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	static void ShutdownSDK();

	/**
	 * Connect to the Chirp server
	 * @return True if connection initiated successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	static bool Connect();

	/**
	 * Disconnect from the Chirp server
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	static void Disconnect();

	/**
	 * Check if connected to the server
	 * @return True if connected
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Core")
	static bool IsConnected();

	/**
	 * Login with user credentials
	 * @param UserId User identifier
	 * @param Token Authentication token
	 * @param DeviceId Optional device identifier
	 * @return True if login succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	static bool Login(const FString& UserId, const FString& Token, const FString& DeviceId = TEXT(""));

	/**
	 * Logout from current session
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Core")
	static void Logout();

	/**
	 * Get current user ID
	 * @return Current user ID
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Core")
	static FString GetUserId();

	// ============================================================================
	// Chat Functions
	// ============================================================================

	/**
	 * Send a text message
	 * @param ToUserId Recipient user ID
	 * @param Content Message content
	 * @return True if message sent successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	static bool SendTextMessage(const FString& ToUserId, const FString& Content);

	/**
	 * Send a message to a channel
	 * @param ChannelId Channel ID
	 * @param ChannelType Channel type
	 * @param Content Message content
	 * @return True if message sent successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	static bool SendChannelMessage(const FString& ChannelId, EChirpChannelType ChannelType, const FString& Content);

	/**
	 * Get chat history
	 * @param ChannelId Channel ID
	 * @param ChannelType Channel type
	 * @param Limit Maximum number of messages
	 * @return Array of chat messages
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	static TArray<FChirpMessage> GetHistory(const FString& ChannelId, EChirpChannelType ChannelType, int32 Limit = 50);

	/**
	 * Mark messages as read
	 * @param ChannelId Channel ID
	 * @param MessageId Last read message ID
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Chat")
	static bool MarkRead(const FString& ChannelId, const FString& MessageId);

	/**
	 * Get unread message count for a channel
	 * @param ChannelId Channel ID
	 * @return Unread count
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Chat")
	static int32 GetUnreadCount(const FString& ChannelId);

	/**
	 * Get total unread message count
	 * @return Total unread count
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Chat")
	static int32 GetTotalUnreadCount();

	// ============================================================================
	// Social Functions
	// ============================================================================

	/**
	 * Send a friend request
	 * @param UserId User to send request to
	 * @param Message Optional message
	 * @return True if request sent successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	static bool SendFriendRequest(const FString& UserId, const FString& Message = TEXT(""));

	/**
	 * Accept a friend request
	 * @param RequestId Request ID to accept
	 * @return True if accepted successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	static bool AcceptFriendRequest(const FString& RequestId);

	/**
	 * Remove a friend
	 * @param UserId Friend user ID to remove
	 * @return True if removed successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	static bool RemoveFriend(const FString& UserId);

	/**
	 * Get friend list
	 * @return Array of friend info
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	static TArray<FChirpFriendInfo> GetFriendList();

	/**
	 * Get pending friend requests
	 * @return Array of user IDs with pending requests
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	static TArray<FString> GetPendingFriendRequests();

	/**
	 * Set presence status
	 * @param StatusText Status message (e.g., "In Game", "Away")
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Social")
	static bool SetPresence(const FString& StatusText);

	// ============================================================================
	// Voice Functions
	// ============================================================================

	/**
	 * Join a voice room
	 * @param RoomId Room ID to join
	 * @return True if joined successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	static bool JoinVoiceRoom(const FString& RoomId);

	/**
	 * Leave current voice room
	 * @return True if left successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	static bool LeaveVoiceRoom();

	/**
	 * Set microphone muted state
	 * @param bMuted Whether to mute microphone
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	static void SetMicMuted(bool bMuted);

	/**
	 * Set speaker muted state
	 * @param bMuted Whether to mute speaker
	 */
	UFUNCTION(BlueprintCallable, Category = "Chirp|Voice")
	static void SetSpeakerMuted(bool bMuted);

	/**
	 * Check if microphone is muted
	 * @return True if mic is muted
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Voice")
	static bool IsMicMuted();

	/**
	 * Check if speaker is muted
	 * @return True if speaker is muted
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Voice")
	static bool IsSpeakerMuted();

	/**
	 * Get current voice room info
	 * @return Current room info
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Voice")
	static FChirpVoiceRoomInfo GetCurrentVoiceRoom();

	// ============================================================================
	// Event Binders (for Blueprint event subsystems)
	// ============================================================================

	/**
	 * Get the client instance for event binding
	 * @return Pointer to Chirp client
	 */
	UFUNCTION(BlueprintPure, Category = "Chirp|Core")
	static UChirpClient* GetClient();
};
