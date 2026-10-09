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
# COVERAGE_BUILD_DIR isolates the gate from other actors configuring into the
# default build-cov directory concurrently (a second agent or editor task on
# the same checkout wipes test binaries and .gcda mid-run otherwise).
BUILD_DIR="${COVERAGE_BUILD_DIR:-${ROOT_DIR}/build-cov}"
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
  # Same cache variables as the "coverage" preset (see CMakePresets.json), but
  # passed explicitly so COVERAGE_BUILD_DIR can relocate the binary dir; the
  # preset hard-codes ${sourceDir}/build-cov.
  # shellcheck disable=SC2086
  cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DCHIRP_ENABLE_COVERAGE=ON \
    ${CMAKE_ARGS:-}
  # Build everything: a fixed target list here would silently skip targets
  # whose source list changed (or new ones), reporting stale coverage.
  cmake --build "${BUILD_DIR}"
fi

ctest --test-dir "${BUILD_DIR}" --output-on-failure

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
# Keep the intermediates out of /tmp: temp cleaners running on the host have
# been observed deleting files under /tmp mid-run, which silently drops
# packages from the aggregated report. Under the build dir we own the data.
WORK_DIR="$(mktemp -d "${BUILD_DIR}/.gcov-work.XXXXXX")"
trap 'rm -rf "${WORK_DIR}"' EXIT

# gcov writes its JSON intermediates into the current directory and names
# them after the source file, so every gcda is processed in its own
# subdirectory to prevent clobbering between compilation contexts.
i=0
gcov_failed=0
while IFS= read -r -d '' gcda; do
  mkdir -p "${WORK_DIR}/${i}"
  if (cd "${WORK_DIR}/${i}" && gcov -j -b -c "${gcda}" >/dev/null 2>&1); then
    :
  else
    gcov_failed=$((gcov_failed + 1))
    echo "WARNING: gcov failed on ${gcda} (its lines count as uncovered)" >&2
  fi
  i=$((i + 1))
done < <(find "${BUILD_DIR}" -name '*.gcda' -print0)
if [[ ${gcov_failed} -gt 0 ]]; then
  echo "WARNING: ${gcov_failed} gcov invocation(s) failed; report may be incomplete" >&2
