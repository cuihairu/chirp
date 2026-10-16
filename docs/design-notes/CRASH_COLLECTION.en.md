> 🌐 Language: [中文](./CRASH_COLLECTION.md) | **English** (this page)

# Crash collection (Crashpad) design brief

> Brief of 2026-10-08 (design before implementation). The choice is pinned: **Google Crashpad**, integrated directly in C++;
> Breakpad is kept as a historical reference only and is not brought in (its on-process handler and build chain are a previous-generation solution).

## Goals and boundaries

- A process crash (SIGSEGV/SIGABRT/…) writes a minidump to local disk, and symbolication restores the call stack.
- **Local disk first, no outbound upload**: uploading is an outward data flow, off by default (the reserved slot is below).
- No new monitoring system: hook into the existing logging surface (`libs/common/logger`); a Warn-level structured line is the integration point.

## Integration approach (CMake)

- **FetchContent with a pinned fixed version**, not a submodule: the same approach as the existing abseil/asio integration in the root `CMakeLists.txt` (gated by `CHIRP_FETCH_DEPS`), sparing every CI workflow the
  `submodules: checkout` change surface.
  - Repository: `https://github.com/backtrace-labs/crashpad.git`
  - Pinned to **tag `v0.3.0`** (the backtrace branch, tag commit `3f7fee9e54…`, a fork still maintained in 2026; it ships a complete per-directory CMake build — `client`/`handler`/`tools`/`compat`/`snapshot`/`minidump` are each a target, with no community wrapper repo needed).
- New switch `CHIRP_ENABLE_CRASHPAD` (default `ON`; actually fetched only on Linux with `CHIRP_FETCH_DEPS=ON`; when off or when fetching fails, `crash::Initialize` degrades to a no-op stub returning `false`, and all services compile and run as usual).
- **Warning and instrumentation isolation**: the root `add_compile_options` (`-Wall -Wextra -Wpedantic` `-Werror` (CI) plus coverage `-ftest-coverage`) would flow into third-party sources via directory-property inheritance. Before integration, temporarily clear the root `COMPILE_OPTIONS` and restore them after `FetchContent_MakeAvailable`, ensuring: ① CI's `-Werror` is not knocked over by 2010s-style third-party code; ② the coverage build does not instrument crashpad (its sources are outside the gate universe and would only waste time).

## Handler initialization point

- Location: **the first statement of every service's `main()`** — `chirp::common::crash::Initialize(argc, argv)` — before any flag parsing or logger initialization, so startup-phase crashes are caught too. The return value is ignored (a failure degrades to no crash collection + one Warn log line).
- What initialization does (`libs/common/crash_handler.{h,cc}`):
  1. Parse the crash directory and the upload URL (see below);
  2. Scan for dumps left over from the previous session → emit a Warn log (the log/monitoring integration surface);
  3. `CrashpadClient::StartHandler(handler path, database, database, url, annotations, arguments, /*restartable*/ true, /*asynchronous_start*/ false)`.
- Service coverage: all `services/**/main*.cc` (the probe `tools/crash_probe` calls the same entry). The SDK examples (`sdks/core/examples`) are out of scope for this batch.

## Dump directory

Lookup order (first hit wins):

