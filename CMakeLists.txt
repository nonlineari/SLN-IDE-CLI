cmake_minimum_required(VERSION 3.16)
project(sln-ide C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Narrow feature set plus glibc defaults (getgrouplist) and wide ncurses.
add_compile_definitions(_DEFAULT_SOURCE NCURSES_WIDECHAR=1)
add_compile_options(-Wall -Wextra -Werror)

set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

set(SLN_IDE_SOURCES
    src/main.c
    src/ui.c
    src/panes/heatsink.c
    src/panes/process.c
    src/panes/security.c
    src/panes/tree.c
    src/panes/files.c
    src/panes/cim.c
    src/panes/esp32.c
    src/panes/url.c
    src/panes/cmdline.c
)

add_executable(sln-ide ${SLN_IDE_SOURCES})
target_include_directories(sln-ide PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CURSES_INCLUDE_DIRS}
)
target_link_libraries(sln-ide PRIVATE ${CURSES_LIBRARIES})

add_executable(sln-pico src/pico.c)
target_include_directories(sln-pico PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(sln-pico PRIVATE ${CURSES_LIBRARIES})

# Parent of this repository, resolved when CMake runs.
get_filename_component(SLN_PARENT "${CMAKE_SOURCE_DIR}/.." REALPATH)

target_compile_definitions(sln-ide PRIVATE
    "SLN_ESP_SH=\"${CMAKE_SOURCE_DIR}/tools/esp.sh\""
    "SLN_TREE_ROOT=\"${SLN_PARENT}\""
    "SLN_FW_BIN=\"${SLN_PARENT}/ESPWROOM32_wifi/firmware/build/noiseprotocol_classic.bin\""
    "SLN_ESP_LOG=\"${CMAKE_BINARY_DIR}/esp-last.log\""
)

target_compile_definitions(sln-pico PRIVATE
    "SLN_PICO_SH=\"${CMAKE_SOURCE_DIR}/tools/pico.sh\""
    "SLN_PICO_LOG=\"${CMAKE_BINARY_DIR}/pico-last.log\""
)
