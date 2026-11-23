#!/bin/sh
set -e
if [ $# -lt 2 ]; then
  echo "usage: $0 <host> <token> [image]" >&2
  exit 1
fi
HOST="$1"
TOKEN="$2"
IMAGE="${3:-$(dirname "$0")/../build/levoit_matter.bin}"
curl --fail --show-error --max-time 600 \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$IMAGE" "http://$HOST/ota"
