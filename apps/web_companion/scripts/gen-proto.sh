#!/bin/bash
# Regenerate proto/ts for the web companion. Thin wrapper around the repo-root
# script so `npm run gen:proto` works from this directory.
set -euo pipefail
cd "$(dirname "$0")/../../.."
bash gen_proto.sh