1. `--crash_dir=DIR` (a per-argument peek over argv; does not take over each service's own flag parsing);
2. the environment variable `CHIRP_CRASH_DIR`;
3. the default `./crash_dumps` (the process cwd).

If the directory does not exist it is created (mkdir -p semantics). The directory contents follow crashpad's own database layout (`new/` `pending/` `completed/` `last_upload_attempt`), which is **not managed by hand** and does not go into git (`.gitignore` gains `crash_dumps/`).

## The standalone handler process and packaging

- `crashpad_handler` is a standalone executable, launched out of process by the client's `StartHandler` (if the handler crashes, the main process is not implicated; the handler must be alive when the main process crashes, or nothing is written).
- Runtime lookup order:
  1. the environment variable `CHIRP_CRASH_HANDLER` (ops override);
  2. **the same directory** as the calling process (the flat layout of an install/release package);
  3. **up at most 4 parent levels** from the exe directory (build tree: `build/services/<group>/<service>/` → `build/`).
- Artifact surface: the CMake build finish copies the handler as a **build-root artifact** (`build/crashpad_handler`), at the same level as the `build/` layout used by `test_services.sh`.
- Install/nightly packaging surface (landed): the nightly Linux leg's build step adds the handler copy target (`chirp_crashpad_handler_copy`), the assembly step lays `build/crashpad_handler` flat into `cpp/<arch>/bin/`, the package job carries it into the tarball via the existing `bin/` directory probing; `install.sh --component cpp` lays the package's `bin/` flat into `$PREFIX/bin`. The darwin/windows legs have crashpad off (platform gate), and those packages honestly contain no handler (`install.ps1` has the note). The post-install lookup order has been measured: the probe with the handler in the same directory, and one level up from a subdirectory, both hit `$PREFIX/bin/crashpad_handler`.

## Symbol management

- **Keep `-g` throughout the build**: the dev/coverage presets are Debug and carry it naturally; ci/minimal are Release and rely on the symbol-separation flow:
  1. `scripts/extract_symbols.sh <binary> [symbols_dir]`: `objcopy --only-keep-debug` (separated symbol archive) → `dump_syms <bin>` generates the breakpad `.sym` → laid out per the symbol-server convention `<name>/<debug-id>/<name>.sym`; **the original binary is untouched** — run `strip --strip-debug` separately when the release surface needs slimming.
- **Restoring stacks**: `minidump-stackwalk <dump> --symbols-path symbols/`, outputting a call stack with `file:line` and function names.
- Tool dependencies (declared as-is, not committed): `dump_syms` and `minidump-stackwalk` are both from the rust-minidump suite, installed with `cargo install dump_syms minidump-stackwalk`. (The Breakpad-semantics `.sym` format is the common intermediate format of both; no Breakpad source is introduced.)
- **Nightly symbol package archiving (landed)**: on the Linux leg, after `cargo install dump_syms` (cargo provided by the existing Setup Rust step), run `extract_symbols.sh` against `libchirp_core_sdk.so` and `crashpad_handler` to produce `symbols/<arch>/chirp-cpp-symbols-<arch>.tar.gz` as a **separate artifact** (not in the main install package), archived alongside the nightly-dist branch mirror and the rolling Release, with the manifest listing a separate `symbols` component; static libraries have no runtime symbol surface and produce nothing.

## Reserved upload slot (off by default)

- The URL argument of `StartHandler`: **empty string = purely local disk, nothing sent out** (crashpad does not start the uploader thread).
- Reserved slot: the `CHIRP_CRASH_UPLOAD_URL` environment variable (or the `--crash_upload_url=` peek). **Empty by default = off**; only when non-empty is the URL passed to the handler to enable its built-in upload surface. Turning it on is an outbound-data decision that needs separate evaluation and does not come on by default in this batch.

## Integration with existing logging/monitoring

- `Initialize` scans the database: if the previous session left unprocessed dumps, it emits a Warn (`crash report pending: <path>`) — the deployment side's existing log collection (log-to-file/stdout) is the integration point; no new monitoring surface is opened.
- Annotations injected into every dump: `binary` (argv0), `pid`, `cmd` (the truncated command line), `crash_dir` — so the crash scene and the log lines index each other.

## Acceptance (deliberate-crash test)

1. New `tools/crash_probe`: initialize crashpad → print a ready message → the `--crash_probe_segfault` switch triggers a null-pointer write;
2. Run → the dump lands in the specified `--crash_dir`, and the database retains a record after exit;
3. `dump_syms` generates the .sym → `minidump-stackwalk` restores a call stack **containing `crash_probe` frames with `file:line`**; keep the log/output as acceptance evidence.

## Coverage and gate reconciliation

- `libs/common/crash_handler.cc` is inside the gate universe (`libs/**`): the testable surface — directory lookup order (argv/env/default), directory creation, URL off by default, database scan producing a Warn, and failure degradation when the handler is missing — is fully covered by unit tests.
- **The coverage build (`CHIRP_ENABLE_COVERAGE=ON`) force-disables crashpad**: the third-party sources are neither in the universe (gcov aggregation only recognizes the `libs/` `services/` `sdks/core/src` prefixes) nor instrumented (saves time); the `StartHandler` branch is then not compiled at all (`CHIRP_HAVE_CRASHPAD` undefined), with no uncovered lines and no exemption pins needed — `Initialize` compiles as a stub that takes the short path (create directory + leftover Warn + `return false`), which unit tests assert safely. E2E responsibility for actually launching the handler belongs to the `crash_probe` acceptance leg.
- The crashpad headers included by this repo's sources carry GCC extensions (`#include_next`); `crash_handler.cc` locally suppresses `-Wpedantic` at the include point, while its own code is still bound by `-Wall -Wextra -Wpedantic` (CI adds `-Werror`).
- `sdks/core`'s `libchirp_core_sdk.so` links the crashpad static library: a FetchContent-scoped global `CMAKE_POSITION_INDEPENDENT_CODE=ON`.

## Out of scope (as-is)

- Crash **collection** currently takes effect only on Linux: `CHIRP_ENABLE_CRASHPAD` is gated to Linux by the platform check (consistent with the repo's CI), and the darwin/windows packages honestly contain no handler. But `libs/common/crash_handler.*` itself compiles cross-platform (exe-directory probing branches on `_WIN32` to `GetModuleFileNameA`, pid uses `_getpid`); on non-Linux platforms it is a degraded stub without collection and does not block the SDK build.
- Upload endpoint, multi-machine centralized retrieval, alert integration: the outbound surface stays off by default, to be evaluated in a separate batch.
