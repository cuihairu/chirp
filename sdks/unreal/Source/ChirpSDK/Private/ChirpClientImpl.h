#pragma once

#include "CoreMinimal.h"
#include "ChirpSDK/Public/ChirpBlueprintFunctionLibrary.h"

// Forward declare Chirp core SDK
namespace chirp {
namespace core {
class Client;
class Config;
enum class ConnectionState;
} // namespace core
} // namespace chirp

/**
 * Implementation class that wraps the Chirp C++ SDK
 * Uses PIMPL pattern to hide SDK details from Unreal headers
 */
class UChirpClientImpl
{
public:
	UChirpClientImpl();
	~UChirpClientImpl();

	bool Initialize(const FChirpConfig& Config);
	void Shutdown();

	bool Connect();
	void Disconnect();
	bool IsConnected() const;

	bool Login(const FString& UserId, const FString& Token, const FString& DeviceId);
	void Logout();
	FString GetUserId() const;

	// Chat
	bool SendTextMessage(const FString& ToUserId, const FString& Content);
	bool SendChannelMessage(const FString& ChannelId, EChirpChannelType ChannelType, const FString& Content);
	TArray<FChirpMessage> GetHistory(const FString& ChannelId, EChirpChannelType ChannelType, int32 Limit);
	bool MarkRead(const FString& ChannelId, const FString& MessageId);
	int32 GetUnreadCount(const FString& ChannelId);
	int32 GetTotalUnreadCount();

	// Social
	bool SendFriendRequest(const FString& UserId, const FString& Message);
	bool AcceptFriendRequest(const FString& RequestId);
	bool RemoveFriend(const FString& UserId);
	TArray<FChirpFriendInfo> GetFriendList();
	bool SetPresence(const FString& StatusText);

	// Voice
	bool JoinVoiceRoom(const FString& RoomId);
	bool LeaveVoiceRoom();
	void SetMicMuted(bool bMuted);
	void SetSpeakerMuted(bool bMuted);
	bool IsMicMuted() const;
	bool IsSpeakerMuted() const;

	// Event callback setters (for internal use)
	void SetMessageCallback(TFunction<void(const FChirpMessage&)> Callback);
	void SetConnectionCallback(TFunction<void(EChirpConnectionState)> Callback);

private:
	chirp::core::Client* Client = nullptr;
	bool bIsInitialized = false;

	// Convert between Unreal and Chirp types
	static chirp::core::Config ConvertConfig(const FChirpConfig& Config);
	static FString StdStringToFString(const std::string& Str);
	static std::string FStringToStdString(const FString& Str);

	// Message conversion
	static FChirpMessage ConvertMessage(const struct ChirpMessageData& MsgData);
};
