#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
if ME="$(bash "${SCRIPT_DIR}/resolve-message-engine.sh" 2>/dev/null)"; then
  export MESSAGE_ENGINE="${ME}"
  bash "${SCRIPT_DIR}/build-messageengine-lib.sh"
  make -C "${REPO_ROOT}/sidecar" MESSAGE_ENGINE="${ME}"
else
  # Fallback: build against vendored third_party/messageengine snapshot.
  make -C "${REPO_ROOT}/sidecar"
fi
