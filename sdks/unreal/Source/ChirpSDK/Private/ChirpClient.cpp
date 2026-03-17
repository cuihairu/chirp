#include "ChirpClient.h"
#include "ChirpClientImpl.h"
#include "ChirpBlueprintFunctionLibrary.h"

UChirpClient::UChirpClient()
	: Impl(nullptr)
{
}

void UChirpClient::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

bool UChirpClient::Initialize(const FChirpConfig& Config)
{
	if (Impl)
	{
		return true;  // Already initialized
	}

	Impl = new UChirpClientImpl();
	return Impl->Initialize(Config);
}

void UChirpClient::Shutdown()
{
	if (Impl)
	{
		Impl->Shutdown();
		delete Impl;
		Impl = nullptr;
	}
}

bool UChirpClient::Connect()
{
	return Impl ? Impl->Connect() : false;
}

void UChirpClient::Disconnect()
{
	if (Impl) Impl->Disconnect();
}

bool UChirpClient::IsConnected() const
{
	return Impl ? Impl->IsConnected() : false;
}

bool UChirpClient::Login(const FString& UserId, const FString& Token, const FString& DeviceId)
{
	return Impl ? Impl->Login(UserId, Token, DeviceId) : false;
}

void UChirpClient::Logout()
{
	if (Impl) Impl->Logout();
}

FString UChirpClient::GetUserId() const
{
	return Impl ? Impl->GetUserId() : FString();
}

// ============================================================================
// Chat Functions
// ============================================================================

bool UChirpClient::SendTextMessage(const FString& ToUserId, const FString& Content)
{
	return Impl ? Impl->SendTextMessage(ToUserId, Content) : false;
}

bool UChirpClient::SendChannelMessage(const FString& ChannelId, EChirpChannelType ChannelType, const FString& Content)
{
	return Impl ? Impl->SendChannelMessage(ChannelId, ChannelType, Content) : false;
}

TArray<FChirpMessage> UChirpClient::GetHistory(const FString& ChannelId, EChirpChannelType ChannelType, int32 Limit)
{
	return Impl ? Impl->GetHistory(ChannelId, ChannelType, Limit) : TArray<FChirpMessage>();
}

bool UChirpClient::MarkRead(const FString& ChannelId, const FString& MessageId)
{
	return Impl ? Impl->MarkRead(ChannelId, MessageId) : false;
}

int32 UChirpClient::GetUnreadCount(const FString& ChannelId)
{
	return Impl ? Impl->GetUnreadCount(ChannelId) : 0;
}

int32 UChirpClient::GetTotalUnreadCount()
{
	return Impl ? Impl->GetTotalUnreadCount() : 0;
}

// ============================================================================
// Social Functions
// ============================================================================

bool UChirpClient::SendFriendRequest(const FString& UserId, const FString& Message)
{
	return Impl ? Impl->SendFriendRequest(UserId, Message) : false;
}

bool UChirpClient::AcceptFriendRequest(const FString& RequestId)
{
	return Impl ? Impl->AcceptFriendRequest(RequestId) : false;
}

bool UChirpClient::RemoveFriend(const FString& UserId)
{
	return Impl ? Impl->RemoveFriend(UserId) : false;
}

TArray<FChirpFriendInfo> UChirpClient::GetFriendList()
{
	return Impl ? Impl->GetFriendList() : TArray<FChirpFriendInfo>();
}

bool UChirpClient::SetPresence(const FString& StatusText)
{
	return Impl ? Impl->SetPresence(StatusText) : false;
}

// ============================================================================
// Voice Functions
// ============================================================================

bool UChirpClient::JoinVoiceRoom(const FString& RoomId)
{
	return Impl ? Impl->JoinVoiceRoom(RoomId) : false;
}

bool UChirpClient::LeaveVoiceRoom()
{
	return Impl ? Impl->LeaveVoiceRoom() : false;
}

void UChirpClient::SetMicMuted(bool bMuted)
{
	if (Impl) Impl->SetMicMuted(bMuted);
}

void UChirpClient::SetSpeakerMuted(bool bMuted)
{
	if (Impl) Impl->SetSpeakerMuted(bMuted);
}

bool UChirpClient::IsMicMuted() const
{
	return Impl ? Impl->IsMicMuted() : true;
}

bool UChirpClient::IsSpeakerMuted() const
{
	return Impl ? Impl->IsSpeakerMuted() : true;
}

// ============================================================================
// Blueprint Function Library Implementation
// ============================================================================

UChirpClient* g_GlobalChirpClient = nullptr;

