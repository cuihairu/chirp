# Chirp Project Status

## Current Snapshot

This repository builds successfully with the checked-in sources and currently validates the following out of the box:

- `cmake -S . -B build`
- `cmake --build build -j4`
- `ctest --test-dir build --output-on-failure`

As of 2026-04-01 in this workspace:

- Root build configuration is healthy.
- Unit tests pass (`common_tests`, `network_tests`).
- The integration test target builds and provides a local protobuf/framing smoke check.
- The integration test target also supports a real gateway login smoke path.
- Service and SDK directories contain a mix of working components and scaffolded or partially wired features.

## What This File Means

This is a status snapshot, not a claim that every roadmap item is production-ready.

The repository includes code for:

- Gateway, auth, chat, social, voice, notification, and search services
- Core C++ SDK plus Unity and Unreal bridges
- CLI client, load tester, admin dashboard, and mobile companion app skeletons
- Redis/MySQL-oriented chat archival tooling

Those areas are present in the tree, but they are not all covered by the same level of automated verification today.

## Verified Today

- Configure: `cmake -S . -B build`
- Build: `cmake --build build -j4`
- Unit tests: `ctest --test-dir build --output-on-failure`

## Integration Testing

`tests/run_integration_tests.sh` is the supported entrypoint.

Default behavior:

- Reuses the main project build when available
- Uses system dependencies if they already work
- Avoids forcing a full `vcpkg install`
- Builds `tests/integration`
- Runs the local integration smoke test

Optional behavior:

- `bash tests/run_integration_tests.sh --docker --connect`
- Starts Docker services and runs the connection-oriented smoke path
- `bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500`
- Starts local `chirp_auth` and `chirp_gateway`, runs the same login smoke, then cleans them up

## Remaining Work

The main gap is verification depth, not repository shape:

- More end-to-end coverage is still needed for the newer services and SDK surfaces.
- Several summary documents in the repo were previously over-optimistic and should be treated as historical notes unless they match the code and tests.
