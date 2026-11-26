# SLN-IDE-CLI

Konsole dashboard for the local Linux account. Nine panes show temperatures, processes, files, a directory tree, a small read-only catalog, the serial port, one URL, and the ESP32 firmware build. The panes run on the computer. The ESP32 receives the existing Noise firmware through `idf.py`.

Each pane is one C file with `sample`, `draw`, and `key`. `src/main.c` lists those objects in one table and the core calls them. Adding a pane means adding the file to `CMakeLists.txt` and one line in that table.

The program reads the account that starts it. It does not create users. It uses whichever account starts the program.

## Requirements

- CMake 3.16 or newer, Ninja, and GCC
- ncurses wide (`ncursesw`)
- A terminal such as Konsole
- For the ESP32 actions: ESP-IDF at `~/esp/esp-idf`, and the firmware tree next to this repo at `../ESPWROOM32_wifi/firmware`
- For the URL action: `xdg-open` and `curl`

## Build

From this directory:

```sh
cmake -G Ninja -S . -B build
ninja -C build
./build/sln-ide
```

The tree root is the parent of this repository, resolved when CMake runs, so the path follows the directory that contains this repository. `./build/sln-ide --sample` prints one text frame of every pane.

## Keys

`Tab` and `Shift-Tab` move between panes. `j` / `k` or the arrow keys move inside a pane. `Enter` runs that pane's action. `q` quits.

In the tree, `h` and `l` collapse and expand. Directories named `build` are skipped. The files pane stats the selected path.

In the URL pane, `i` edits the address, `Esc` stops editing, and `Enter` runs the selected action. The address starts as `http://localhost`.

## Panes

| Pane | What it shows |
| --- | --- |
| heatsink | hwmon temperatures and the fan `cooling_device` state |
| process | CPU percent, load, memory, and the busiest tasks |
| security | Local user and groups, `/dev/ttyUSB0`, the USB serial identity, listening TCP ports |
| tree | Parent directory of this repo |
| files | `stat` of the tree selection: mode, owner, size, mtime |
| cim | `ComputerSystem`, `Processor`, `TemperatureSensor`, `LogicalFile`, `USBDevice`, `ESP32Board` |
| esp32 | `build`, `flash`, or `monitor` for the Noise firmware |
| url | One `http` or `https` address, opened outside the terminal |
| cmdline | The last action, its exit code, and the ESP32 log tail |

The catalog is a read-only Common Information Model shape taken from the same local files. The security pane does not scan hosts or change firewall rules. The process pane does not stop tasks. Keystrokes are not passed to a shell.

## ESP32

`tools/esp.sh` is the ESP32 script. The dashboard execs it with one fixed argument.

- `build` sources `~/esp/esp-idf/export.sh` and runs `idf.py build` in the sibling firmware directory.
- `flash` and `monitor` use `/dev/ttyUSB0`. If that port is not readable and writable they print `cannot open /dev/ttyUSB0 (group uucp required)` and return before `idf.py` starts.

The firmware project is not part of this repository.

## Pico 2

`sln-pico` is a second window and a different tool from the nine-pane dashboard. It watches a Raspberry Pi Pico 2 in BOOTSEL, which appears as USB `2e8a:000f` `RP2350 Boot` and a read-only volume. `j` / `k` select `info` or `volume`. `Enter` runs `tools/pico.sh` with that one argument. `q` quits.

```sh
./build/sln-pico
```

`info` prints the USB identity and `INFO_UF2.TXT`. `volume` lists the file names on the boot volume. The script does not copy a UF2 onto the board.

## URL

`Enter` on **open in browser** runs `xdg-open` with the address as one argument. The page opens in the desktop browser. **curl -I** requests headers with a five-second limit. The terminal does not render the page. The address must be `http` or `https`, with a host, and without shell metacharacters.

## Layout

```text
CMakeLists.txt
README.md
gitignore.md
.gitignore
src/main.c
src/inject.h
src/ui.c
src/panes/*.c
src/pico.c
tools/esp.sh
tools/pico.sh
```
