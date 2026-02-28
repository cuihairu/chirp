#include "chirp/core/sdk.h"
#include "client_impl.h"

namespace chirp {
namespace core {

std::unique_ptr<Client> SDK::s_client;
bool SDK::s_initialized = false;

bool SDK::Initialize(const Config& config) {
  if (s_initialized) {
    return true;
  }

  s_client = Client::Create(config);
  s_initialized = (s_client != nullptr);

  return s_initialized;
}

void SDK::Shutdown() {
  if (!s_initialized) {
    return;
  }

  if (s_client) {
    s_client->Disconnect();
    s_client.reset();
  }

  s_initialized = false;
}

Client* SDK::GetClient() {
  return s_client.get();
}

bool SDK::IsInitialized() {
  return s_initialized;
}

} // namespace core
} // namespace chirp
