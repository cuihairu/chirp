# Code Deep Review — 2026-11-01

> Scope: full-repo security static scan + hidden-bug hunt + quality gate (requesting-code-review process, steps ①–⑥).
> Coverage note: the C++ core is gated in CI by `-Werror` (compiler-as-bug-hunter) and a 100% branch coverage gate
> (`scripts/run_coverage.sh`, FAIL_UNDER=100) plus TSan-instrumented tests for the transport seam; the static scan and
> targeted deep reads below are the *additional* human pass on top of that automated surface. No `compile_commands.json`
> existed (the coverage build tree had been cleaned), so `clang-tidy` could not be driven reliably; the bug hunt used
> targeted reads of the highest-risk/newest code instead. This is stated so the coverage is not overstated.

## Totals

| Severity | Count | Fixed this round | Recorded to todo |
| --- | --- | --- | --- |
| P0 | 0 | 0 | 0 |
| P1 | 0 | 0 | 0 |
| P2 | 0 | 0 | 0 |
| P3 | 5 | 0 | 5 |

No P0/P1 finding was found, so no source change was required this round; all five P3 items are defense-in-depth
robustness gaps recorded to `todo.md` (see below).

## ① Security static scan (whole repo)

Scanned for: hardcoded secrets/material, shell injection, SQL concatenation, `eval`/XSS, unsafe deserialization,
path traversal. Exclusions: `node_modules`, `build`, `vendor`, `dist`, `target`, generated `*.pb.*`, lockfiles.

| Category | Result |
| --- | --- |
| Hardcoded key material (`BEGIN *PRIVATE KEY`, `AKIA…`, `ghp_…`, `sk_live_…`, `xox…`) | Only test-fixture PEM in `tests/unit/notification_apns_jwt_test.cc:40,98,229,232` (legitimate) — no live secret |
| Shell exec (`system`/`popen`/`exec*`/`child_process.exec`/`subprocess`) | **0 hits** repo-wide |
| SQL concatenation | 4 sites, 3 server-side all escaped/parameterized (`mysql_user_store.cc:407` `EscapeString`; `mysql_message_store.cc:516,533` `conn->Escape`/`to_string`) — safe; 1 tool-side (see P3-2) |
| `eval`/`new Function`/`innerHTML`/`document.write` | **0 hits** |
| Unsafe deserialization (`pickle`/`unserialize`/`ObjectInputStream`/`yaml.load`) | **0 hits** |
| Path traversal (`../` into `fopen`/`open`/`readFile`) | **0 hits** |

## ② Hidden-bug hunt (targeted deep reads, highest-risk/newest code)

Files read in full: `services/app/notification/src/http_push_transport.cc` (hand-rolled HTTP/1.1+asyncio+OpenSSL),
`apns_jwt.cc` (ES256 provider JWT), `device_store.h` (file-backed device registry); plus bounds/off-by-one review of
`services/shared/chat/src/hybrid_message_store.cc` (message paging + pending-delivery expiry sweep). The JWT default
secret was traced to its gates (below). No P0/P1/P2; three P3 robustness gaps in the transport/device-store layer.

## ③ Quality gate (test baseline comparison)

- Last **completed** `ci.yml` (coverage gate) run on `main`: `ef605e0` — **success**.
- The most recent code commit `1b13549` (proto TS regen) has a `ci.yml` run that was **cancelled** (not failed) —
  no regression, but the coverage gate was not re-confirmed on the very latest code commit. All other recent
  workflows (nightly, dependabot docker/go/actions auto-updates) are `success`.
- Conclusion: **baseline green, no regression**; no new test/typecheck/lint break introduced by this review (no source
  change made).

## Findings (P3, by severity)

### P3-1 — JWT signing secret default is a latent footgun

