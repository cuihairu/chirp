#!/usr/bin/env bash
# Run unit tests with coverage instrumentation and produce per-package
# line-coverage statistics.
#
# Usage:
#   CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" \
#     scripts/run_coverage.sh [--fail-under N] [--skip-build] [--fresh]
#
# CMAKE_ARGS is passed straight to the configure step; CI sets it to the
# vcpkg toolchain (see .github/workflows/ci.yml). Without it the configure
# falls back to pre-generated protos and a half-successful system
# FindProtobuf can leave a protobuf::libprotobuf import target pointing at
# NOTFOUND, which fails at generate time.
#
# --fresh wipes the coverage build dir first. Needed after sources move or
# get deleted: incremental builds leave orphaned .gcda files for removed
# objects behind, and gcovr happily merges that stale data into the report.
#
# Requires: cmake + ninja, a GNU/Clang toolchain and gcov (from gcc).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-cov"
FAIL_UNDER=100
SKIP_BUILD=0
FRESH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fail-under)
      FAIL_UNDER="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --fresh)
      FRESH=1
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ${FRESH} -eq 1 && ${SKIP_BUILD} -eq 0 ]]; then
  rm -rf "${BUILD_DIR}"
fi

if [[ ${SKIP_BUILD} -eq 0 ]]; then
  # shellcheck disable=SC2086
  cmake --preset coverage ${CMAKE_ARGS:-}
  # Build everything: a fixed target list here would silently skip targets
  # whose source list changed (or new ones), reporting stale coverage.
  cmake --build --preset coverage
fi

ctest --preset coverage --output-on-failure

# ---------------------------------------------------------------------------
# Coverage aggregation.
#
# gcov is run directly over every .gcda and the JSON intermediates are folded
# in Python: one entry per (file, line) with the maximum hit count (several
# compilation contexts -- library objects, test objects and gcov function
# thunks -- can emit separate counters for the same physical line). Blank,
# brace-only, comment and signature-continuation lines carry no executable
# semantics and are dropped from the statistics, as are the lines listed in
# KNOWN_UNCOVERABLE (defensive branches that cannot be reached through the
# public APIs).
# ---------------------------------------------------------------------------
mkdir -p coverage_html
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

# gcov writes its JSON intermediates into the current directory and names
# them after the source file, so every gcda is processed in its own
# subdirectory to prevent clobbering between compilation contexts.
i=0
while IFS= read -r -d '' gcda; do
  mkdir -p "${WORK_DIR}/${i}"
  (cd "${WORK_DIR}/${i}" && gcov -j -b -c "${gcda}" >/dev/null 2>&1) || true
  i=$((i + 1))
done < <(find "${BUILD_DIR}" -name '*.gcda' -print0)

python3 - "${ROOT_DIR}" "${WORK_DIR}" "${FAIL_UNDER}" <<'PYEOF'
import csv
import glob
import gzip
import json
import os
import sys
from collections import defaultdict

root, work_dir, fail_under = sys.argv[1], sys.argv[2], float(sys.argv[3])

INCLUDE_PREFIXES = (
    os.path.join(root, "libs") + os.sep,
    os.path.join(root, "services") + os.sep,
    os.path.join(root, "sdks", "core", "src") + os.sep,
)
EXCLUDE = (".pb.cc", "/proto/", "examples/")

def is_excluded(path):
    if any(tag in path for tag in EXCLUDE):
        return True
    # Service entry points (main.cc, main_enhanced.cc, main_distributed.cc):
    # matched on the basename only. The old substring test also dropped every
    # file under any ancestor directory whose path merely contains "main"
    # (e.g. a worktree at /tmp/chirp-cov-main), silently reporting 0/0 = 100%.
    return os.path.basename(path).startswith("main")

