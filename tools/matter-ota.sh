#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] pair <pairing-code>" >&2
  echo "       $0 [-d device] update [image]" >&2
  echo "pair: once. In Apple Home, open the purifier's settings, tap Turn On Pairing Mode and pass the code it shows." >&2
  echo "update: sends a Matter OTA image, by default the one from the last build. Its version must be higher than the running one." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
SHELL_PACKAGE="@matter/nodejs-shell@0.18.0"
export MATTER_STORAGE_PATH="$DEVICE_DIR/matter"
NODE_FILE="$MATTER_STORAGE_PATH/node-id"

SHELL_DIR="$ROOT/.matter-shell"
SHELL_APP="$SHELL_DIR/node_modules/@matter/nodejs-shell/dist/esm/app.js"
if [ ! -e "$SHELL_APP" ]; then
  command -v pnpm >/dev/null || { echo "pnpm not found. Install it: https://pnpm.io/installation" >&2; exit 1; }
  mkdir -p "$SHELL_DIR"
  pnpm add --silent --dir "$SHELL_DIR" "$SHELL_PACKAGE"
fi

WORK=$(mktemp -d)
LOG="$WORK/shell.log"
SHELL_PID=""
cleanup() {
  if [ -n "$SHELL_PID" ]; then
    echo exit >&3 2>/dev/null || true
    sleep 1
    kill "$SHELL_PID" 2>/dev/null || true
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

start_shell() {
  mkdir -p "$MATTER_STORAGE_PATH"
  chmod 700 "$MATTER_STORAGE_PATH"
  mkfifo "$WORK/in"
  node "$SHELL_APP" <"$WORK/in" >"$LOG" 2>&1 &
  SHELL_PID=$!
  exec 3>"$WORK/in"
  wait_for 'Started Node' 120 || fail "the matter.js shell did not start"
}

send() { printf '%s\n' "$1" >&3; }

wait_for() {
  waited=0
  while [ "$waited" -lt "$2" ]; do
    tail -n +"${3:-1}" "$LOG" | grep -q -E "$1" && return 0
    kill -0 "$SHELL_PID" 2>/dev/null || return 1
    sleep 1
    waited=$((waited + 1))
  done
  return 1
}

fail() {
  echo "$1. Shell output:" >&2
  tail -n 30 "$LOG" >&2
  exit 1
}

case "$1" in
  pair)
    [ -n "$2" ] || usage
    [ ! -e "$NODE_FILE" ] || { echo "Already paired as node $(cat "$NODE_FILE"). Remove $MATTER_STORAGE_PATH to pair again." >&2; exit 1; }
    CODE=$(printf '%s' "$2" | tr -d ' -')
    start_shell
    send "config ota-test-images set true"
    send "commission pair --pairing-code $CODE"
    wait_for 'Commissioned Node: [0-9]+|Error' 180 || fail "pairing timed out"
    NODE=$(sed -n -E 's/.*Commissioned Node: ([0-9]+).*/\1/p' "$LOG" | head -n 1)
    [ -n "$NODE" ] || fail "pairing failed"
    printf '%s\n' "$NODE" >"$NODE_FILE"
    echo "Paired as node $NODE. Controller data is in $MATTER_STORAGE_PATH; back it up with the rest of $DEVICE_DIR."
    ;;
  update)
    [ -e "$NODE_FILE" ] || { echo "Not paired yet. Run: $0 pair <pairing-code>" >&2; exit 1; }
    NODE=$(cat "$NODE_FILE")
    IMAGE="${2:-$FIRMWARE/levoit_matter-ota.bin}"
    [ -e "$IMAGE" ] || { echo "no image at $IMAGE. Build it first." >&2; exit 1; }
    IMAGE=$(cd "$(dirname "$IMAGE")" && pwd)/$(basename "$IMAGE")
    start_shell
    send "ota add file://$IMAGE"
    wait_for 'added to storage successfully|Error' 60 || fail "the image was not accepted"
    grep -q 'added to storage successfully' "$LOG" || fail "the image was not accepted"
    VERSION=$(sed -n -E 's/.*Software Version String: (.*)$/\1/p' "$LOG" | head -n 1)
    echo "Image $VERSION accepted."
    send "nodes connect $NODE 1 60"
    wait_for "Node $NODE connected" 120 || fail "could not reach the purifier"
    echo "Sending. This takes about a minute, then the purifier reboots."
    for attempt in 1 2 3; do
      START=$(($(wc -l <"$LOG") + 1))
      send "nodes ota apply $NODE --local"
      wait_for 'Update done for peer|Update failed for peer|not in a state|Error|"previousState":2,"newState":1' 600 "$START" || fail "no result after 10 minutes"
      if tail -n +"$START" "$LOG" | grep -q 'Update done for peer'; then
        echo "Updated to $VERSION."
        exit 0
      fi
      [ "$attempt" -lt 3 ] && echo "Not taken. The purifier refuses while its own last update is unconfirmed. Trying again in 20 seconds." && sleep 20
    done
    fail "the update did not complete"
    ;;
  *) usage ;;
esac
