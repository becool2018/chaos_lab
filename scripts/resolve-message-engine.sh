#!/usr/bin/env bash
# Resolve absolute path to messageEngine checkout for Chaos Lab builds.
# Prefers $MESSAGE_ENGINE if set and valid; else chaos-lab/messageEngine; else sibling ../messageEngine.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ -n "${MESSAGE_ENGINE:-}" ]]; then
  if [[ ! -d "${MESSAGE_ENGINE}" ]]; then
    echo "MESSAGE_ENGINE is set but not a directory: ${MESSAGE_ENGINE}" >&2
    exit 1
  fi
  cd "${MESSAGE_ENGINE}" && pwd
  exit 0
fi

if [[ -d "${REPO_ROOT}/messageEngine" ]]; then
  cd "${REPO_ROOT}/messageEngine" && pwd
  exit 0
fi

PARENT_ME="$(cd "${REPO_ROOT}/.." && pwd)/messageEngine"
if [[ -d "${PARENT_ME}" ]]; then
  cd "${PARENT_ME}" && pwd
  exit 0
fi

echo "messageEngine not found. Clone it next to chaos-lab or under chaos-lab/messageEngine, or set MESSAGE_ENGINE." >&2
exit 1
