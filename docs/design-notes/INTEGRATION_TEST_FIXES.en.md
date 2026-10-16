> 🌐 Language: [中文](./INTEGRATION_TEST_FIXES.md) | **English** (this page)

# Integration Test Fix Summary

## Overview

The integration test setup was stabilized around the current state of the repository:

- The integration target builds cleanly
- The default test path runs a protobuf/frame-format smoke check once
- The optional connection path performs a real gateway login smoke
- The wrapper script can now bring up the local `auth` and `gateway` binaries directly, without Docker

## Current behavior

`tests/integration/integration_test.cc` now provides:

1. `Protobuf Encoding`
2. `Basic Connection` when `--connect` is passed

The connection test:

- Opens a TCP connection to the gateway
- Sends `LOGIN_REQ`
- Waits for `LOGIN_RESP`
- Exits with a non-zero code on failure

## Current entry points

Default smoke:

```bash
bash tests/run_integration_tests.sh
```

Docker-backed connection smoke:

```bash
bash tests/run_integration_tests.sh --docker --connect
```

Connection smoke with local binaries:

```bash
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## Script changes

`tests/run_integration_tests.sh` now:

- Prefers reusing system dependencies that already work
- Does not force `vcpkg install`
- Accepts `--use-vcpkg` when that behavior is explicitly wanted
- Can start Docker services
- Can start local `chirp_auth` and `chirp_gateway`
- Cleans up local child processes on exit

## Notes

This is still smoke coverage, not a complete service-to-service verification of every chat, social and voice flow.
