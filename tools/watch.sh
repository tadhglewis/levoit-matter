#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] [host] [filter]" >&2
  echo "Follows the purifier's log over Wi-Fi. Stop with Ctrl-C." >&2
  echo "The optional filter is a regular expression, for example 'link|matter|purifier'." >&2
  echo "The host is remembered after the first use." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
if is_host "$1"; then HOST="$1"; shift; else HOST=$(saved_host) || exit 1; fi
TOKEN=$(read_token)
remember_host "$HOST"
export LEVOIT_WATCH_TOKEN="$TOKEN"
exec python3 - "$HOST" "${1:-}" <<'PY'
import os, re, sys, time, urllib.request

host, pattern = sys.argv[1], sys.argv[2]
keep = re.compile(pattern) if pattern else None
request = urllib.request.Request(
    f"http://{host}/log", headers={"Authorization": "Bearer " + os.environ["LEVOIT_WATCH_TOKEN"]})
seen = []
offline = False

def show(line):
    if keep is None or keep.search(line):
        print(line, flush=True)

while True:
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            lines = response.read().decode("utf-8", "replace").splitlines()[1:]
        if offline:
            print("--- back online ---", flush=True)
            offline = False
    except KeyboardInterrupt:
        break
    except Exception as error:
        if not offline:
            print(f"--- unreachable: {error} ---", flush=True)
            offline = True
        time.sleep(2)
        continue

    start = 0
    if seen:
        last = seen[-1]
        positions = [i for i, line in enumerate(lines) if line == last]
        match = next((i for i in reversed(positions)
                      if lines[max(0, i - 2):i + 1] == seen[-min(3, i + 1):]), None)
        if match is None:
            print("--- rebooted, or lines were missed ---", flush=True)
        else:
            start = match + 1
    for line in lines[start:]:
        show(line)
    if lines:
        seen = lines
    try:
        time.sleep(2)
    except KeyboardInterrupt:
        break
PY