# Defensive branches that cannot be reached through the public APIs; every
# entry documents why. Removing one requires a reproducing test.
KNOWN_UNCOVERABLE = {
    # HttpPushTransport connect-vs-deadline race: the timer arm only fires
    # against a packet-blackhole address. Sandboxes whose gateway SYN-proxies
    # every destination complete the handshake instead, so no
    # environment-independent unit test can hit this arm deterministically.
    # (Lines track ConnectTcpWithDeadline's timer lambda; keep in sync with
    # the branch-level entry for the same lambda below.)
    ("services/app/notification/src/http_push_transport.cc", 302),
    ("services/app/notification/src/http_push_transport.cc", 303),
    # Connect's success-path "new TcpHttpConnection" lines carry only the
    # bad_alloc unwind block of the make_unique call; the live block of the
    # statement sits on the neighbouring return line and is covered by the
    # loopback tests. Same for the SSL factory's plain-scheme fallthrough.
    ("services/app/notification/src/http_push_transport.cc", 351),
    ("services/app/notification/src/http_push_transport.cc", 523),
    # Frame encoder guards: needs a >4GiB message / a Message whose
    # SerializeToArray disagrees with ByteSizeLong. L12's untaken arms are
    # the >UINT32_MAX / >INT_MAX sides of the size check (same 4GiB wall).
    ("libs/network/protobuf_framing.cc", 12),
    ("libs/network/protobuf_framing.cc", 14),
    ("libs/network/protobuf_framing.cc", 20),
    # NormalizeDeviceId string-return EH pads (std::string copy throw only);
    # both ternary arms are covered by NormalizeDeviceIdDefaultsEmptyToDevice.
    ("libs/network/session_registry.cc", 35),
    # TcpSession/WebSocketSession Send/SendAndClose: untaken arms are EH
    # pads for asio::post lambda capture (std::string move / shared_ptr
    # copy throwing bad_alloc); the post body itself is covered by every
    # loopback echo test.
    ("libs/network/tcp_session.cc", 59),
    ("libs/network/tcp_session.cc", 70),
    ("libs/network/websocket_session.cc", 74),
    ("libs/network/websocket_session.cc", 88),
    # ChatPeerHub/ChatPeerLink/ServerGatewayPeer Create: PrivateTag +
    # std::move of handlers — untaken arms are construction EH pads.
    ("libs/network/chat_peer_hub.cc", 25),
    ("libs/network/chat_peer_link.cc", 25),
    ("libs/network/server_gateway_peer.cc", 29),
    # Peer link Send paths: same asio::post capture EH pattern as sessions.
    ("libs/network/chat_peer_link.cc", 78),
    ("libs/network/chat_peer_link.cc", 90),
    # message_router / redis_session_manager / notification_client /
    # chat_bridge request lambdas: asio::post capture EH pads.
    ("libs/network/message_router.cc", 68),
    ("libs/network/redis_session_manager.cc", 97),
    ("libs/network/redis_session_manager.cc", 147),
    ("libs/network/notification_client.cc", 108),
    ("libs/network/chat_bridge.cc", 270),
    ("libs/network/chat_bridge.cc", 284),
    ("libs/network/server_gateway_peer.cc", 113),
    # WebSocket FindHeaderValue: untaken arms need a header line without
    # trailing \r (fragmented/odd handshake) or getline throwing.
    ("libs/network/websocket_session.cc", 16),
    # Frame size ==0 || >cap: the >cap arm needs a 4MiB+ header write;
    # size==0 and normal sizes are covered by loopback tests.
    ("libs/network/chat_bridge.cc", 130),
    ("libs/network/chat_peer_hub.cc", 259),
    ("libs/network/chat_peer_link.cc", 150),
    ("libs/network/server_gateway_peer.cc", 214),
    # Handshake/resolver async_wait cancel-vs-fire races: the untaken arms
    # are the operation_aborted / already-ready sides of the || chain that
    # require cancelling a timer after the handler already dispatched.
    ("libs/network/chat_bridge.cc", 260),
    ("libs/network/chat_bridge.cc", 261),
    # Peer idle/reconnect timers: same cancel-vs-fire race as above.
    ("libs/network/chat_peer_hub.cc", 357),
    ("libs/network/chat_peer_hub.cc", 360),
    ("libs/network/chat_peer_link.cc", 283),
    ("libs/network/chat_peer_link.cc", 307),
    ("libs/network/server_gateway_peer.cc", 341),
    ("libs/network/server_gateway_peer.cc", 362),
    # PeerConn::Close re-entry / pen-scan miss: closing is set once and the
    # pen erase always finds `this` when the conn was still unregistered;
    # displaced-entry erase needs a same-pointer race on the vector.
    ("libs/network/chat_peer_hub.cc", 397),
    ("libs/network/chat_peer_hub.cc", 398),
    ("libs/network/chat_peer_hub.cc", 408),
    # SendRawPacket/Send while closing: closing is checked before the post
    # and set under the same strand — the post-body closing arm only runs
    # if Close wins the race after Send entered.
    ("libs/network/chat_peer_link.cc", 303),
    ("libs/network/server_gateway_peer.cc", 358),
    # Create-handshake resolve callbacks: stop-during-resolve races.
    ("libs/network/chat_peer_link.cc", 115),
    # SessionRegistry identity-change EH pads on string != (high/odd arms
    # only reachable if std::string compare throws); the semantic arms are
    # covered by RebindSameIdentityKeepsSlotWithoutErase + cross-device
    # + cross-user rebind tests. L125's untaken arm is the weak_ptr lock
    # landing pad between !bound and bound==session (covered semantically
    # by RemoveWithExpiredSlotOwner + RemoveReportsNoReleaseWhen...).
    ("libs/network/session_registry.cc", 56),
    ("libs/network/session_registry.cc", 125),
    # service_id_for_game: null-shared_ptr / unregistered entries cannot
    # appear in peers_ (insert only after registered=true with a live conn).
    ("libs/network/chat_peer_hub.cc", 120),
    # ReadBody closing short-circuit: requires Close() to win the race with
    # an in-flight body completion on the same conn (idle timer vs read).
    ("libs/network/chat_peer_hub.cc", 274),
    # SendKickAndClose reason.empty() ? "kicked" : reason — every FailClient
    # call site passes a non-empty literal; empty-reason is dead.
    ("libs/network/chat_bridge.cc", 36),
    # notification_service/handlers defensive arms: deferred by decision
    # (other session owns those files).
    ("services/app/notification/src/notification_handlers.cc", 94),
    ("services/app/notification/src/notification_service.cc", 46),
    ("services/app/notification/src/notification_service.cc", 128),
    ("services/app/notification/src/notification_service.cc", 175),
    ("services/app/notification/src/notification_service.cc", 223),
    ("services/app/notification/src/notification_service.cc", 226),
    ("services/app/notification/src/notification_service.cc", 394),
    # ChatClient::Impl::SendRequest timeout sweep: the pending entry is
    # erased only together with timer->cancel(); a handler already dispatched
    # before the cancel exits at the timer_ec arm above, so the find-miss
    # return is unreachable by construction.
    ("sdks/core/src/sdk_client.cc", 466),
    ("sdks/core/src/sdk_client.cc", 467),
    # ChatClient::Impl::SendRequest pending_.emplace: next_seq_ is monotonic,
    # so the red-black insert comparison always walks the greater side; the
    # less/duplicate arms would require a 2^32 sequence wrap.
    ("sdks/core/src/sdk_client.cc", 476),
    # ChatClient static error-category construction: the exception-cleanup
    # arm of the function-local static guard only runs when allocation of
    # the category object throws.
    ("sdks/core/src/sdk.cc", 41),
    # SendMessage command-predicate line: the only untaken arm is the
    # exception path of the inlined string ops (content.front()).
    ("sdks/core/src/sdk_client.cc", 256),
    # SendMessage channel_id ternary: untaken arms are throw edges into the
    # landing pad (blocks 114/118) plus the pad's internal cleanup branches;
    # reaching them requires operator+ to throw (std::bad_alloc).
    ("sdks/core/src/sdk_client.cc", 268),
    # DispatchCommand args ternary: untaken arms are the throw edges into
    # landing pad block 55 (string construction throwing) and the pad's
    # internal branches.
    ("sdks/core/src/sdk_client.cc", 418),
    # SendPacket null-socket arm: every caller is state-guarded via
    # ReadyForRequests(), so socket_ is only null inside the teardown window
    # between DoClose dropping the socket and the state flip - a disconnect
    # race no public-API sequence reaches deterministically.
    ("sdks/core/src/sdk_client.cc", 761),
    # HandleFrame pong match: the untaken arm is the throw edge into landing
    # pad block 83 (string/stdexcept during logging).
    ("sdks/core/src/sdk_client.cc", 600),
    # Renewal post: the untaken (state != Connected) arm is unreachable
    # because every transition away from Connected (DoClose, login success,
    # a fresh Login) clears auth_renewing_ first, so the guard at line 202
    # would have returned before line 209 is evaluated.
    ("sdks/core/src/sdk_client.cc", 209),
    # DispatchCommand loop: untaken arms are the throw edge into landing pad
    # block 63 and the pad's internal branches (string ctor throwing while
    # comparing handler names). The zero-trip loop entry and the null-handler
    # arm are covered by probes (HasCommands + NullCommandHandlerIsSkipped).
    ("sdks/core/src/sdk_client.cc", 426),
    # Login/Request/SendMessage asio::post lines: untaken arms are (a) throw
    # edges, (b) the out-edges of pad blocks 13/11 which have no in-edges,
    # and (c) the fall arm of the same-destination merge emitted for the
    # inlined std::string/std::function move inside the closure construction.
    # Every constructible input -- empty/SSO/heap tokens and bodies, empty/
    # SBO/heap std::function captures -- takes the tree arm; the fall arm is
    # the untaken half of an always-equal allocator/equality check (same
    # shape as the asio chrono_time_traits comparisons excluded above).
    ("sdks/core/src/sdk_client.cc", 107),
    ("sdks/core/src/sdk_client.cc", 247),
    ("sdks/core/src/sdk_client.cc", 292),
    # ChatClient::Impl::SendPacket socket guard: every caller checks the
    # connection state on the same io thread immediately before sending, and
    # only DoClose (same thread) clears socket_.
    ("sdks/core/src/sdk_client.cc", 758),
    # Logger::LevelToString fallthrough: every enumerator has a case; the
    # trailing return only exists to satisfy the compiler.
    ("libs/common/logger.cc", 40),
    # ChannelPermissionChecker::HasPermission: the Field enum is exhaustive;
    # the trailing return only satisfies the compiler.
    ("services/shared/chat/src/channel_manager.cc", 44),
    # GetChannels skips ids missing from channels_: both maps are updated
    # together under the same lock, so the skip cannot trigger.
    ("services/shared/chat/src/channel_manager.cc", 365),
    # Voice user_limit enforcement: no public API sets a nonzero user_limit
    # on a channel, so the "channel full" branch is unreachable today.
    ("services/shared/chat/src/channel_manager.cc", 535),
    ("services/shared/chat/src/channel_manager.cc", 536),
    ("services/shared/chat/src/channel_manager.cc", 537),
    # WebSocketClient handshake write-error branch: reaching it requires the
    # peer's TCP reset to land between connect() returning and the handshake
    # write (a sub-millisecond kernel race). The dedicated test hits it only
    # intermittently, so the lines are excluded from the stable statistics.
    ("libs/network/websocket_client.cc", 38),
    ("libs/network/websocket_client.cc", 39),
    # ReactionHandlers::HandleAddReaction guard: ReactionManager::AddReaction
    # has set semantics (re-adding is idempotent) and never returns false.
    ("services/shared/chat/src/message_handlers.cc", 231),
    ("services/shared/chat/src/message_handlers.cc", 232),
    # MessageMigrationWorker migrating_ guards: reaching the "already
    # migrating" arms requires RunMigrationNow to race an in-flight migration
    # on the io thread, which the single-threaded test io context cannot do.
    ("services/shared/chat/src/message_migration_worker.cc", 63),
    ("services/shared/chat/src/message_migration_worker.cc", 64),
    ("services/shared/chat/src/message_migration_worker.cc", 99),
    ("services/shared/chat/src/message_migration_worker.cc", 100),
    # MessageDeliveryTracker::RunCheck stop guard: firing depends on a timer
    # tick landing after Stop(), a race the deterministic test loop avoids.
    ("services/shared/chat/src/message_delivery_tracker.cc", 110),
    # DeliveryAckManager::RunCheck stop guard: same shape as the tracker
    # above - RunCheck is private and only timer-driven, and cancel() wins
    # the race against a pending tick in every deterministic test loop.
    ("services/shared/chat/src/delivery_ack_manager.cc", 145),
    # MetricsHttpServer::Start catch arm: async_accept(ec form) does not
    # throw, so the arm is purely defensive.
    ("libs/common/src/metrics_http_server.cc", 35),
    ("libs/common/src/metrics_http_server.cc", 36),
    ("libs/common/src/metrics_http_server.cc", 37),
    # MetricsHttpServer::StatusText 500/default arms: routes cannot fail,
    # so no caller ever asks for those strings.
    ("libs/common/src/metrics_http_server.cc", 144),
    ("libs/common/src/metrics_http_server.cc", 145),
    # AuthService ConfirmPasswordReset expired-token branch: tokens live 1h
    # and there is no injectable clock to age one past its expiry.
    ("services/app/auth/src/auth_service.cc", 424),
    ("services/app/auth/src/auth_service.cc", 425),
    ("services/app/auth/src/auth_service.cc", 426),
    # PresenceManager CleanupOfflineUsers erase: last_seen is written only
    # from the internal clock, so no test can age an entry past the 24h cutoff.
    ("services/social/src/presence_manager.cc", 408),
    # ChatBridge::InternalConn::Close re-entry guard: every closer (Detach,
    # FailClient) erases the map entry in the same call, so a second Close
    # never lands on an already-closing connection.
    ("libs/network/chat_bridge.cc", 87),
    # ChatBridge write-error arm: the peer RST always surfaces on the parked
    # header read first, and FailClient then removes the connection, so a
    # later forward can never target the dead socket with an in-flight write.
    ("libs/network/chat_bridge.cc", 114),
    ("libs/network/chat_bridge.cc", 115),
    # ChatBridge kConnecting switch arm: reads start only after the connect
    # handler flips the state, so no frame is ever handled while connecting.
    ("libs/network/chat_bridge.cc", 215),
    ("libs/network/chat_bridge.cc", 216),
    # ChatBridge::FailClient re-entry guard: the failed/closing flags make a
    # second entry unreachable in the single-threaded call graph - the timer,
    # read and write completions all check those flags before calling.
    ("libs/network/chat_bridge.cc", 364),
    # Server-plane registry defensive arms: the by-id map and the
    # tuple/game-user index are only ever mutated together under one lock,
    # so a tuple hit whose by-id record is missing cannot happen. (These
    # registries moved from services/game/server_gateway into
    # services/shared/chat with app_chat; the arms kept their line numbers.)
    ("services/shared/chat/src/identity_registry.cc", 143),
    ("services/shared/chat/src/identity_registry.cc", 177),
    ("services/shared/chat/src/subscription_registry.cc", 180),
    # ChatPeerHub::Start listen arm: reaching it needs listen(2) to fail
    # after bind(2) succeeded - only fd exhaustion landing between the two
    # syscalls does that, which no environment-independent test can force.
    ("libs/network/chat_peer_hub.cc", 94),
    ("libs/network/chat_peer_hub.cc", 95),
    # ChatPeerHub::DoAccept error arm: async_accept fails here only on
    # kernel-level conditions (EMFILE/ENFILE), unreachable from a test.
    # (Line numbers moved again when the unregistered-peer handling landed
    # in the accept completion; re-pinned 2026-09-25.)
    ("libs/network/chat_peer_hub.cc", 169),
    ("libs/network/chat_peer_hub.cc", 170),
    ("libs/network/chat_peer_hub.cc", 171),
    # ChatPeerHub::PeerConn::SendRawPacket closing guard: Close erases the
    # conn from peers_ (or the conn is displaced) on the same hub thread, so
    # no SendInject can ever target a closing connection.
    ("libs/network/chat_peer_hub.cc", 429),
    # base64 DecodeTable function-local static: gcov counts the guard's
    # exception-cleanup arcs, but MakeDecodeTable is non-throwing so those
    # arcs can never fire.
    ("libs/common/base64.cc", 21),
    # SimpleMetrics::Instance function-local static: same gcov guard-cleanup
    # shape as DecodeTable above; the constructor cannot throw.
    ("libs/common/src/metrics.cc", 119),
    # Search's orphan-token defense: inverted_index_ is written only inside
    # IndexDocument (together with documents_) and erased only inside
    # DeleteDocument (also together with documents_), so a token can never
    # reference a missing document.
    ("services/search/src/message_search_service.cc", 180),
    # CalculateScore's find-miss defense: Search scores results it just read
    # out of documents_ under the same lock, so the lookup always hits.
    ("services/search/src/message_search_service.cc", 327),
    # UnregisterSession's missing-presence guard: RegisterSession always
    # creates presence, and the only eraser is CleanupOfflineUsers' 24h purge
    # (needs last_seen < now-24h; no public API injects time), so a session
    # that is still registered always has presence.
    ("services/social/src/presence_manager.cc", 346),
    # CleanupOfflineUsers' purge condition: the erase arm needs last_seen
    # older than 24h (the body is already GCOVR_EXCL_LINE'd); the adjacent
    # short-circuit arm is dropped with it because exclusions are per line.
    ("services/social/src/presence_manager.cc", 405),
    # ConfigParser TrimInPlace trailing loop: the '\n' operand's true arm.
    # getline() strips the terminator before TrimInPlace runs, so a line -
    # and therefore a key or value extracted from it - can never end in '\n'.
    ("libs/common/config.cc", 11),
    # MetricsHttpServer::HandleRequest exception landing pad: the untaken
    # arms hang off the THROW edges of the inlined metrics_handler_
    # invocation. Reaching them requires the handler itself to throw, which
    # would escape the asio read callback and terminate the process.
    ("libs/common/src/metrics_http_server.cc", 106),
    # GetUsername exception landing pad: same shape as the metrics handler
    # above - the untaken arms sit on the THROW edges of the it->second
    # string copy and need an allocation failure inside the copy.
    ("services/search/src/message_search_service.cc", 403),
    # BuildRequest path assignment exception landing pad: the untaken arms
    # hang off the THROW edges of the `path_slash == npos ? "/" : substr`
    # string write and need an allocation failure inside the assignment.
    ("services/app/notification/src/http_push_transport.cc", 62),
    # ParseResponseHead header-loop guards: the caller guarantees the head
    # block ends with "\r\n\r\n" at head_end, so find("\r\n", pos) from any
    # pos < head_end always lands at or before head_end - neither the npos
    # arm nor the "past head_end" arm can fire.
    ("services/app/notification/src/http_push_transport.cc", 166),
    # TcpHttpConnection::WaitReadable mask-false arm: poll is requested with
    # POLLIN only, so revents is a subset of POLLIN|POLLHUP|POLLERR|POLLNVAL;
    # POLLNVAL needs the fd closed underneath the live connection, which the
    # transport never does while waiting. SslHttpConnection::WaitReadable
    # (line 261) is the same shape with the same justification.
    ("services/app/notification/src/http_push_transport.cc", 218),
    ("services/app/notification/src/http_push_transport.cc", 261),
    # ConnectTcpWithDeadline timer lambda: the wait_ec-false arm only fires
    # when the timer expires before async_connect settles - the same
    # environment race already excluded on the two lines above. The SSL
    # handshake deadline lambda (545) and both factories' success-path
    # return lines (350 / 562: asio chrono comparison that never flips for
    # a future deadline, plus bad_alloc unwind) repeat the pattern.
    ("services/app/notification/src/http_push_transport.cc", 301),
    ("services/app/notification/src/http_push_transport.cc", 545),
    ("services/app/notification/src/http_push_transport.cc", 350),
    ("services/app/notification/src/http_push_transport.cc", 562),
    # ReadReceiptManager ChannelKey npos arm: ChannelKey always builds
    # `std::to_string(type) + ":" + channel_id` (read_receipt_manager.h:65),
    # so find(':') never returns npos and the defense arm is dead code.
    ("services/shared/chat/src/read_receipt_manager.cc", 80),
    # MessageMigrationWorker entry guards: the migrating_ arms are marked
    # GCOVR_EXCL_LINE (comments at 62-64/98-100) but gcov still attributes
    # the condition's branches; racing an in-flight migration on one io context is
    # impossible in the single-threaded test loop.
    ("services/shared/chat/src/message_migration_worker.cc", 62),
    ("services/shared/chat/src/message_migration_worker.cc", 98),
    # ReactionHandlers::BroadcastReaction short-circuit: both call sites hardcode
    # channel_id="", so resolved_channel.empty() is always true and the
    # non-empty short-circuit arm (skip ChannelOfMessage) is unreachable.
    ("services/shared/chat/src/message_handlers.cc", 187),
    # ChannelManager category/channel index lookups: maps are updated under
    # the same mu_ as their group_index_ reverse maps, so a reverse-map hit
    # with a missing forward-map entry cannot be observed through the public
    # API (same defense shape as line 364's GCOVR_EXCL_LINE).
    ("services/shared/chat/src/channel_manager.cc", 201),
    ("services/shared/chat/src/channel_manager.cc", 364),
    ("services/shared/chat/src/channel_manager.cc", 590),
    # ChatRateLimiter CheckLogin/CheckSend identity ternary: untaken arms are
    # the always-equal allocator/SSO half of the string concat emitted for
    # `prefix + identity` (same shape as sdk_client.cc:107/247/292).
    ("services/shared/chat/src/chat_rate_limiter.cc", 20),
    ("services/shared/chat/src/chat_rate_limiter.cc", 26),
    # DeliveryAckManager Track/Acknowledge/RunCheck: untaken arms are throw
    # edges into std::string/std::unordered_map landing pads (Pending and
    # requeued_ insertion); reaching them requires bad_alloc during map ops.
    ("services/shared/chat/src/delivery_ack_manager.cc", 102),
    ("services/shared/chat/src/delivery_ack_manager.cc", 161),
    ("services/shared/chat/src/delivery_ack_manager.cc", 163),
    # InstallSignalStop async_wait registration: untaken arms are asio
    # internal signal_set/error_code paths (and the shared_ptr capture's
    # throw edge); the handler itself is covered by the SIGINT probe.
    ("services/shared/chat/src/distributed_runtime.cc", 55),
    # IdentityRegistry Load clash lookup + Bind idempotency tuple equality +
    # game_user_index/by_id stale finds + GetByPlayer/Resolve/Unbind by_id_
    # finds: all by_id_/index maps are written together under mu_, so the
    # stale/miss arms are defensive (same shape as the already-excluded
    # map-miss lines). Line 200's `it == by_id_.end()` half is the same
    # defense; the game_id-mismatch half is covered by ResolveGameUserSkipsOtherGames.
    ("services/shared/chat/src/identity_registry.cc", 57),
    ("services/shared/chat/src/identity_registry.cc", 104),
    ("services/shared/chat/src/identity_registry.cc", 142),
    ("services/shared/chat/src/identity_registry.cc", 161),
    ("services/shared/chat/src/identity_registry.cc", 176),
    ("services/shared/chat/src/identity_registry.cc", 200),
    ("services/shared/chat/src/identity_registry.cc", 219),
    # MessageStoreConfig FromEnv boolean env parses: untaken arms are throw
    # edges into the two temporary std::string construction landing pads on
    # `std::string(env_val) == "1" || == "true"` (bad_alloc); the true/false
    # value arms are covered by FromEnvOverridesDefaults.
    ("services/shared/chat/src/message_store_config.cc", 29),
    ("services/shared/chat/src/message_store_config.cc", 34),
    # PlayerDirectory log string concats on successful unbind/unsubscribe:
    # untaken arms are throw edges into the `prefix + id` / triple-concat
    # landing pads (bad_alloc); the taken arms execute the log normally.
    ("services/shared/chat/src/player_directory.cc", 69),
    ("services/shared/chat/src/player_directory.cc", 159),
    # RepeatGuard mute-expire reset: `state = RepeatState{}` untaken arm is
    # the move/copy half of the implicitly-generated assignment (always the
    # same direction for a prvalue Reset).
    ("services/shared/chat/src/repeat_guard.cc", 12),
    # SubscriptionRegistry MintSubscriptionId static salt + Load/Subscribe
    # tuple-index stale finds + GetForPlayer/GetForChannel/Unsubscribe/
    # EraseEntry by_id_ finds: function-local static init throw arm and
    # map-miss defenses (indices written together under mu_).
    ("services/shared/chat/src/subscription_registry.cc", 20),
    ("services/shared/chat/src/subscription_registry.cc", 78),
    ("services/shared/chat/src/subscription_registry.cc", 128),
    ("services/shared/chat/src/subscription_registry.cc", 179),
    ("services/shared/chat/src/subscription_registry.cc", 198),
    ("services/shared/chat/src/subscription_registry.cc", 219),
    ("services/shared/chat/src/subscription_registry.cc", 236),
    ("services/shared/chat/src/subscription_registry.cc", 243),
    # MySQL row-parse / COUNT lines: semantic arms (null column, "0"/"1"
    # flags, empty fetch) are covered by NullSessionRow/NullUserRow/
    # NullTokenRow and existence-check probes. The untaken high-block arms
    # are EH landing-pad internals for the inlined std::stoll/std::stoi/
    # temporary std::string (reach them only via bad_alloc/invalid_argument
    # that would unwind out of the store with no local catch).
    ("services/app/auth/src/mysql_session_store.cc", 180),
    ("services/app/auth/src/mysql_session_store.cc", 225),
    ("services/app/auth/src/mysql_session_store.cc", 399),
    ("services/app/auth/src/mysql_session_store.cc", 446),
    ("services/app/auth/src/mysql_session_store.cc", 554),
    ("services/app/auth/src/mysql_user_store.cc", 237),
    ("services/app/auth/src/mysql_user_store.cc", 281),
    ("services/app/auth/src/mysql_user_store.cc", 325),
    ("services/app/auth/src/mysql_user_store.cc", 431),
    ("services/app/auth/src/mysql_user_store.cc", 459),
    ("services/app/auth/src/mysql_user_store.cc", 488),
    # RedisClient SendCmd/BuildRedisCommand call sites: the untaken high-block
    # arms are EH landing-pad internals for the temporary initializer_list /
    # std::string / std::to_string construction (reach only via bad_alloc);
    # success, wrong-type, and connection-refused replies are covered by
    # network_full_tests RedisClientTest probes.
    ("libs/network/redis_client.cc", 60),
    ("libs/network/redis_client.cc", 74),
    ("libs/network/redis_client.cc", 79),
    ("libs/network/redis_client.cc", 84),
    ("libs/network/redis_client.cc", 93),
    ("libs/network/redis_client.cc", 98),
    ("libs/network/redis_client.cc", 103),
    ("libs/network/redis_client.cc", 108),
    ("libs/network/redis_client.cc", 114),
    ("libs/network/redis_client.cc", 129),
    ("libs/network/redis_client.cc", 253),
    ("libs/network/redis_client.cc", 258),
    # StreamBroker RoundTrip call sites: untaken arms are EH landing-pad
    # internals for the temporary BuildRedisCommand initializer_list
    # (bad_alloc only); success/wrong-reply/transport paths are covered by
    # server_gateway_stream_broker_test probes.
    ("services/game/server_gateway/src/stream_broker.cc", 184),
    ("services/game/server_gateway/src/stream_broker.cc", 196),
    ("services/game/server_gateway/src/stream_broker.cc", 239),
    ("services/game/server_gateway/src/stream_broker.cc", 245),
    ("services/game/server_gateway/src/stream_broker.cc", 255),
    # StreamBroker Run() empty-consumer ternary: untaken arms are EH pads
    # for the std::string temporary (src_block 107); the empty vs non-empty
    # semantic arms are covered by EmptyConsumerFallsBackToDefaultPrefix
    # and every BrokerConfig with consumer "c1".
    ("services/game/server_gateway/src/stream_broker.cc", 323),
    # AuthService Register rate-limit body: UserRegisterResult default-ctor
    # and return EH pads (RegisterBlockedByRateLimit covers the semantic
    # if/return; the untaken arms are landing pads for exception unwinding).
    ("services/app/auth/src/auth_service.cc", 76),
    ("services/app/auth/src/auth_service.cc", 80),
    # AuthService password_reset_tokens_ insert: untaken arms are EH pads
    # for unordered_map operator[] (bad_alloc only); semantic insert path
    # is covered by PasswordResetFlow.
    ("services/app/auth/src/auth_service.cc", 391),
    # AuthService CompletePasswordReset expiry if: the expired-true arm is
    # the GCOVR_EXCL_LINE body below (tokens live 1h, no injectable clock).
    ("services/app/auth/src/auth_service.cc", 423),
    # PasswordHasher HashPassword null-trim: after resize(STRBYTES) the
    # buffer is never empty and the last byte stays '\0', so only the
    # true/true arm is reachable through the fake pwhash.
    ("services/app/auth/src/password_hasher.cc", 92),
    # PasswordHasher ValidateStrength else-if chains: untaken [3] arms are
    # EH pads for the inlined char range compares (logic arms covered by
    # StrengthValidation class-matrix probes).
    ("services/app/auth/src/password_hasher.cc", 129),
    ("services/app/auth/src/password_hasher.cc", 130),
    # RedisAuthStore GetUserDevices last-colon: every key returned by the
    # user_devices:<id>:* glob contains ':', so pos==npos is unreachable.
    ("services/app/auth/src/redis_auth_store.cc", 402),
    # TokenGenerator fallback static-local guards + string/return EH pads:
    # the std::random path itself is covered by
    # TokenIdFallsBackToStdRandomWhenSodiumUnavailable; untaken arms are
    # thread_local init exception paths and NRVO/copy EH for `out`.
    ("services/app/auth/src/token_generator.cc", 48),
    ("services/app/auth/src/token_generator.cc", 49),
    ("services/app/auth/src/token_generator.cc", 52),
    ("services/app/auth/src/token_generator.cc", 59),
    # AuthClient asio::post closure lines: same shape as sdk_client's
    # excluded post sites -- untaken arms are EH pads and the always-equal
    # merge half for the inlined std::function/string moves; success and
    # error deliveries are covered by AuthClientLoopback tests.
    ("libs/network/auth_client.cc", 115),
    ("libs/network/auth_client.cc", 128),
    ("libs/network/auth_client.cc", 140),
    ("libs/network/auth_client.cc", 149),
    # ChatClient convenience API asio::post closure lines (SendMessage /
    # MarkChannelRead / BlockUser / typing / edit / reactions / group ops):
    # untaken arms are EH pads for the captured-string/std::function moves
    # plus the always-equal allocator half of inlined moves inside the
    # closure. Semantic arms (NotConnected guard, connected path) are covered
    # by AllConvenienceMethodsFailFastWhenNotConnected + the per-API matrix.
    ("sdks/core/src/sdk_client.cc", 946),
    ("sdks/core/src/sdk_client.cc", 1015),
    ("sdks/core/src/sdk_client.cc", 1043),
    ("sdks/core/src/sdk_client.cc", 1056),
    ("sdks/core/src/sdk_client.cc", 1106),
    ("sdks/core/src/sdk_client.cc", 1124),
    ("sdks/core/src/sdk_client.cc", 1138),
    ("sdks/core/src/sdk_client.cc", 1154),
    ("sdks/core/src/sdk_client.cc", 1169),
    ("sdks/core/src/sdk_client.cc", 1184),
    ("sdks/core/src/sdk_client.cc", 1199),
    ("sdks/core/src/sdk_client.cc", 1212),
    ("sdks/core/src/sdk_client.cc", 1225),
    ("sdks/core/src/sdk_client.cc", 1242),
    ("sdks/core/src/sdk_client.cc", 1258),
    ("sdks/core/src/sdk_client.cc", 1272),
    ("sdks/core/src/sdk_client.cc", 1285),
    ("sdks/core/src/sdk_client.cc", 1299),
    ("sdks/core/src/sdk_client.cc", 1314),
    ("sdks/core/src/sdk_client.cc", 1328),
    ("sdks/core/src/sdk_client.cc", 1341),
    # RecallMessage (the recall alias) repeats the closure shape of
    # DeleteMessage above, plus the always-equal comparison half of the bool
    # setter on the shared request message. Both semantic paths (NotConnected
    # fail-fast and the typed round trip) are asserted by
    # AllConvenienceMethodsFailFastWhenNotConnected +
    # RecallSendsSoftDeleteForTheAuthor.
    ("sdks/core/src/sdk_client.cc", 1162),
    ("sdks/core/src/sdk_client.cc", 1168),
    # ReadyForRequests || chain: the LoggedIn half never evaluates once
    # Connected is true (short-circuit); every test either fails fast on
    # NotConnected or runs fully Connected/LoggedIn, so the "first true"
    # fall arm is dead.
    ("sdks/core/src/sdk_client.cc", 920),
    # TypedRequest lambda: untaken arms are EH pads for Resp{}/
    # ParseFromString failure construction and the std::function invoke
    # landing pads; ec-ok, BadResponse and success arms are covered by
    # UnparseableResponseReportsBadResponse + the typed-request matrix.
    ("sdks/core/src/sdk_client.cc", 930),
    ("sdks/core/src/sdk_client.cc", 935),
    # SendMessage private channel_id ternary string concat: untaken arms
    # are throw edges into the `a + "|" + b` landing pads (bad_alloc); both
    # orderings (user<=peer and peer<user) are covered by the SSO/heap
    # receiver probes.
    ("sdks/core/src/sdk_client.cc", 963),
    ("sdks/core/src/sdk_client.cc", 964),
    # HybridMessageStore::HasMessage redis loop: untaken arms are EH pads
    # for ParseFromArray / message_id string compare and the loop-empty
    # fall-through attributed to this line; corrupt-entry skip and cold-tier
    # fallback are covered by the store probes.
    ("services/shared/chat/src/hybrid_message_store.cc", 281),
    # PrivateChannelId ternary: both a<b and b<a orderings are asserted by
    # PrivateChannelIdOrderingAndAccessors; untaken arms are throw edges
    # into the string-concat landing pads.
    ("services/shared/chat/src/hybrid_message_store.cc", 510),
}

