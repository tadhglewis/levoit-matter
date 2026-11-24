#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] <host> [image]" >&2
  echo "       $0 [-d device] <host> --set-token <new-token>" >&2
  echo "The token comes from LEVOIT_OTA_TOKEN, else the device's secrets folder." >&2
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

if [ "$2" = "--set-token" ]; then
  [ -n "$3" ] || usage
  curl --fail-with-body --show-error --max-time 30 \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: text/plain" \
    --data-binary "$3" "http://$HOST/ota/token"
  if [ -d "$SECRETS" ]; then
    printf '%s\n' "$3" > "$SECRETS/ota-token.current"
    chmod 600 "$SECRETS/ota-token.current"
  fi
  exit 0
fi

IMAGE="${2:-$FIRMWARE/levoit_matter.bin}"
curl --fail-with-body --show-error --max-time 600 \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$IMAGE" "http://$HOST/ota"
