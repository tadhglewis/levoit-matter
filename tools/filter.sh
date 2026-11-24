#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] [host] <percent>" >&2
  echo "Sets the remaining filter life, 0 to 100. Use it to carry over a part-used filter." >&2
  echo "The host is remembered after the first use." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
if is_host "$1"; then HOST="$1"; shift; else HOST=$(saved_host) || exit 1; fi
PERCENT="$1"
[ -n "$PERCENT" ] && [ $# -eq 1 ] || usage
TOKEN=$(read_token)
curl --fail-with-body --show-error --silent --max-time 30 \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: text/plain" \
  --data-binary "$PERCENT" "http://$HOST/filter"
remember_host "$HOST"