- **Location**: `services/app/auth/src/auth_service.h:30`
- **Claim**: `AuthService::Config::jwt_secret` defaults to the literal `"change_this_secret_in_production"`.
- **Evidence it is not a live secret**: both production entry points gate the flag with `RequireArg` — `main_enhanced.cc:361`
  and `main.cc:183` (`libs/common/required_arg.h:13` returns false when the flag is absent, so startup aborts; the error
  text even says "there is no built-in default"). The only `Config{}` constructions are `main_enhanced.cc:354` (right
  before the gate) and test files. So the weak default is never used by a running service.
- **Why it matters**: any future code path that builds `Config` directly without the flag silently gets a
  well-known signing secret.
- **Fix (defense-in-depth)**: default the field to an empty string so a misuse fails loudly at token-sign/verify time
  rather than signing with a published constant. Not a live vulnerability today.

### P3-2 — Benchmark exporter interpolates an operator-controlled table name into SQL

- **Location**: `tools/benchmark/chat_mysql_exporter.cc:117` (and `:100`)
- **Claim**: `WriteInsert`/`WriteSchema` splice the `table` argument into DDL/DML inside backticks without escaping or
  validation.
- **Evidence**: `table` comes from `GetArg(argc, argv, "--history_table", "chat_messages")` at `:197-198` — an
  operator-supplied CLI flag on an offline export tool that writes a `.sql` file the operator runs themselves. Not an
  untrusted-input path and not a server; no attacker-reachable injection.
- **Why it matters**: if the tool is ever reused with a flag sourced from a config file, the SQL becomes injectable.
- **Fix (low)**: validate `table` against `[A-Za-z0-9_]+` before interpolation.

### P3-3 — Content-Length parse lacks an overflow guard

- **Location**: `services/app/notification/src/http_push_transport.cc:196-203`
- **Claim**: the Content-Length digit loop does `parsed = parsed * 10 + digit` with no bound; a multi-megabyte digit
  string wraps `std::size_t` (unsigned), and a wrapped value can pass the `> max_body_bytes` check at `:458` and drive a
  wrong `buf.resize(content_length)` at `:467` (body truncation accepted as success) or a hang until the deadline.
- **Evidence**: only reachable from a misbehaving/malicious provider endpoint (Apple/FCM, or an operator-configured
  `--apns-endpoint`/`--fcm-endpoint` proxy), so not attacker-reachable in the default config; but the wrap is real.
- **Fix (defense-in-depth)**: stop the loop once `parsed > max_body_bytes` (or use a checked multiply).

### P3-4 — DNS resolution is outside the connect deadline

- **Location**: `services/app/notification/src/http_push_transport.cc:309`
- **Claim**: `resolver.resolve(host, port, *ec)` is a blocking synchronous call that runs before the deadline timer
  (`:318`), so a slow/stuck resolver is not bounded by `connect_timeout_ms`.
- **Evidence**: `connect_timeout_ms` governs TCP connect and (separately) the TLS handshake, but not DNS.
- **Fix (low)**: move resolution under the deadline (or run it via `async_resolve`) so the whole egress attempt is bounded.

### P3-5 — `strtoll` result unvalidated in device-store decode

- **Location**: `services/app/notification/src/device_store.h:169`
- **Claim**: `device->registered_at = std::strtoll(cells[5].c_str(), nullptr, 10)` ignores both the `endptr` and
  `errno`; a corrupt/non-numeric `registered_at` cell silently becomes `0` (or clamps on overflow).
- **Evidence**: only reachable by editing the on-disk `--device_store_file` snapshot by hand; the file is written by the
  service under the registry lock with an atomic tmp+rename, so corruption is not expected in normal operation.
- **Fix (low)**: parse with `endptr`/`errno` checks and skip the row on failure (the row is already `Warn`-logged on
  other decode failures).

## ⑤ P0/P1 fix decision

None — no P0/P1/P2 finding, so no source change was warranted this round. All five P3 items are recorded to
`todo.md` as open items (no code touched, so no test run was needed beyond the existing green baseline).

## ⑥ Commit list

- (docs) `docs/review/2026-11-01-code-review.md` — this report
- (docs) `todo.md` — the five P3 findings recorded as open items
