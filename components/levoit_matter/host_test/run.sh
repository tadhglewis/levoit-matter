#!/bin/sh
# Host tests for the Matter bridge. test_logic needs only our own sources;
# test_core_bridge drives tuct's real protocol core, which is not vendored here
# any more (it is an IDF dependency). Fetch it at the pinned commit from
# idf_component.yml, or point LEVOIT_CORE_DIR at an existing checkout.
set -e
cd "$(dirname "$0")"
mkdir -p build

c++ -std=c++17 -Wall -Wextra -Werror -I ../include \
  test_logic.cpp ../src/levoit_frame.cpp ../src/purifier_map.cpp ../src/purifier_model.cpp \
  -o build/test_logic
./build/test_logic

if [ -n "$LEVOIT_CORE_DIR" ]; then
  CORE="$LEVOIT_CORE_DIR/components/levoit"
else
  PIN=$(awk '/^  levoit:/{f=1} f&&/version:/{gsub(/[" ]/,"",$2);print $2;exit}' ../idf_component.yml)
  [ -n "$PIN" ] || { echo "no core pin in idf_component.yml" >&2; exit 1; }
  CORE=build/levoit-core
  if [ ! -f "$CORE/components/levoit/levoit_device.cpp" ]; then
    rm -rf "$CORE"
    git -c advice.detachedHead=false clone --quiet --filter=blob:none https://github.com/tuct/levoit.git "$CORE"
    git -C "$CORE" checkout --quiet "$PIN"
  fi
  CORE="$CORE/components/levoit"
fi

for f in levoit_device decoder core_status vital_status sprout_status superior_status \
  core_commands vital_commands sprout_commands superior_commands tlv levoit_message; do
  c++ -std=c++17 -DLEVOIT_HOST_QUIET -c "$CORE/$f.cpp" -o "build/core_$f.o"
done
c++ -std=c++17 -Wall -Wextra -Werror -DLEVOIT_HOST_QUIET -I ../include -iquote "$CORE" \
  test_core_bridge.cpp ../src/core_bridge.cpp ../src/levoit_frame.cpp ../src/purifier_map.cpp \
  ../src/purifier_model.cpp build/core_*.o -o build/test_core_bridge
./build/test_core_bridge
