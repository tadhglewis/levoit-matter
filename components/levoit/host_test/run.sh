#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -I ../include \
  test_logic.cpp ../src/levoit_frame.cpp ../src/levoit_core.cpp \
  ../src/purifier_map.cpp ../src/filter_life.cpp -o build/test_logic
./build/test_logic
