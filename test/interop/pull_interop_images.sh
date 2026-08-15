#!/usr/bin/env bash
#
# Pull all QUIC implementation images used for interop comparison.
# The image list is extracted automatically from implementations.json
# (the locally-built "quicx" image is excluded).
#
# Usage:
#   ./pull_interop_images.sh                # pull everything
#   ./pull_interop_images.sh mvfst quic-go  # pull only the specified implementations
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMPL_JSON="${SCRIPT_DIR}/implementations.json"

if ! command -v docker >/dev/null 2>&1; then
  echo "Error: docker not found. Please install/start docker first." >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "Error: python3 not found (used to parse implementations.json)." >&2
  exit 1
fi

# Extract every image from implementations.json (in JSON order), excluding quicx.
mapfile -t ALL_IMAGES < <(python3 - "$IMPL_JSON" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
for name, info in data.items():
    if name == "quicx":
        continue   # quicx is built locally, not pulled from remote
    print(info["image"])
PY
)

# If implementation names are given on the command line, pull only the
# matching images (matched by substring against the image reference).
SELECTED=("${@:-${ALL_IMAGES[@]}}")
if [ "$#" -gt 0 ]; then
  SELECTED=()
  for want in "$@"; do
    matched=0
    for img in "${ALL_IMAGES[@]}"; do
      if [[ "$img" == *"$want"* ]]; then
        SELECTED+=("$img"); matched=1
      fi
    done
    if [ "$matched" -eq 0 ]; then
      echo "Warning: no image matches '$want' (available: ${ALL_IMAGES[*]})" >&2
    fi
  done
fi

if [ "${#SELECTED[@]}" -eq 0 ]; then
  echo "No images to pull." >&2
  exit 1
fi

echo "Pulling ${#SELECTED[@]} image(s):"
for img in "${SELECTED[@]}"; do echo "  - $img"; done
echo "----------------------------------------"

fail=0
for img in "${SELECTED[@]}"; do
  echo ">>> Pulling $img ..."
  if docker pull "$img"; then
    echo "<<< OK: $img"
  else
    echo "<<< FAILED: $img" >&2
    fail=1
  fi
  echo "----------------------------------------"
done

if [ "$fail" -ne 0 ]; then
  echo "Some images failed to pull. Check network/auth and retry." >&2
  exit 1
fi

echo "All images pulled successfully."
