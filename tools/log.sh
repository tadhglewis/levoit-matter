#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] [host]" >&2
  echo "Prints the purifier's recent log, fetched over Wi-Fi. The host is remembered after the first use." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
if is_host "$1"; then HOST="$1"; shift; else HOST=$(saved_host) || exit 1; fi
[ $# -eq 0 ] || usage
TOKEN=$(read_token)
curl --fail-with-body --show-error --silent --max-time 30 \
  -H "Authorization: Bearer $TOKEN" "http://$HOST/log"
remember_host "$HOST"