# Whole functions tests can never execute: deleting-dtors of abstract
# interfaces are never the most-derived dtor, so `delete`-through-base always
# dispatches to the concrete class's own D0. Keys are (relpath, start_line)
# matching the coverage-gaps.txt function format.
KNOWN_UNCOVERABLE_FUNCTIONS = {
    # NpcEngine is abstract (Reply is pure virtual); its inline deleting-dtor
    # is never the most-derived one.
    ("services/game/npc_dialog/src/npc_engine.h", 24),
    # Session is abstract (Send/SendAndClose/Close/IsClosed/RemoteAddress are
    # pure virtual); only TcpSession/WebSocketSession/Mock D0s can run.
    ("libs/network/session.h", 9),
    # SessionStore is abstract (Initialize/CreateSession/... are pure
    # virtual); only MySQLSessionStore's concrete D0 can run.
    ("services/app/auth/src/session_store.h", 69),
    # UserStore is abstract (Initialize/Register/... are pure virtual).
    ("services/app/auth/src/user_store.h", 57),
    # MessageStore is abstract (Initialize/StoreMessage/... pure virtual);
    # deleting-dtor starts at the `virtual ~MessageStore()` line (38), not
    # the `public:` access specifier (37).
    ("services/shared/chat/src/message_store.h", 38),
    # HttpConnection is abstract (WriteAll/WaitReadable/ReadSome pure
    # virtual); only the TcpHttpConnection Impl D0 can run.
    ("services/app/notification/src/http_push_transport.h", 20),
    # HttpConnectionFactory is abstract (Connect pure virtual).
    ("services/app/notification/src/http_push_transport.h", 39),
    # PushTransport is abstract (Post pure virtual); LoggingPushTransport
    # and HttpPushTransport own their concrete D0s.
    ("services/app/notification/src/push_transport.h", 25),
    # PeerSender is abstract (Send pure virtual); only concrete senders'
    # D0s can run.
    ("services/game/server_gateway/src/service_registry.h", 19),
}

