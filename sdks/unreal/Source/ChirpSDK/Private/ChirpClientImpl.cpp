#include "ChirpClientImpl.h"

#include "chirp/core/sdk.h"
#include "chirp/core/client.h"
#include "chirp/core/config.h"
#include "chirp/core/modules/chat/chat_module.h"

// Define Chirp message data structure if not available
struct ChirpMessageData {
	std::string message_id;
	std::string sender_id;
	std::string receiver_id;
	std::string channel_id;
	int channel_type;
	std::string content;
	int64_t timestamp;
};

UChirpClientImpl::UChirpClientImpl()
{
}

UChirpClientImpl::~UChirpClientImpl()
{
	Shutdown();
}

bool UChirpClientImpl::Initialize(const FChirpConfig& Config)
{
	if (bIsInitialized)
	{
		return true;
	}

	chirp::core::Config ChirpConfig = ConvertConfig(Config);

	if (!chirp::core::SDK::Initialize(ChirpConfig))
	{
		return false;
	}

	Client = chirp::core::SDK::GetClient();
	if (!Client)
	{
		return false;
	}

	bIsInitialized = true;

	// Set up connection state callback
	Client->SetConnectionStateCallback([this](chirp::core::ConnectionState State, const std::string& Reason)
	{
		// Convert and notify - implementation depends on having access to outer class
		// For now, this is a simplified version
	});

	return true;
}

void UChirpClientImpl::Shutdown()
{
	if (bIsInitialized)
	{
		chirp::core::SDK::Shutdown();
		Client = nullptr;
		bIsInitialized = false;
	}
}

bool UChirpClientImpl::Connect()
{
	if (!Client)
	{
		return false;
	}

	return Client->Connect();
}

void UChirpClientImpl::Disconnect()
{
	if (Client)
	{
		Client->Disconnect();
	}
}

bool UChirpClientImpl::IsConnected() const
{
	return Client ? Client->IsConnected() : false;
}

bool UChirpClientImpl::Login(const FString& UserId, const FString& Token, const FString& DeviceId)
{
	if (!Client)
	{
		return false;
	}

	return Client->Login(FStringToStdString(UserId), FStringToStdString(Token));
}

void UChirpClientImpl::Logout()
{
	if (Client)
	{
		Client->Logout();
	}
}

FString UChirpClientImpl::GetUserId() const
{
	if (!Client)
	{
		return FString();
	}

	return StdStringToFString(Client->GetUserId());
}

// ============================================================================
// Chat Implementation
// ============================================================================

bool UChirpClientImpl::SendTextMessage(const FString& ToUserId, const FString& Content)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* ChatModule = Client->GetChatModule();
	if (!ChatModule)
	{
		return false;
	}

	return ChatModule->SendTextMessage(FStringToStdString(ToUserId), FStringToStdString(Content));
}

bool UChirpClientImpl::SendChannelMessage(const FString& ChannelId, EChirpChannelType ChannelType, const FString& Content)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* ChatModule = Client->GetChatModule();
	if (!ChatModule)
	{
		return false;
	}

	return ChatModule->SendChannelMessage(
		FStringToStdString(ChannelId),
		static_cast<chirp::core::ChannelType>(ChannelType),
		FStringToStdString(Content)
	);
}

TArray<FChirpMessage> UChirpClientImpl::GetHistory(const FString& ChannelId, EChirpChannelType ChannelType, int32 Limit)
{
	TArray<FChirpMessage> Result;

	if (!Client || !Client->IsConnected())
	{
		return Result;
	}

	auto* ChatModule = Client->GetChatModule();
	if (!ChatModule)
	{
		return Result;
	}

	auto Messages = ChatModule->GetHistory(
		FStringToStdString(ChannelId),
		static_cast<chirp::core::ChannelType>(ChannelType),
		Limit
	);

	for (const auto& Msg : Messages)
	{
		FChirpMessage ConvertedMsg;
		ConvertedMsg.MessageId = StdStringToFString(Msg.message_id);
		ConvertedMsg.SenderId = StdStringToFString(Msg.sender_id);
		ConvertedMsg.ReceiverId = StdStringToFString(Msg.receiver_id);
		ConvertedMsg.ChannelId = StdStringToFString(Msg.channel_id);
		ConvertedMsg.Content = StdStringToFString(Msg.content);
		ConvertedMsg.Timestamp = Msg.timestamp;
		Result.Add(ConvertedMsg);
	}

	return Result;
}

bool UChirpClientImpl::MarkRead(const FString& ChannelId, const FString& MessageId)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* ChatModule = Client->GetChatModule();
	if (!ChatModule)
	{
		return false;
	}

	return ChatModule->MarkRead(FStringToStdString(ChannelId), FStringToStdString(MessageId));
}

int32 UChirpClientImpl::GetUnreadCount(const FString& ChannelId)
{
	if (!Client || !Client->IsConnected())
	{
		return 0;
	}

	auto* ChatModule = Client->GetChatModule();
	if (!ChatModule)
	{
		return 0;
	}

	return ChatModule->GetUnreadCount(FStringToStdString(ChannelId));
}

int32 UChirpClientImpl::GetTotalUnreadCount()
{
	if (!Client || !Client->IsConnected())
	{
		return 0;
	}

	auto* ChatModule = Client->GetChatModule();
	if (!ChatModule)
	{
		return 0;
	}

	return ChatModule->GetTotalUnreadCount();
}

