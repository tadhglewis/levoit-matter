#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -I ../include \
  test_logic.cpp ../src/levoit_frame.cpp ../src/levoit_core.cpp \
  ../src/purifier_map.cpp ../src/filter_life.cpp -o build/test_logic
./build/test_logic

CORE=../../../third_party/levoit/components/levoit
for f in levoit_device decoder core_status vital_status sprout_status superior_status \
  core_commands vital_commands sprout_commands superior_commands tlv levoit_message; do
  c++ -std=c++17 -DLEVOIT_HOST_QUIET -c "$CORE/$f.cpp" -o "build/core_$f.o"
done
c++ -std=c++17 -Wall -Wextra -Werror -DLEVOIT_HOST_QUIET -I ../include -I ../../levoit_core/include \
  test_core_bridge.cpp ../src/core_bridge.cpp ../src/levoit_frame.cpp ../src/levoit_core.cpp \
  ../src/purifier_map.cpp ../src/filter_life.cpp build/core_*.o -o build/test_core_bridge
./build/test_core_bridge