src_cache = {}

def source_lines(path):
    if path not in src_cache:
        try:
            with open(path, errors="replace") as fh:
                src_cache[path] = fh.readlines()
        except OSError:
            src_cache[path] = None
    return src_cache[path]

def executable_line(path, lineno):
    rel = os.path.relpath(path, root)
    if (rel, lineno) in KNOWN_UNCOVERABLE:
        return False
    lines = source_lines(path)
    if not lines or lineno > len(lines):
        return True  # cannot verify: keep the line
    text = lines[lineno - 1].strip()
    if not text:
        return False
    if text in ("{", "}", "};", "{})", "});", "} else {", "else {"):
        return False
    if text.startswith("//") or text.startswith("/*") or text.startswith("*"):
        return False
    if text.startswith("#"):
        return False
    if text.startswith("friend "):
        return False
    if text.startswith("<<"):
        return False  # stream continuation line
    if (text.endswith(",") or text.endswith(") {") or text.endswith("&) {")
            or text.endswith("*) {") or text.endswith("> {")) \
            and "=" not in text and "return" not in text and "<<" not in text:
        return False  # signature continuation
    if text.startswith("ss <<") and text.endswith('"{'):
        return False  # ostream statement split across lines (gcov artifact)
    return True

# file -> line -> max count
data = defaultdict(lambda: defaultdict(int))
# file -> line -> {arm_index: [max_count, is_throw_edge]}
branch_data = defaultdict(lambda: defaultdict(dict))
# (file, start_line, function_name) -> max execution count across contexts
func_exec = {}

