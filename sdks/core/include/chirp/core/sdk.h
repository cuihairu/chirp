#ifndef CHIRP_CORE_SDK_H_
#define CHIRP_CORE_SDK_H_

#include <memory>
#include <string>

#include "chirp/core/config.h"
#include "chirp/core/client.h"

namespace chirp {
namespace core {

// Main SDK entry point
class SDK {
public:
  // Initialize the SDK with configuration
  static bool Initialize(const Config& config);

  // Shutdown the SDK
  static void Shutdown();

  // Get the global client instance
  static Client* GetClient();

  // Check if SDK is initialized
  static bool IsInitialized();

private:
  SDK() = default;
  ~SDK() = default;

  static std::unique_ptr<Client> s_client;
  static bool s_initialized;
};

} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_SDK_H_
