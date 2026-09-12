# Integration Test Fixes Summary

## Overview

The integration test setup was stabilized around the current repository reality:

- the integration target builds cleanly
- the default test path runs a protobuf/framing smoke check
- the optional connection path performs a real gateway login smoke
- the wrapper script can now start local `auth` and `gateway` binaries without Docker

## Current Behavior

`tests/integration/integration_test.cc` now provides:

1. `Protobuf Encoding`
2. `Basic Connection` when `--connect` is supplied

The connection test:

- opens a TCP connection to the gateway
- sends `LOGIN_REQ`
- waits for `LOGIN_RESP`
- exits non-zero on failure

## Current Entry Points

Default smoke:

```bash
bash tests/run_integration_tests.sh
```

Docker-backed connection smoke:

```bash
bash tests/run_integration_tests.sh --docker --connect
```

Local binary connection smoke:

```bash
bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500
```

## Script Changes

`tests/run_integration_tests.sh` now:

- prefers already-working system dependencies
- does not force `vcpkg install`
- accepts `--use-vcpkg` when that behavior is explicitly wanted
- can start Docker services
- can start local `chirp_auth` and `chirp_gateway`
- cleans up local child processes on exit

## Notes

This is still smoke coverage, not a full service-to-service validation of all chat, social, or voice flows.
