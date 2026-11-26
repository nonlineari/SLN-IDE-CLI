# gitignore

Git reads `.gitignore` in this directory. This page is the same list, written out for the repository.

| Pattern | Left out because |
| --- | --- |
| `build/` | CMake and Ninja output, including `sln-ide`, `sln-pico`, and the tool logs |
| `CMakeCache.txt` | Generated cache if CMake is run in the source tree |
| `CMakeFiles/` | Generated CMake intermediates |
| `cmake_install.cmake` | Generated install script |
| `compile_commands.json` | Generated compilation database |
| `.cache/` | Tool caches |
| `*.o` | Stray object files |
| `*.a` | Stray static libraries |
| `esp-last.log` | Log from `tools/esp.sh` when it is written outside `build/` |
| `pico-last.log` | Log from `tools/pico.sh` when it is written outside `build/` |

Source files, `CMakeLists.txt`, `tools/esp.sh`, `tools/pico.sh`, `README.md`, and this page stay in the tree. The ESP-IDF firmware image lives in the sibling `ESPWROOM32_wifi` project, not in this repository.