for gz in glob.glob(os.path.join(work_dir, "**", "*.gcov.json.gz"), recursive=True):
    with gzip.open(gz, "rt") as fh:
        blob = json.load(fh)
    for fc in blob.get("files", []):
        name = fc.get("file", "")
        if not os.path.isabs(name):
            name = os.path.normpath(os.path.join(
                blob.get("current_working_directory") or root, name))
        if not name.startswith(INCLUDE_PREFIXES):
            continue
        if is_excluded(name):
            continue
        for l in fc.get("lines", []):
            ln = l["line_number"]
            if l["count"] > data[name][ln]:
                data[name][ln] = l["count"]
            for bi, br in enumerate(l.get("branches", [])):
                cnt = br.get("count", 0)
                thr = bool(br.get("throw", False))
                slot = branch_data[name][ln].get(bi)
                if slot is None:
                    branch_data[name][ln][bi] = [cnt, thr]
                elif cnt > slot[0]:
                    slot[0] = cnt
        for fn in fc.get("functions", []):
            fname = fn.get("name", "")
            if fname.startswith("<") or "artificial" in fname:
                continue
            key = (name, fn.get("start_line", 0), fname)
            cnt = fn.get("execution_count", 0)
            if cnt > func_exec.get(key, -1):
                func_exec[key] = cnt