fi

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
    # crash_handler SelfExeDir: the readlink-failure return line. /proc/self/exe
    # resolves for every real (and therefore test) process; the branch-level
    # entry for the same condition (crash_handler.cc arm set, line 91) carries
    # the full rationale - keep the two in sync.
    ("libs/common/crash_handler.cc", 92),
    # HttpPushTransport connect-vs-deadline race: the timer arm only fires
    # against a packet-blackhole address. Sandboxes whose gateway SYN-proxies
    # every destination complete the handshake instead, so no
    # environment-independent unit test can hit this arm deterministically.
    # (Lines track ConnectTcpWithDeadline's timer lambda; keep in sync with
    # the branch-level entry for the same lambda below. Re-pinned 302/303 ->
    # 321/322 for the socket-lifetime fix's +19-line upstream drift
    # (36e294c); block layout unchanged.)
    ("services/app/notification/src/http_push_transport.cc", 321),
    ("services/app/notification/src/http_push_transport.cc", 322),
    # Connect's success-path "new TcpHttpConnection" lines carry only the
    # bad_alloc unwind block of the make_unique call; the live block of the
    # statement sits on the neighbouring return line and is covered by the
    # loopback tests. Same for the SSL factory's plain-scheme fallthrough.
    # (Re-pinned 351/523 -> 370/542 for the same 36e294c drift.)
    ("services/app/notification/src/http_push_transport.cc", 370),
    ("services/app/notification/src/http_push_transport.cc", 542),
    # Frame encoder guards: needs a >4GiB message / a Message whose
    # SerializeToArray disagrees with ByteSizeLong. L12's untaken arms are
    # the >UINT32_MAX / >INT_MAX sides of the size check (same 4GiB wall).
    ("libs/network/protobuf_framing.cc", 12),
    ("libs/network/protobuf_framing.cc", 14),
    ("libs/network/protobuf_framing.cc", 20),
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
    # service_bridge request lambdas: asio::post capture EH pads.
    ("libs/network/message_router.cc", 68),
    ("libs/network/redis_session_manager.cc", 97),
    ("libs/network/redis_session_manager.cc", 147),
    ("libs/network/notification_client.cc", 108),
    ("libs/network/service_bridge.cc", 274),
    ("libs/network/service_bridge.cc", 288),
    ("libs/network/server_gateway_peer.cc", 113),
    # WebSocket FindHeaderValue: untaken arms need a header line without
    # trailing \r (fragmented/odd handshake) or getline throwing.
    ("libs/network/websocket_session.cc", 16),
    # PeerConn::Close re-entry / pen-scan miss: closing is set once and the
    # pen erase always finds `this` when the conn was still unregistered;
    # displaced-entry erase needs a same-pointer race on the vector.
    ("libs/network/chat_peer_hub.cc", 398),
    # SendRawPacket/Send while closing: closing is checked before the post
    # and set under the same strand — the post-body closing arm only runs
    # if Close wins the race after Send entered.
    ("libs/network/chat_peer_link.cc", 303),
    ("libs/network/server_gateway_peer.cc", 358),
    # Create-handshake resolve callbacks: stop-during-resolve races.
    ("libs/network/chat_peer_link.cc", 115),
    # SendKickAndClose reason.empty() ? "kicked" : reason — every FailClient
    # call site passes a non-empty literal; empty-reason is dead.
    ("libs/network/service_bridge.cc", 37),
    # notification_service/handlers defensive arms: deferred by decision
    # (other session owns those files).
    ("services/app/notification/src/notification_handlers.cc", 94),
    ("services/app/notification/src/notification_service.cc", 128),
    ("services/app/notification/src/notification_service.cc", 223),
    ("services/app/notification/src/notification_service.cc", 394),
    # ChatClient::Impl::SendRequest timeout sweep: the pending entry is
    # erased only together with timer->cancel(); a handler already dispatched
    # before the cancel exits at the timer_ec arm above, so the find-miss
    # return is unreachable by construction.
    ("sdks/core/src/sdk_client.cc", 470),
    # ChatClient::Impl::SendRequest pending_.emplace: next_seq_ is monotonic,
    # so the red-black insert comparison always walks the greater side; the
    # less/duplicate arms would require a 2^32 sequence wrap.
    ("sdks/core/src/sdk_client.cc", 479),
    # SendPacket null-socket arm: every caller is state-guarded via
    # ReadyForRequests(), so socket_ is only null inside the teardown window
    # between DoClose dropping the socket and the state flip - a disconnect
    # race no public-API sequence reaches deterministically.
    ("sdks/core/src/sdk_client.cc", 764),
    # HandleFrame pong match: the untaken arm is the throw edge into landing
    # pad block 83 (string/stdexcept during logging).
    ("sdks/core/src/sdk_client.cc", 600),
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
    # WordFilterPush PolicyToProto: the WordFilterPolicy switch is exhaustive
    # (all three enumerators covered by the enhanced policy-mirror test); the
    # trailing return only satisfies the compiler.
    ("services/shared/chat/src/word_filter_push.cc", 18),
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
    # (Line re-pinned for the P1-5 per-device split: slot param + Late
    # struct pushed RunCheck down; guard semantics unchanged.)
    ("services/shared/chat/src/delivery_ack_manager.cc", 165),
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
    # ServiceBridge write-error arm: the peer RST always surfaces on the parked
    # header read first, and FailClient then removes the connection, so a
    # later forward can never target the dead socket with an in-flight write.
    ("libs/network/service_bridge.cc", 117),
    ("libs/network/service_bridge.cc", 118),
    # ServiceBridge kConnecting switch arm: reads start only after the connect
    # handler flips the state, so no frame is ever handled while connecting.
    ("libs/network/service_bridge.cc", 219),
    ("libs/network/service_bridge.cc", 220),
    # ServiceBridge::FailClient re-entry guard: the failed/closing flags make a
    # second entry unreachable in the single-threaded call graph - the timer,
    # read and write completions all check those flags before calling.
    ("libs/network/service_bridge.cc", 398),
    # ServiceBridge::ReattachForDegrade expired-credential erase: a forward
    # for a session whose weak ref already expired dereferences the session
    # in DegradeSearch first, so reaching this arm would already be
    # use-after-free - unreachable in the single-threaded gateway graph.
    ("libs/network/service_bridge.cc", 452),
    ("libs/network/service_bridge.cc", 453),
    # Server-plane registry defensive arms: the by-id map and the
    # tuple/game-user index are only ever mutated together under one lock,
    # so a tuple hit whose by-id record is missing cannot happen. (These
    # registries moved from services/game/server_gateway into
    # services/shared/chat with app_chat; the arms kept their line numbers.)
    ("services/shared/chat/src/identity_registry.cc", 157),
    ("services/shared/chat/src/identity_registry.cc", 191),
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
    # Search index（message_search 批）的死臂，三类：
    # ① I/O 类——建表/BEGIN/COMMIT 语句的 step 期失败只剩磁盘 I/O /
    #   SQLITE_FULL 一类故障，进程内无注入点；prepare 期损坏库形态由
    #   CorruptDatabaseFailsOpen 在 Open 的 schema 臂覆盖。
    # ② 二连接篡改下的 prepare 失败臂（del/ins_map/delete/search）——实测
    #   linked sqlite 在开放事务 + 二连接 DDL 之下错误全部落在 step 期
    #   （连接内 schema 缓存；篡改测试系列已逐步覆盖各 step 失败臂），同
    #   语句文本为常量、健康 schema 下 prepare 不会失败。del 臂（220 段）
    #   由 DeleteMessageFailsWhenMapDropped 实测覆盖（map 被篡改后 del_fts
    #   的子查询 prepare 即失败），不在此列。151-154/194-198 含 152/195：
    #   gcov 把多字面量拼接的错误消息续行单独记行，需逐行钉。
    # ③ UpsertMessage 内 ins_fts.Run 失败的回滚臂（187-188）不可达：fts 镜
    #   像表受损时事务内先行的 del step 必先失败（同②），ins_map 同理。
    ("services/search/src/message_search_index.cc", 114),
    ("services/search/src/message_search_index.cc", 115),
    ("services/search/src/message_search_index.cc", 116),
    ("services/search/src/message_search_index.cc", 143),
    ("services/search/src/message_search_index.cc", 151),
    ("services/search/src/message_search_index.cc", 152),
    ("services/search/src/message_search_index.cc", 153),
    ("services/search/src/message_search_index.cc", 154),
    ("services/search/src/message_search_index.cc", 156),
    ("services/search/src/message_search_index.cc", 157),
    ("services/search/src/message_search_index.cc", 187),
    ("services/search/src/message_search_index.cc", 188),
    ("services/search/src/message_search_index.cc", 194),
    ("services/search/src/message_search_index.cc", 195),
    ("services/search/src/message_search_index.cc", 197),
    ("services/search/src/message_search_index.cc", 198),
    ("services/search/src/message_search_index.cc", 208),
    ("services/search/src/message_search_index.cc", 209),
    ("services/search/src/message_search_index.cc", 284),
    ("services/search/src/message_search_index.cc", 285),
    ("services/search/src/message_search_index.cc", 287),
    # CleanupOfflineUsers' purge condition: the erase arm needs last_seen
    # older than 24h (the body is already GCOVR_EXCL_LINE'd); the adjacent
    # short-circuit arm is dropped with it because exclusions are per line.
    ("services/social/src/presence_manager.cc", 405),
    # ConfigParser TrimInPlace trailing loop: the '\n' operand's true arm.
    # getline() strips the terminator before TrimInPlace runs, so a line -
    # and therefore a key or value extracted from it - can never end in '\n'.
    ("libs/common/config.cc", 11),
    # TcpHttpConnection::WaitReadable mask-false arm: poll is requested with
    # POLLIN only, so revents is a subset of POLLIN|POLLHUP|POLLERR|POLLNVAL;
    # POLLNVAL needs the fd closed underneath the live connection, which the
    # transport never does while waiting. SslHttpConnection::WaitReadable
    # (line 261) is the same shape with the same justification.
    ("services/app/notification/src/http_push_transport.cc", 261),
    # ConnectTcpWithDeadline timer lambda: the wait_ec-false arm only fires
    # when the timer expires before async_connect settles - the same
    # environment race already excluded on the two lines above. The SSL
    # handshake deadline lambda (545) and both factories' success-path
    # return lines (350 / 562: asio chrono comparison that never flips for
    # a future deadline, plus bad_alloc unwind) repeat the pattern.
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
    # (Lines re-pinned for the P1-5 per-device split: slot param + struct
    # member insertions shifted the statements; pad semantics unchanged.)
    ("services/shared/chat/src/delivery_ack_manager.cc", 116),
    ("services/shared/chat/src/delivery_ack_manager.cc", 182),
    ("services/shared/chat/src/delivery_ack_manager.cc", 185),
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
    ("services/shared/chat/src/identity_registry.cc", 156),
    ("services/shared/chat/src/identity_registry.cc", 175),
    ("services/shared/chat/src/identity_registry.cc", 190),
    ("services/shared/chat/src/identity_registry.cc", 214),
    ("services/shared/chat/src/identity_registry.cc", 233),
    # MessageStoreConfig FromEnv boolean env parses: untaken arms are throw
    # edges into the two temporary std::string construction landing pads on
    # `std::string(env_val) == "1" || == "true"` (bad_alloc); the true/false
    # value arms are covered by FromEnvOverridesDefaults.
    ("services/shared/chat/src/message_store_config.cc", 29),
    ("services/shared/chat/src/message_store_config.cc", 34),
    # PlayerDirectory log string concats on successful unbind/unsubscribe:
    # untaken arms are throw edges into the `prefix + id` / triple-concat
    # landing pads (bad_alloc); the taken arms execute the log normally.
    ("services/shared/chat/src/player_directory.cc", 99),
    ("services/shared/chat/src/player_directory.cc", 195),
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
    # AuthService password_reset_tokens_ insert: untaken arms are EH pads
    # for unordered_map operator[] (bad_alloc only); semantic insert path
    # is covered by PasswordResetFlow.
    ("services/app/auth/src/auth_service.cc", 391),
    # PasswordHasher HashPassword null-trim: after resize(STRBYTES) the
    # buffer is never empty and the last byte stays '\0', so only the
    # true/true arm is reachable through the fake pwhash.
    ("services/app/auth/src/password_hasher.cc", 92),
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
    ("sdks/core/src/sdk_client.cc", 1184),
    ("sdks/core/src/sdk_client.cc", 1199),
    ("sdks/core/src/sdk_client.cc", 1225),
    ("sdks/core/src/sdk_client.cc", 1272),
    ("sdks/core/src/sdk_client.cc", 1285),
    ("sdks/core/src/sdk_client.cc", 1314),
    ("sdks/core/src/sdk_client.cc", 1328),
    ("sdks/core/src/sdk_client.cc", 1341),
    # RecallMessage (the recall alias) repeats the closure shape of
    # DeleteMessage above, plus the always-equal comparison half of the bool
    # setter on the shared request message. Both semantic paths (NotConnected
    # fail-fast and the typed round trip) are asserted by
    # AllConvenienceMethodsFailFastWhenNotConnected +
    # RecallSendsSoftDeleteForTheAuthor.
    ("sdks/core/src/sdk_client.cc", 1168),
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
    # deleting-dtor starts at the `virtual ~MessageStore()` line (39), not
    # the `public:` access specifier (38). (Re-pinned from 38 after the
    # header gained a line and gcov's function start-line moved.)
    ("services/shared/chat/src/message_store.h", 39),
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

