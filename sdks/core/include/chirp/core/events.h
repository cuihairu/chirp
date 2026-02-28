#ifndef CHIRP_CORE_EVENTS_H_
#define CHIRP_CORE_EVENTS_H_

#include <functional>
#include <string>
#include <vector>

#include "chirp/core/modules/chat/chat_events.h"
#include "chirp/core/modules/social/social_events.h"
#include "chirp/core/modules/voice/voice_events.h"

namespace chirp {
namespace core {

// Event dispatcher for handling SDK events
class EventDispatcher {
public:
  template<typename EventType, typename Callback>
  void Subscribe(Callback&& callback) {
    // Implementation would use type erasure or specific event queues
  }

  template<typename EventType>
  void Publish(const EventType& event) {
    // Implementation would dispatch to all subscribers
  }

  void ProcessEvents();
  void Clear();
};

} // namespace core
} // namespace chirp

#endif // CHIRP_CORE_EVENTS_H_