// ============================================================================
// Social Implementation
// ============================================================================

bool UChirpClientImpl::SendFriendRequest(const FString& UserId, const FString& Message)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* SocialModule = Client->GetSocialModule();
	if (!SocialModule)
	{
		return false;
	}

	return SocialModule->SendFriendRequest(FStringToStdString(UserId), FStringToStdString(Message));
}

bool UChirpClientImpl::AcceptFriendRequest(const FString& RequestId)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* SocialModule = Client->GetSocialModule();
	if (!SocialModule)
	{
		return false;
	}

	return SocialModule->AcceptFriendRequest(FStringToStdString(RequestId));
}

bool UChirpClientImpl::RemoveFriend(const FString& UserId)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* SocialModule = Client->GetSocialModule();
	if (!SocialModule)
	{
		return false;
	}

	return SocialModule->RemoveFriend(FStringToStdString(UserId));
}

TArray<FChirpFriendInfo> UChirpClientImpl::GetFriendList()
{
	TArray<FChirpFriendInfo> Result;

	if (!Client || !Client->IsConnected())
	{
		return Result;
	}

	auto* SocialModule = Client->GetSocialModule();
	if (!SocialModule)
	{
		return Result;
	}

	auto Friends = SocialModule->GetFriendList();
	for (const auto& Friend : Friends)
	{
		FChirpFriendInfo Info;
		Info.UserId = StdStringToFString(Friend.user_id);
		Info.Username = StdStringToFString(Friend.username);
		Info.AvatarUrl = StdStringToFString(Friend.avatar_url);
		Info.bIsOnline = Friend.is_online;
		Info.StatusText = StdStringToFString(Friend.status_text);
		Result.Add(Info);
	}

	return Result;
}

bool UChirpClientImpl::SetPresence(const FString& StatusText)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* SocialModule = Client->GetSocialModule();
	if (!SocialModule)
	{
		return false;
	}

	return SocialModule->SetPresence(FStringToStdString(StatusText));
}

// ============================================================================
// Voice Implementation
// ============================================================================

bool UChirpClientImpl::JoinVoiceRoom(const FString& RoomId)
{
	if (!Client || !Client->IsConnected())
	{
		return false;
	}

	auto* VoiceModule = Client->GetVoiceModule();
	if (!VoiceModule)
	{
		return false;
	}

	return VoiceModule->JoinRoom(FStringToStdString(RoomId));
}

bool UChirpClientImpl::LeaveVoiceRoom()
{
	if (!Client)
	{
		return false;
	}

	auto* VoiceModule = Client->GetVoiceModule();
	if (!VoiceModule)
	{
		return false;
	}

	return VoiceModule->LeaveRoom();
}

void UChirpClientImpl::SetMicMuted(bool bMuted)
{
	if (!Client)
	{
		return;
	}

	auto* VoiceModule = Client->GetVoiceModule();
	if (!VoiceModule)
	{
		return;
	}

	VoiceModule->SetMicMuted(bMuted);
}

void UChirpClientImpl::SetSpeakerMuted(bool bMuted)
{
	if (!Client)
	{
		return;
	}

	auto* VoiceModule = Client->GetVoiceModule();
	if (!VoiceModule)
	{
		return;
	}

	VoiceModule->SetSpeakerMuted(bMuted);
}

bool UChirpClientImpl::IsMicMuted() const
{
	if (!Client)
	{
		return true;
	}

	auto* VoiceModule = Client->GetVoiceModule();
	if (!VoiceModule)
	{
		return true;
	}

	return VoiceModule->IsMicMuted();
}

bool UChirpClientImpl::IsSpeakerMuted() const
{
	if (!Client)
	{
		return true;
	}

	auto* VoiceModule = Client->GetVoiceModule();
	if (!VoiceModule)
	{
		return true;
	}

	return VoiceModule->IsSpeakerMuted();
}

// ============================================================================
// Private Helpers
// ============================================================================

chirp::core::Config UChirpClientImpl::ConvertConfig(const FChirpConfig& Config)
{
	chirp::core::Config ChirpConfig;
	ChirpConfig.gateway_host = FStringToStdString(Config.GatewayHost);
	ChirpConfig.gateway_port = Config.GatewayPort;
	ChirpConfig.app_id = FStringToStdString(Config.AppId);
	return ChirpConfig;
}

FString UChirpClientImpl::StdStringToFString(const std::string& Str)
{
	return FString(UTF8_TO_TCHAR(Str.c_str()));
}

std::string UChirpClientImpl::FStringToStdString(const FString& Str)
{
	return std::string(TCHAR_TO_UTF8(*Str));
}

FChirpMessage UChirpClientImpl::ConvertMessage(const ChirpMessageData& MsgData)
{
	FChirpMessage Msg;
	Msg.MessageId = StdStringToFString(MsgData.message_id);
	Msg.SenderId = StdStringToFString(MsgData.sender_id);
	Msg.ReceiverId = StdStringToFString(MsgData.receiver_id);
	Msg.ChannelId = StdStringToFString(MsgData.channel_id);
	Msg.Content = StdStringToFString(MsgData.content);
	Msg.Timestamp = MsgData.timestamp;
	return Msg;
}
