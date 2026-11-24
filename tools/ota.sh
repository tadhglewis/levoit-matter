#!/bin/sh
set -e
FACTORY="$(dirname "$0")/../factory"
usage() {
  echo "usage: $0 <host> [image]" >&2
  echo "       $0 <host> --set-token <new-token>" >&2
  echo "The token comes from LEVOIT_OTA_TOKEN, else factory/ota_token.current, else factory/ota_token.txt." >&2
  exit 1
}
[ $# -ge 1 ] || usage
HOST="$1"
if [ -n "$LEVOIT_OTA_TOKEN" ]; then
  TOKEN="$LEVOIT_OTA_TOKEN"
elif [ -e "$FACTORY/ota_token.current" ]; then
  TOKEN=$(cat "$FACTORY/ota_token.current")
elif [ -e "$FACTORY/ota_token.txt" ]; then
  TOKEN=$(cat "$FACTORY/ota_token.txt")
else
  echo "no update token found" >&2
  usage
fi

if [ "$2" = "--set-token" ]; then
  [ -n "$3" ] || usage
  curl --fail-with-body --show-error --max-time 30 \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: text/plain" \
    --data-binary "$3" "http://$HOST/ota/token"
  if [ -d "$FACTORY" ]; then
    printf '%s\n' "$3" > "$FACTORY/ota_token.current"
    chmod 600 "$FACTORY/ota_token.current"
  fi
  exit 0
fi

IMAGE="${2:-$(dirname "$0")/../build/levoit_matter.bin}"
curl --fail-with-body --show-error --max-time 600 \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$IMAGE" "http://$HOST/ota"