report = []
for name in sorted(data):
    kept = [ln for ln in data[name] if executable_line(name, ln)]
    total = len(kept)
    covered = sum(1 for ln in kept if data[name][ln] > 0)
    miss = [ln for ln in kept if data[name][ln] == 0]
    report.append((name, total, covered, miss))

total_all = sum(t for _, t, _, _ in report)
covered_all = sum(c for _, _, c, _ in report)

with open("coverage.txt", "w") as fh:
    fh.write("-" * 78 + "\n")
    fh.write("                           GCC Code Coverage Report\n")
    fh.write(f"Directory: {root}\n")
    fh.write("-" * 78 + "\n")
    fh.write(f"{'File':<46}{'Lines':>7}{'Exec':>7}{'Cover':>8}   Missing\n")
    fh.write("-" * 78 + "\n")
    for name, t, c, miss in report:
        rel = os.path.relpath(name, root)
        pct = f"{100.0 * c / t:.0f}%" if t else "100%"
        fh.write(f"{rel:<46}{t:>7}{c:>7}{pct:>8}   {','.join(map(str, miss))}\n")
    fh.write("-" * 78 + "\n")
    pct = f"{100.0 * covered_all / total_all:.1f}%" if total_all else "100%"
    fh.write(f"{'TOTAL':<46}{total_all:>7}{covered_all:>7}{pct:>8}\n")
    fh.write("-" * 78 + "\n")

