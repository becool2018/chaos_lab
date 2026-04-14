#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ME="$(bash "${SCRIPT_DIR}/resolve-message-engine.sh")"
# Upstream messageEngine no longer ships a libmessageengine.a target; pack the
# core + platform objects the same way the old static library did.
make -C "${ME}" server
mkdir -p "${ME}/build"
rm -f "${ME}/build/libmessageengine.a"
ar crs "${ME}/build/libmessageengine.a" "${ME}/build/objs/core/"*.o "${ME}/build/objs/platform/"*.o
