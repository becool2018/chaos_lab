#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ME="$(bash "${SCRIPT_DIR}/resolve-message-engine.sh")"
VENDOR_ROOT="${REPO_ROOT}/third_party/messageengine"

bash "${SCRIPT_DIR}/build-messageengine-lib.sh"

mkdir -p "${VENDOR_ROOT}/build"
rm -rf "${VENDOR_ROOT}/src"
cp -R "${ME}/src" "${VENDOR_ROOT}/src"
cp "${ME}/build/libmessageengine.a" "${VENDOR_ROOT}/build/libmessageengine.a"

echo "Vendored messageEngine snapshot updated at ${VENDOR_ROOT}"
