# ROOM integration

DOOR links **[ROOM](https://github.com/IHP-GmbH/Room)** ([HTML API docs](https://ihp-gmbh.github.io/Room/)) for `.room` views and format converters. ROOM is a **public** repository (`IHP-GmbH/Room`). At configure time DOOR probes GitHub access to that repo; if reachable, ROOM is enabled, otherwise stubs are used (`LIBMAN_NO_ROOM`).

## Default behaviour

**qmake** — `core_build_config.pri` runs `scripts/probe_core_access.sh` (or `.cmd` on Windows):

```bash
mkdir -p build && cd build
qmake ../door.pro
make -j1 capnp_install
make -j1 lstream_schemas
make -j"$(nproc)"
```

Without access you will see: `DOOR: building without ROOM (CommonDB not available)`.

**CMake** — same probe via `cmake/ProbeCoreAccess.cmake`:

```bash
cmake -B build
cmake --build build -j
```

### Enable ROOM access

| Method | When |
|--------|------|
| *(default)* | Public clone of `IHP-GmbH/Room` — no token required |
| `export LIBMAN_CORE_GIT_TOKEN=ghp_...` | Optional PAT (rate limits / private forks) |
| `export GITHUB_TOKEN=...` | Same (fallback env var) |
| Clone to `.deps/Room` | Local checkout (no probe needed) |
| `LIBMAN_ROOM_SOURCE_DIR=/path/to/Room` | Side-by-side development tree |

After a successful probe, qmake builds fetch ROOM on `make room_fetch` (or automatically when the target exists in CI).

### Force overrides

| qmake | CMake |
|-------|-------|
| `CONFIG+=no_room` | `-DLIBMAN_FORCE_NO_CORE=ON` |
| `CONFIG+=core` | `-DLIBMAN_FORCE_CORE=ON` |

## Full ROOM build (with access)

### qmake

```bash
export LIBMAN_CORE_GIT_TOKEN=ghp_...
qmake ../door.pro
make -j1 capnp_install
make -j1 lstream_schemas
make -j1 room_fetch
make -j"$(nproc)"
```

### CMake

```bash
export LIBMAN_CORE_GIT_TOKEN=ghp_...
cmake -B build
cmake --build build -j
```

FetchContent clones ROOM to `.deps/Room/` and links `ROOM::room` / `ROOM::room_utils`.

Re-configure after changing the ROOM revision:

```powershell
Remove-Item -Recurse -Force .deps/Room
cmake -B build
```

Or pin a tag/commit:

```powershell
cmake -B build -DCORE_GIT_TAG=91705d7
```

## Local ROOM checkout (development)

```powershell
cmake -B build -DLIBMAN_ROOM_SOURCE_DIR=C:/path/to/Room
```

## Installed ROOM (advanced)

```powershell
cmake -B build -DLIBMAN_FETCH_CORE=OFF -DCORE_DIR=...
```

(`find_package(CORE)` — requires ROOM installed with `cmake --install`.)

## CMake cache variables

| Variable | Default | Description |
|----------|---------|-------------|
| `LIBMAN_FORCE_CORE` | `OFF` | Enable ROOM even if GitHub probe fails |
| `LIBMAN_FORCE_NO_CORE` | `OFF` | Disable ROOM even if probe succeeds |
| `LIBMAN_FETCH_CORE` | `ON` | Fetch ROOM from GitHub (when ROOM enabled) |
| `CORE_GIT_URL` | `https://github.com/IHP-GmbH/Room.git` | Repository URL |
| `CORE_GIT_TAG` | `main` | Branch, tag, or commit |
| `LIBMAN_ROOM_SOURCE_DIR` | *(empty)* | Local tree instead of fetch |

## Using ROOM in DOOR code

```cpp
#include "database.h"

room::Database db;
db.loadFromFile("layout.room");
```

Link targets are already set in `CMakeLists.txt` (`ROOM::room`, `ROOM::room_utils`).

## Python Command Line

DOOR also starts an out-of-process Python REPL with `import room` for interactive inspection of `.room` files. See **[Python Command Line](PYTHON_CONSOLE.md)** (Window → Messages / Command Line, Enter to run).

## CI (GitHub Actions)

| Job | ROOM |
|-----|------|
| `build-linux-no-room` | No token — probe fails, stubs only |
| `build-linux`, `tests-linux`, `build-windows`, `build-rhel8`, `build-ubuntu24` | `GH_PAT` (or legacy `LIBMAN_CORE_GIT_TOKEN`) — CommonDB checkout + full ROOM |

Add repository secret (org-level `GH_PAT` is preferred — same token as Qucs/XSchem/KLayout CI):

| Secret | Description |
|--------|-------------|
| `GH_PAT` | PAT with `repo` read access to `IHP-GmbH/Room` |
| `LIBMAN_CORE_GIT_TOKEN` | Legacy alias (still accepted if `GH_PAT` is unset) |

`qmake` / `cmake` auto-detect access; no manual `CONFIG+=core` required in CI.

## Layout view in DOOR

`layout` (and legacy `core`) is a first-class layout view suffix (like `gds`, `oas`, `lstr`):

- **Create:** View panel → New → Layout → `layout` (creates `<cell>/<cell>.layout.room`)
- **Tree:** expand `layout` to browse cell hierarchy from `LibIndex`
- **Open:** double-click opens the file in KLayout with a resolved top cell; see **[KLayout integration](KLAYOUT_INTEGRATION.md)** for server setup, root-cell rules, and mroom plugin notes.

**Schematic/symbol (`*.schematic.room`, `*.symbol.room`):** on Windows open in **Xschem via WSL** ([Xschem integration](XSCHEM_INTEGRATION.md)) and/or **Qucs-S** ([Qucs-S integration](QUCS_INTEGRATION.md)) — register one or both in Tool Manager.

Default `LayoutViews` property: `gds,oas,lstr,layout`.

## Project file and views

View paths come from the project file (`define(library, path)`). Edit entries in **[Project Editor](PROJECT_EDITOR.md)** (**File → Edit Project...**, `Ctrl+E`). Bulk-import from external formats via **[Import](IMPORT.md)** (**File → Import...**). After Save or import, DOOR reloads libraries; removed views disappear from the tree and documentation for the selected library is refreshed.

## Converter tools

With `CORE_BUILD_EXAMPLES=ON` (default in DOOR CMake / qmake fetch scripts), converter executables are deployed next to `door.exe` for the Import dialog. See **[Import](IMPORT.md)** for formats, folder import, and `LIBMAN_CONVERTER_DIR`.
