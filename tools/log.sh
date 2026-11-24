#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] <host>" >&2
  echo "Prints the purifier's recent log, fetched over Wi-Fi." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
HOST="$1"
[ -n "$HOST" ] || usage
TOKEN=$(read_token)
curl --fail-with-body --show-error --silent --max-time 30 \
  -H "Authorization: Bearer $TOKEN" "http://$HOST/log"
