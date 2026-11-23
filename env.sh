BASE="$HOME/Library/Caches/esphome/idf"
export IDF_PATH="$BASE/frameworks/5.5.6"
export IDF_TOOLS_PATH="$BASE"
export IDF_PYTHON_ENV_PATH="$BASE/penvs/5.5.6"
export PATH="$BASE/penvs/5.5.6/bin:$BASE/tools/cmake/3.30.2/CMake.app/Contents/bin:$BASE/tools/ninja/1.12.1:$BASE/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$PATH"
idf() { "$BASE/penvs/5.5.6/bin/python" "$IDF_PATH/tools/idf.py" "$@"; }