UChirpClient* UChirpBlueprintFunctionLibrary::GetClient()
{
	if (!g_GlobalChirpClient)
	{
		g_GlobalChirpClient = NewObject<UChirpClient>();
		g_GlobalChirpClient->AddToRoot();
	}
	return g_GlobalChirpClient;
}

bool UChirpBlueprintFunctionLibrary::InitializeSDK(const FChirpConfig& Config)
{
	return GetClient()->Initialize(Config);
}

void UChirpBlueprintFunctionLibrary::ShutdownSDK()
{
	if (g_GlobalChirpClient)
	{
		g_GlobalChirpClient->Shutdown();
		g_GlobalChirpClient->RemoveFromRoot();
		g_GlobalChirpClient = nullptr;
	}
}

bool UChirpBlueprintFunctionLibrary::Connect()
{
	return GetClient()->Connect();
}

void UChirpBlueprintFunctionLibrary::Disconnect()
{
	GetClient()->Disconnect();
}

bool UChirpBlueprintFunctionLibrary::IsConnected()
{
	return GetClient()->IsConnected();
}

bool UChirpBlueprintFunctionLibrary::Login(const FString& UserId, const FString& Token, const FString& DeviceId)
{
	return GetClient()->Login(UserId, Token, DeviceId);
}

void UChirpBlueprintFunctionLibrary::Logout()
{
	GetClient()->Logout();
}

FString UChirpBlueprintFunctionLibrary::GetUserId()
{
	return GetClient()->GetUserId();
}

bool UChirpBlueprintFunctionLibrary::SendTextMessage(const FString& ToUserId, const FString& Content)
{
	return GetClient()->SendTextMessage(ToUserId, Content);
}

bool UChirpBlueprintFunctionLibrary::SendChannelMessage(const FString& ChannelId, EChirpChannelType ChannelType, const FString& Content)
{
	return GetClient()->SendChannelMessage(ChannelId, ChannelType, Content);
}

TArray<FChirpMessage> UChirpBlueprintFunctionLibrary::GetHistory(const FString& ChannelId, EChirpChannelType ChannelType, int32 Limit)
{
	return GetClient()->GetHistory(ChannelId, ChannelType, Limit);
}

bool UChirpBlueprintFunctionLibrary::MarkRead(const FString& ChannelId, const FString& MessageId)
{
	return GetClient()->MarkRead(ChannelId, MessageId);
}

int32 UChirpBlueprintFunctionLibrary::GetUnreadCount(const FString& ChannelId)
{
	return GetClient()->GetUnreadCount(ChannelId);
}

int32 UChirpBlueprintFunctionLibrary::GetTotalUnreadCount()
{
	return GetClient()->GetTotalUnreadCount();
}

bool UChirpBlueprintFunctionLibrary::SendFriendRequest(const FString& UserId, const FString& Message)
{
	return GetClient()->SendFriendRequest(UserId, Message);
}

bool UChirpBlueprintFunctionLibrary::AcceptFriendRequest(const FString& RequestId)
{
	return GetClient()->AcceptFriendRequest(RequestId);
}

bool UChirpBlueprintFunctionLibrary::RemoveFriend(const FString& UserId)
{
	return GetClient()->RemoveFriend(UserId);
}

TArray<FChirpFriendInfo> UChirpBlueprintFunctionLibrary::GetFriendList()
{
	return GetClient()->GetFriendList();
}

bool UChirpBlueprintFunctionLibrary::SetPresence(const FString& StatusText)
{
	return GetClient()->SetPresence(StatusText);
}

bool UChirpBlueprintFunctionLibrary::JoinVoiceRoom(const FString& RoomId)
{
	return GetClient()->JoinVoiceRoom(RoomId);
}

bool UChirpBlueprintFunctionLibrary::LeaveVoiceRoom()
{
	return GetClient()->LeaveVoiceRoom();
}

void UChirpBlueprintFunctionLibrary::SetMicMuted(bool bMuted)
{
	GetClient()->SetMicMuted(bMuted);
}

void UChirpBlueprintFunctionLibrary::SetSpeakerMuted(bool bMuted)
{
	GetClient()->SetSpeakerMuted(bMuted);
}

bool UChirpBlueprintFunctionLibrary::IsMicMuted()
{
	return GetClient()->IsMicMuted();
}

bool UChirpBlueprintFunctionLibrary::IsSpeakerMuted()
{
	return GetClient()->IsSpeakerMuted();
}

FChirpVoiceRoomInfo UChirpBlueprintFunctionLibrary::GetCurrentVoiceRoom()
{
	return FChirpVoiceRoomInfo();  // TODO: Implement
}