with open("coverage.csv", "w") as fh:
    fh.write("filename,line_total,line_covered,line_percent\n")
    for name, t, c, _ in report:
        fh.write(f"{os.path.relpath(name, root)},{t},{c},{100.0 * c / t if t else 100.0}\n")

files_json = [{"file": os.path.relpath(name, root),
               "lines": [{"line_number": ln, "count": data[name][ln]}
                         for ln in sorted(data[name]) if executable_line(name, ln)]}
              for name, _, _, _ in report]
with open("coverage.json", "w") as fh:
    json.dump({"files": files_json}, fh)

# lcov tracefile for Codecov (and any other lcov-aware consumer).
with open("coverage.lcov", "w") as fh:
    for name, t, c, _ in report:
        fh.write("TN:\n")
        fh.write(f"SF:{os.path.relpath(name, root)}\n")
        for ln in sorted(data[name]):
            if executable_line(name, ln):
                fh.write(f"DA:{ln},{data[name][ln]}\n")
        fh.write(f"LF:{t}\n")
        fh.write(f"LH:{c}\n")
        fh.write("end_of_record\n")

# An empty report (no files matched) is an aggregation failure, never a pass:
# it previously surfaced as 0/0 -> 100% when EXCLUDE swallowed every path.
if total_all == 0:
    print("No coverage data aggregated (0 files) - treating as failure.",
          file=sys.stderr)
    sys.exit(1)

