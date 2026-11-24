#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] [host] [image]" >&2
  echo "       $0 [-d device] [host] --set-token <new-token>" >&2
  echo "The host is remembered after the first use. The token comes from LEVOIT_OTA_TOKEN, else the device's secrets folder." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
if is_host "$1"; then HOST="$1"; shift; else HOST=$(saved_host) || exit 1; fi
TOKEN=$(read_token)

if [ "$1" = "--set-token" ]; then
  [ -n "$2" ] || usage
  curl --fail-with-body --show-error --max-time 30 \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: text/plain" \
    --data-binary "$2" "http://$HOST/ota/token"
  if [ -d "$SECRETS" ]; then
    printf '%s\n' "$2" > "$SECRETS/ota-token.current"
    chmod 600 "$SECRETS/ota-token.current"
  fi
  remember_host "$HOST"
  exit 0
fi

IMAGE="${1:-$FIRMWARE/levoit_matter.bin}"
[ -e "$IMAGE" ] || { echo "no image at $IMAGE. Build it first." >&2; exit 1; }
curl --fail-with-body --show-error --max-time 600 \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$IMAGE" "http://$HOST/ota"
remember_host "$HOST"
