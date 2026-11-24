#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] <host> <percent>" >&2
  echo "Sets the remaining filter life, 0 to 100. Use it to carry over a part-used filter." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
HOST="$1"
PERCENT="$2"
[ -n "$HOST" ] && [ -n "$PERCENT" ] || usage
TOKEN=$(read_token)
curl --fail-with-body --show-error --silent --max-time 30 \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: text/plain" \
  --data-binary "$PERCENT" "http://$HOST/filter"
