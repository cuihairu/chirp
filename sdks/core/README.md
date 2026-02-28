# Chirp Core SDK

The Chirp Core SDK is a C++ client library that provides a unified interface for all Chirp backend services.

## Features

- **Chat Module**: Real-time messaging, group chat, offline messages, read receipts
- **Social Module**: Friend management, presence, blacklist
- **Voice Module**: WebRTC-based voice chat with room management

## Platform Support

- Windows (MSVC 2019+)
- macOS (Xcode 12+)
- Linux (GCC 9+, Clang 10+)
- iOS (Xcode 12+)
- Android (NDK 21+)

## Building

```bash
cd sdks/core
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage Example

```cpp
#include "chirp/core/sdk.h"

using namespace chirp::core;

int main() {
  // Initialize SDK
  Config config;
  config.app_id = "my_game";
  config.connection.gateway_host = "localhost";
  config.connection.gateway_port = 5000;
  config.user_id = "player1";

  if (!SDK::Initialize(config)) {
    std::cerr << "Failed to initialize SDK" << std::endl;
    return 1;
  }

  // Get client
  auto* client = SDK::GetClient();

  // Connect
  if (!client->Connect()) {
    std::cerr << "Failed to connect" << std::endl;
    return 1;
  }

  // Login
  client->Login("player1");

  // Use Chat module
  auto* chat = client->GetChatModule();
  chat->SendMessage("player2", MessageType::TEXT, "Hello!",
    [](SendResult result, const std::string& msg_id) {
      if (result == SendResult::SUCCESS) {
        std::cout << "Message sent: " << msg_id << std::endl;
      }
    });

  // Use Social module
  auto* social = client->GetSocialModule();
  social->SetPresence(PresenceStatus::ONLINE, "Playing game");

  // Use Voice module
  auto* voice = client->GetVoiceModule();
  voice->CreateRoom(RoomType::GROUP, "My Room", 10,
    [](bool success, const std::string& room_id) {
      if (success) {
        std::cout << "Created room: " << room_id << std::endl;
      }
    });

  // Cleanup
  std::this_thread::sleep_for(std::chrono::seconds(10));
  SDK::Shutdown();

  return 0;
}
```

## Module Documentation

See the module-specific documentation:
- [Chat Module](modules/chat/README.md)
- [Social Module](modules/social/README.md)
- [Voice Module](modules/voice/README.md)