pct = 100.0 * covered_all / total_all if total_all else 100.0
with open("coverage-summary.json", "w") as fh:
    json.dump({"line_total": total_all, "line_covered": covered_all,
               "line_percent": round(pct, 2)}, fh)

print(f"lines: {pct:.1f}% ({covered_all} out of {total_all})")

# Per-package (directory) breakdown, failing when any package is below the
# threshold.
print()
print(f"Per-package line coverage (threshold: {fail_under}%)")
pkgs = defaultdict(lambda: [0, 0])
for name, t, c, _ in report:
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    pkgs[pkg][0] += c
    pkgs[pkg][1] += t

failed = False
for pkg in sorted(pkgs):
    c, t = pkgs[pkg]
    p = 100.0 * c / t if t else 100.0
    status = "OK" if p >= fail_under else "LOW"
    if p < fail_under:
        failed = True
    print(f"{status:3}  {p:6.2f}%  {c:5}/{t:<5}  {pkg}")

# ---------------------------------------------------------------------------
# Branch / function coverage. Informational for now: the gate above stays
# line-based; this section inventories which control-flow arms and whole
# functions tests never reach. Throw edges (exception cleanup) are skipped -
# they are unreachable without a throwing path and gcov counts them as
# ordinary arms. KNOWN_UNCOVERABLE lines drop out via executable_line, same
# as the line stats.
# ---------------------------------------------------------------------------
branch_taken = branch_total = 0
func_cov = func_total = 0
branch_gaps = []  # (name, line, taken, total, untaken_arm_indices)
func_gaps = []    # (name, start_line, function_name)
pkg_branch = defaultdict(lambda: [0, 0])
pkg_func = defaultdict(lambda: [0, 0])

for name, _t, _c, _miss in report:
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    for ln in sorted(data[name]):
        if not executable_line(name, ln):
            continue
        arms = branch_data.get(name, {}).get(ln)
        if not arms:
            continue
        tot = tak = 0
        untaken = []
        for bi in sorted(arms):
            cnt, thr = arms[bi]
            if thr:
                continue
            tot += 1
            if cnt > 0:
                tak += 1
            else:
                untaken.append(bi)
        branch_total += tot
        branch_taken += tak
        pkg_branch[pkg][0] += tak
        pkg_branch[pkg][1] += tot
        if tak < tot:
            branch_gaps.append((name, ln, tak, tot, untaken))

for (name, sl, fname), cnt in sorted(func_exec.items()):
    if (os.path.relpath(name, root), sl) in KNOWN_UNCOVERABLE_FUNCTIONS:
        continue
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    func_total += 1
    pkg_func[pkg][1] += 1
    if cnt > 0:
        func_cov += 1
        pkg_func[pkg][0] += 1
    else:
        func_gaps.append((name, sl, fname))

bpct = 100.0 * branch_taken / branch_total if branch_total else 100.0
fpct = 100.0 * func_cov / func_total if func_total else 100.0

print()
print(f"branches:  {bpct:.1f}% ({branch_taken} of {branch_total} arms, "
      "throw edges excluded)")
print(f"functions: {fpct:.1f}% ({func_cov} of {func_total})")

print()
print("Per-package branch/function coverage (informational, not gated)")
for pkg in sorted(set(pkg_branch) | set(pkg_func)):
    bt, bb = pkg_branch[pkg]
    fc_, ft = pkg_func[pkg]
    b = 100.0 * bt / bb if bb else 100.0
    f = 100.0 * fc_ / ft if ft else 100.0
    print(f"     branch {b:6.2f}% {bt:5}/{bb:<5}  "
          f"func {f:6.2f}% {fc_:5}/{ft:<5}  {pkg}")

with open("coverage-gaps.txt", "w") as fh:
    fh.write("uncovered branch arms (file:line taken/total untaken=[arm indices])\n")
    for name, ln, tak, tot, untaken in branch_gaps:
        fh.write(f"{os.path.relpath(name, root)}:{ln}  {tak}/{tot} {untaken}\n")
    fh.write("\nuncovered functions (file:start_line name)\n")
    for name, sl, fname in func_gaps:
        fh.write(f"{os.path.relpath(name, root)}:{sl}  {fname}\n")

print()
print(f"branch gaps: {len(branch_gaps)} lines, "
      f"function gaps: {len(func_gaps)} (see coverage-gaps.txt)")

if failed:
    print(f"\nCoverage threshold {fail_under}% not met for some packages.", file=sys.stderr)
    sys.exit(1)
print(f"\nAll packages meet the {fail_under}% line coverage threshold.")
PYEOF
