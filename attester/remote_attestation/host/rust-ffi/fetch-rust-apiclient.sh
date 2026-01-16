#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
DEST_DIR="${SCRIPT_DIR}/coserv-rs"
REPO_URL="https://github.com/veraison/rust-apiclient.git"
REV="8e1268e5011563c7955027033cf081c1228d268f"

if [[ -e "${DEST_DIR}" && ! -d "${DEST_DIR}/.git" ]]; then
    echo "Error: ${DEST_DIR} exists but is not a git repository." >&2
    exit 1
fi

if [[ ! -d "${DEST_DIR}/.git" ]]; then
    git clone "${REPO_URL}" "${DEST_DIR}"
else
    git -C "${DEST_DIR}" fetch --tags origin
fi

git -C "${DEST_DIR}" checkout "${REV}"