# Individual branch arms that stay untaken even though every reachable arm of
# the same line has a reproducing test, and that a whole-line KNOWN_UNCOVERABLE
# entry would wrongly drop from the line statistics. Keys are (relpath, line);
# values are (untaken arm indices, why). Exempt arms leave the branch
# denominator and are re-listed in every report run, so an arm becoming
# reachable (source edit, compiler upgrade) is visible as a mismatch against
# this table instead of silently shrinking coverage.
KNOWN_UNCOVERABLE_ARMS = {
    # -- crash_handler.cc: crash collection (Crashpad batch) -----------------
    # The exempted arms are gcc's never-entered bad_alloc unwind pads behind
    # the inlined std::string constructions, plus two environment-unreachable
    # condition directions documented per line. Every live edge is exercised
    # by the crash_handler_test suite (flag/env/default precedence, walk-up
    # hit/limit, pending-scan, no-op stub degradation).
    ("libs/common/crash_handler.cc", 76): ((6, 7),
        "GetEnv: throw-inspection arms of the inlined std::string "
        "constructions (allocation-failure continuation after the null "
        "check; the empty-value edge and the value edge are both taken). "
        "An always-throwing new_handler cannot be scoped to a single "
        "getenv-shaped helper; allocation-failure unwind, "
        "environment-unreachable."),
    ("libs/common/crash_handler.cc", 91): ((0,),
        "SelfExeDir readlink failure arm: /proc/self/exe always resolves "
        "for every real (and therefore test) process; the >4095-byte path "
        "truncation semantics of readlink(2) cannot produce n<=0 here."),
    ("libs/common/crash_handler.cc", 97): ((1, 7, 8, 9),
        "SelfExeDir return: has_filename is always true for a resolved "
        "/proc/self/exe (regular file), so the false direction of the "
        "ternary is unreachable; the remaining arms are the throw-"
        "inspection slots of the inlined path/string constructions "
        "(allocation-failure unwind, environment-unreachable)."),
    ("libs/common/crash_handler.cc", 137): ((1, 7, 10, 11),
        "Handler walk-up loop: the has_parent_path false direction never "
        "fires because the 4-level walk-up limit terminates root-bound "
        "searches first (deepest tested exe_dir is 5 levels), and the "
        "remaining arms are throw-inspection slots of the inlined path "
        "constructions (allocation-failure unwind, environment-"
        "unreachable). Live edges taken: hit at every walk-up depth "
        "(0-3), hit beyond the limit -> miss, root break."),

    # -- unwind-only edges of inlined construction ---------------------------
    # On every line below the arms with live callers are exercised; the
    # exempted arms are the trailing never-entered block(s) gcc emits for the
    # exceptional path of the inlined std::string / std::function
    # construction (bad_alloc unwind) - identical 0/0 two-edge signatures,
    # immune to every argument-shape manipulation (SSO vs heap, small vs
    # >160-byte captures), same class as the EH-pad KNOWN_UNCOVERABLE entries.
    ("libs/network/chat_peer_hub.cc", 119): ((2, 4, 5),
        "SendInject post-call unwind (batch-14 audit): arm 2 is gcc's "
        "cleanup-state dispatch (mov $0,%ebx before asio::post, "
        "test %bl,%bl/je after) - the fall-through destroys the closure "
        "and rethrows, i.e. the EH continuation for a throwing post; arms "
        "4-5 sit in the never-executed unwind pads behind it. asio::post's "
        "only throw source is its operation allocation, which serves from "
        "the recycling allocator's thread-local cache and reaches global "
        "operator new only on a cache miss: an always-throwing "
        "std::new_handler around an empty-notify SendInject allocates "
        "nothing at all and succeeds (verified empirically), and draining "
        "the cache deterministically would mean poking asio-internal "
        "freelist state. Heap-payload (>15-byte body) injects are "
        "exercised; every live edge of the line is taken."),
    ("libs/network/chat_peer_hub.cc", 420): ((10, 11),
        "idle-warn concat unwind (batch-14 audit): arms 10-11 are the "
        "branches inside the never-executed operator+ unwind pads (blocks "
        "47/51/53/57/58/60) that destroy the partially built concat "
        "temporaries when an allocation throws inside the strand's "
        "idle-timeout handler. All live edges are taken: the ternary's "
        "both arms (3 unregistered + 6 registered timeouts), every concat "
        "allocation's success edge, and the result string's destruction in "
        "both storage classes. A new_handler window cannot be scoped to a "
        "single handler inside a poll() batch - the first allocation would "
        "hit an arbitrary handler and unwind through the scheduler. "
        "Allocation-failure unwind, environment-unreachable."),
    ("libs/network/device_presence.cc", 31): ((2, 3),
        "LoginKickReason ternary string construction unwind edges"),
    ("libs/network/message_router.cc", 82): ((4, 6, 7),
        "dispatch asio::post closure construction unwind edges"),
    ("libs/network/redis_client.cc", 113): ((6, 7),
        "EXPIRE command-argument construction unwind edges"),
    ("libs/network/redis_client.cc", 119): ((6, 7),
        "LRANGE command-argument construction unwind edges"),
    ("libs/network/redis_client.cc", 134): ((6, 7),
        "KEYS command-argument construction unwind edges"),
    ("libs/network/redis_client.cc", 263): ((6, 7),
        "UNSUBSCRIBE command-argument construction unwind edges"),
    ("libs/network/session_registry.cc", 39): ((8, 9),
        "NormalizePlatformId ternary result construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1214): ((6, 8, 10, 11, 12, 13),
        "FetchReactions post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1227): ((4, 6, 7),
        "FetchReadReceipts post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1240): ((6, 8, 10, 11, 12, 13),
        "BulkDeleteMessages post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1257): ((6, 8, 10, 11, 12, 13),
        "FetchMentionSuggestions post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1273): ((6, 8, 10, 11, 12, 13),
        "CreateGroup post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1287): ((4, 6, 7),
        "JoinGroup post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1300): ((4, 6, 7),
        "LeaveGroup post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1329): ((6, 8, 10, 11, 12, 13),
        "InviteToGroup post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1343): ((4, 6, 7),
        "KickMember post-closure construction unwind edges"),
    ("sdks/core/src/sdk_client.cc", 1356): ((4, 6, 7),
        "FetchGroupInfo post-closure construction unwind edges"),
    # -- 覆盖率批次5（hybrid_message_store.cc 逐臂审计）----------------------
    # All semantic input classes are exercised for each of the lines below
    # (empty field, non-numeric text, 17-digit heap-allocated field, ERANGE
    # overflow, INT64_MAX-exact boundary, valid values; see
    # MalformedDeliveryStatusValuesFallBackToDefaults /
    # PendingDeliveriesGrowPastInlineCapacity in chat_mysql_test.cc). The
    # exempted arms are the residual edges that stayed 0 under every input:
    # post-inline dead blocks / allocation-failure unwind paths that no
    # argument shape can reach.
    ("services/shared/chat/src/hybrid_message_store.cc", 528): ((8, 9),
        "GetDeliveryStatus: dead post-inline duplicate of the second "
        "ParseI64 entry block (46->47/46->48 with the never-returning call); "
        "every live edge of both parses is taken across empty, non-numeric, "
        "overflow, heap-length and valid status values. (Re-pinned from "
        "(7, 8): gcov arm indices shifted after upstream line-count drift; "
        "the dead pair is today's (8, 9) — pinning only 9 let 8 resurface "
        "in the batch-7 gate. Line re-pinned 514->524 for the P1-5 "
        "per-device split's upstream line drift; block layout unchanged. "
        "Line re-pinned 524->528 for the ActorKind sender_kind +4-line "
        "upstream drift; block layout unchanged.)"),
    ("services/shared/chat/src/hybrid_message_store.cc", 560): ((3,),
        "GetPendingDeliveries expiry parse: structurally dead edge in the "
        "inlined ParseI64/substr block layout (12->14); empty, non-numeric, "
        "17-digit heap, overflow and INT64_MAX-exact expiry inputs all "
        "exercise the other five arms. (Line re-pinned 546->556 for the "
        "P1-5 per-device split's upstream line drift; 556->560 for the "
        "ActorKind sender_kind +4-line upstream drift.)"),
    ("services/shared/chat/src/hybrid_message_store.cc", 585): ((14, 15, 16, 17),
        "PrivateChannelId string-concat fragments attributed to the "
        "push_back line: operator+ allocation-failure blocks (calls with "
        "returned=0, bad_alloc unwind); the live SSO and heap-concat edges "
        "are exercised by short and >15-char channel-id inputs. (Re-pinned "
        "from (10, 11, 12, 13): those live arms are now taken and gcov "
        "renumbered the pad arcs 16->17/16->18 and 20->21/20->22. Line "
        "re-pinned 571->581 for the P1-5 per-device split's upstream "
        "line drift; 581->585 for the ActorKind sender_kind +4-line "
        "upstream drift.)"),
    ("services/shared/chat/src/player_directory.cc", 194): ((20, 21, 22, 23, 24, 25),
        "Unsubscribe log line: internal arcs of the bad_alloc landing pads "
        "(blocks 72/76/80, the unwind targets of the log-concat operator+ "
        "chain); no argument shape can allocate-fail, and every live ternary/"
        "concat edge is taken by the by-id and by-tuple unsubscribe tests"),
    ("services/shared/chat/src/recall_tombstone.cc", 32): ((9, 10, 11),
        "MarkRecalledInRedisList write-back: 32[9] is a structurally dead "
        "parallel arc 29->31 beside the executed 29->30->31 loop-tail route "
        "(block 30 is the SerializeAsString temporary's destructor); "
        "32[10, 11] are internal arcs of the bad_alloc pad at block 43 "
        "(unwind targets of the SerializeAsString/LSet calls). The reachable "
        "short-circuit outcomes LSet-false and ok-already-false are both "
        "exercised (ReportsWriteFailure / PartialFailureKeepsFailingResult)"),
    # -- invariant-defensive arms --------------------------------------------
    ("libs/network/chat_peer_hub.cc", 412): ((2,),
        "ArmIdleTimer's async_wait: Close() cancels the timer before setting "
        "the closing flag, so the handler never sees ec==OK with closing set "
        "except in the sub-millisecond window where the expiry completion is "
        "already queued on the strand when a packet-triggered Close runs "
        "(same shape as the websocket_client handshake-write race above); "
        "both deterministic sides (ec==OK/closing==false after a real idle "
        "timeout, ec==aborted on Stop) have tests"),
    ("libs/network/chat_peer_hub.cc", 141): ((1,),
        "service_id_for_game scan: peers_ is only ever populated with conns "
        "whose registered flag was already set true (HandleRegister inserts "
        "after flipping it, every Close removes), so the registered==false "
        "side of the scan predicate cannot fire"),
    ("libs/network/chat_peer_hub.cc", 475): ((3,),
        "Close's displaced-else: displacement (the only code that re-points a "
        "peers_ entry) calls Close on the old conn BEFORE overwriting the "
        "entry, so a closing registered conn always still owns its table "
        "entry; the else is defensive against a future reordering"),
    # -- notification push transport / service (2026-09-27 gap sweep) -------
    ("services/app/notification/src/http_push_transport.cc", 63): ((8, 9),
        "ParseHttpUrl path-assignment EH-pad arcs: the two 0/0 edges hang off "
        "the THROW targets of the inlined `path_slash == npos ? \"/\" : "
        "url.substr(path_slash)` string write (same shape as line 62) and "
        "need an allocation failure inside the assignment"),
    ("services/app/notification/src/http_push_transport.cc", 167): ((1, 3),
        "ParseResponseHead header-loop guard-fire arms: the caller only "
        "parses a head whose \\r\\n\\r\\n terminator it already located at "
        "head_end, so from any pos < head_end find(\"\\r\\n\", pos) lands at "
        "or before head_end - neither the npos arm nor the past-head_end arm "
        "can fire (line 166 exempts the loop-condition siblings)"),
    ("services/app/notification/src/http_push_transport.cc", 352): ((4, 6, 7),
        "TcpHttpConnectionFactory::Connect construction line: the untaken "
        "arms are the EH-pad-internal arcs of the `new "
        "TcpHttpConnection(io, make_unique<socket>(...))` chain (bad_alloc "
        "unwind, same family as the line-350 success-path return) plus one "
        "always-false inlined-block edge on that chain"),
    ("services/app/notification/src/http_push_transport.cc", 524): ((4, 6, 7),
        "SslHttpConnectionFactory::Connect construction line: identical "
        "block topology to the Tcp variant on line 352"),
    ("services/app/notification/src/notification_service.cc", 48): ((4, 5),
        "ctor make_shared<LoggingPushTransport> EH-pad-internal arcs: only "
        "an allocation failure inside the fallback construction can enter "
        "them"),
    ("services/app/notification/src/notification_service.cc", 130): ((1,),
        "UnregisterDevice user-index miss: devices_ and user_to_devices_ are "
        "only ever mutated together under the same mutex (RegisterDevice "
        "inserts both, UnregisterDevice/CleanupInactiveDevices erase both), "
        "so a devices_ hit whose user entry is missing cannot happen"),
    ("services/app/notification/src/notification_service.cc", 177): ((1,),
        "GetUserDevices per-device miss: the user index only lists ids that "
        "RegisterDevice put into devices_ under the same lock, and every "
        "eraser removes both sides together, so the lookup always hits"),
    ("services/app/notification/src/notification_service.cc", 409): ((1,),
        "CleanupInactiveDevices user-index miss: same co-maintained-map "
        "invariant as line 130"),
    ("services/app/notification/src/http_push_transport.cc", 534): ((3,),
        "is_ip_literal lambda digit-range lower-bound arm (c < '0'): the "
        "lambda only runs after a successful TCP connect (line 519 returns "
        "first otherwise), and every host string getaddrinfo resolves "
        "without environment control is digits+dots (numeric IPv4), colon-"
        "bearing (IPv6, short-circuited at line 532), or lettered (letters "
        "fail the c <= '9' upper-bound arm, which tests cover) - no "
        "resolvable name can evaluate a char below '0' here"),
    # -- 覆盖率批次8（2026-09-29 陈旧行钉审计：降格迁移）------------------
    # Each of these lines carried a whole-line KNOWN_UNCOVERABLE pin while
    # every block on the line actually executed (count>0, unexecuted_block
    # false in every gcda context of the audit run) - the pin was deflating
    # the line denominator while the only dead residue was branch arms
    # (EH / initializer_list cleanup paths, structurally unreachable
    # guards). The lines are back in the covered line statistics; these
    # arm pins keep the informational branch inventory honest. Arm indices
    # folded across every gcda context (max per index), same fold the gate
    # itself uses; every pinned arm stayed 0 across all 228 contexts.
    ("libs/common/src/metrics.cc", 119): ((3,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/common/src/metrics_http_server.cc", 106): ((8, 9),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/auth_client.cc", 140): ((2, 4, 5),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/auth_client.cc", 149): ((2, 4, 5),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/service_bridge.cc", 131): ((2,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/service_bridge.cc", 264): ((3, 5, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/service_bridge.cc", 265): ((0,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/chat_peer_link.cc", 150): ((2,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/chat_peer_link.cc", 283): ((3, 4),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/chat_peer_link.cc", 307): ((2,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 60): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 74): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 79): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 84): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 93): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 98): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 103): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 108): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/redis_client.cc", 258): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/server_gateway_peer.cc", 214): ((1,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/server_gateway_peer.cc", 341): ((3, 4),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/server_gateway_peer.cc", 362): ((2,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("libs/network/session_registry.cc", 35): ((8, 9),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk.cc", 41): ((3,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk_client.cc", 209): ((2,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk_client.cc", 268): ((14, 15, 16, 17),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk_client.cc", 418): ((8, 9),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk_client.cc", 426): ((10, 11),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk_client.cc", 466): ((0,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk_client.cc", 920): ((1,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("sdks/core/src/sdk_client.cc", 964): ((3, 4, 6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/app/auth/src/auth_service.cc", 76): ((0, 1),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/app/auth/src/auth_service.cc", 80): ((0,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/app/auth/src/auth_service.cc", 423): ((2,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/app/auth/src/mysql_session_store.cc", 180): ((12, 13, 14, 15),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/app/notification/src/http_push_transport.cc", 218): ((3,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/app/notification/src/http_push_transport.cc", 301): ((0,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/game/server_gateway/src/stream_broker.cc", 184): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/game/server_gateway/src/stream_broker.cc", 196): ((4, 5),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/game/server_gateway/src/stream_broker.cc", 239): ((6, 7),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/game/server_gateway/src/stream_broker.cc", 245): ((4, 5),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/game/server_gateway/src/stream_broker.cc", 255): ((4, 5),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/game/server_gateway/src/stream_broker.cc", 323): ((8, 9),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
    ("services/social/src/presence_manager.cc", 346): ((1,),
        "batch-8 migration from a stale whole-line pin; dead arm(s) in every gcda context of the audit run"),
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
exempted_arm_count = 0

for name, _t, _c, _miss in report:
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    for ln in sorted(data[name]):
        if not executable_line(name, ln):
            continue
        arms = branch_data.get(name, {}).get(ln)
        if not arms:
            continue
        arm_entry = KNOWN_UNCOVERABLE_ARMS.get((os.path.relpath(name, root), ln))
        exempt_arms = arm_entry[0] if arm_entry else ()
        tot = tak = 0
        untaken = []
        for bi in sorted(arms):
            cnt, thr = arms[bi]
            if thr:
                continue
            if bi in exempt_arms:
                exempted_arm_count += 1
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
if KNOWN_UNCOVERABLE_ARMS:
    print(f"exempted branch arms: {exempted_arm_count} (KNOWN_UNCOVERABLE_ARMS)")
    for (arm_path, arm_ln), (arm_idxs, arm_why) in sorted(KNOWN_UNCOVERABLE_ARMS.items()):
        print(f"  {arm_path}:{arm_ln} arms={list(arm_idxs)} - {arm_why}")

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
