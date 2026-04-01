# Integration Test Status

## What Is Verified

The repository currently has two tested layers:

- Unit tests through `ctest --test-dir build --output-on-failure`
- A buildable integration test target in `tests/integration`

The integration executable currently guarantees a local smoke check:

- protobuf encode/decode
- gateway packet framing
- linkage against project libraries

It also has an optional live connection path:

- opens a TCP connection to the gateway
- sends `LOGIN_REQ`
- expects a successful `LOGIN_RESP`

That path is not exercised by default. You can provide services in two ways:

- Docker with `--docker --connect`
- local binaries with `--local-services`

## Supported Commands

Local smoke:

```bash
cmake -S . -B build
cmake --build build -j4
bash tests/run_integration_tests.sh
```

Connection smoke against live services:

```bash
docker compose up -d redis auth gateway chat social
bash tests/run_integration_tests.sh --docker --connect
```

Connection smoke with local binaries:

```bash
cmake -S . -B build
cmake --build build -j4 --target chirp_auth chirp_gateway
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## Script Behavior

`tests/run_integration_tests.sh` now defaults to the least surprising behavior:

- it reuses the main project build
- it uses system dependencies when they already work
- it does not force `vcpkg install`
- it accepts `--use-vcpkg` when dependency bootstrapping is actually desired
- it can orchestrate a local auth/gateway smoke path without Docker

## Scope Notes

The integration framework should currently be read as smoke coverage, not comprehensive end-to-end coverage for every service.

What is present today:

- `tests/integration/integration_test.cc` builds and runs
- `tests/integration/CMakeLists.txt` links against the repo libraries
- `tests/integration/demo_test_framework.sh` documents the real entrypoints

What still needs deeper validation over time:

- chat delivery flows against live services
- social/presence flows against live services
- newer service surfaces such as notification and search
- SDK behavior beyond successful compilation
