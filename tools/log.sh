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
if [ -n "$LEVOIT_OTA_TOKEN" ]; then
  TOKEN="$LEVOIT_OTA_TOKEN"
elif [ -e "$SECRETS/ota-token.current" ]; then
  TOKEN=$(cat "$SECRETS/ota-token.current")
elif [ -e "$SECRETS/ota-token.txt" ]; then
  TOKEN=$(cat "$SECRETS/ota-token.txt")
else
  echo "no update token for '$DEVICE' in $SECRETS" >&2
  exit 1
fi
curl --fail-with-body --show-error --silent --max-time 30 \
  -H "Authorization: Bearer $TOKEN" "http://$HOST/log"
