// Internal tests for AuthClient: private members are reached through the
// befriended access tag declared in the header; the implementation is
// linked in (no second compilation context).
#include <gtest/gtest.h>

#include <asio.hpp>

#include <mutex>
#include <string>

#include "network/auth_client.h"

namespace {

TEST(AuthClientInternalTest, DestructorWithoutImplReturnsEarly) {
  EXPECT_NO_THROW({
    asio::io_context io;
    chirp::gateway::AuthClient client(io, "127.0.0.1", 1);
    // Stop/join the worker and drop the pimpl (implemented against the
    // complete Impl type in auth_client.cc): the destructor must then take
    // its empty-pimpl early return.
    chirp::gateway::DrainAndDropAuthClientForTest(client);
    // A second drain hits the empty-pimpl guard inside the hook itself.
    chirp::gateway::DrainAndDropAuthClientForTest(client);
  });
  // destruction at scope exit covers the destructor guard
}

}  // namespace
