# Xschem integration (Windows + WSL)

DOOR runs on **Windows**. Xschem in this flow runs in **WSL** (Linux GUI via WSLg). DOOR double-clicks a schematic/symbol view → Windows launcher → WSL → Xschem with ROOM integration.

Layout views use [KLayout](KLAYOUT_INTEGRATION.md) natively on Windows; schematic/symbol use this guide.

## Overview

```
DOOR (Windows)
  double-click schematic / symbol
    → open-xschem-wsl.bat
      → wsl bash open-xschem-wsl.sh <path>
        → xschem --rcfile <xschemrc>
          → core.tcl (coretcl.so)
            → open *.schematic.room / *.symbol.room
```

| View in DOOR | File suffix | Tool |
|----------------|-------------|------|
| `schematic` | `*.schematic.room` | Xschem (WSL) |
| `symbol` | `*.symbol.room` | Xschem (WSL) |
| `layout` | `*.layout.room` | KLayout (Windows) |

## Prerequisites

### Windows

- DOOR built and running
- [WSL2](https://learn.microsoft.com/en-us/windows/wsl/install) with a Linux distro (Ubuntu recommended)
- **WSLg** (GUI support — default on Windows 11; on Windows 10 install compatible WSL + GUI stack)

### WSL

Inside WSL (from your **XSchem-coredb** checkout):

1. **Xschem** built and on `PATH` (`xschem` command works in a WSL terminal)
2. **coretcl.so** built for WSL (from Room):

   ```bash
   cd /path/to/XSchem-coredb
   bash build-core-tcl.sh
   ```

   Expects **Room** as a sibling directory (`../Room`) unless `ROOM_ROOT` / `COMMONDB_ROOT` is set. Produces `integrations/coretcl.so` and `Room/build-wsl/.../coretcl.so`.

3. Test manually:

   ```bash
   xschem --rcfile /path/to/XSchem-coredb/xschemrc
   ```

### Recommended directory layout

```text
parent/
  XSchem-coredb/   ← https://github.com/adatsuk/XSchem-coredb (launchers, xschemrc, core.tcl)
  Room/            ← sibling checkout (coretcl.so build)
  DOOR/            ← optional; Windows app
  KLayout-roomdb/  ← optional; https://github.com/adatsuk/KLayout-roomdb (mroom plugin)
  Qucs-S-coredb/   ← optional; https://github.com/adatsuk/Qucs-S-coredb (ROOM in Qucs-S)
```

Paths are resolved from script locations — no editing required when clones stay siblings.

## Launcher scripts (Windows → WSL)

Scripts live in the **XSchem-coredb** repo (not DOOR):

| File | Role |
|------|------|
| `XSchem-coredb/integrations/open-xschem-wsl.bat` | Windows entry point for DOOR Tool Manager |
| `XSchem-coredb/integrations/open-xschem-wsl.sh` | WSL wrapper: path conversion, env vars, starts `xschem` |

### Path resolution (no hardcoded checkout paths)

| Component | How the path is chosen |
|-----------|-------------------------|
| `open-xschem-wsl.bat` | `%~dp0` + `wsl --cd` → runs `./open-xschem-wsl.sh` in `integrations/` |
| `open-xschem-wsl.sh` | Parent of `integrations/` = **Xschem root** → `xschemrc` |
| `xschemrc` | `[info script]` = Xschem root; `../Room` = **ROOM root** |
| `core.tcl` | Same; searches `integrations/coretcl.so` and `Room/build-wsl/...` |

Optional environment overrides (WSL / Tcl):

| Variable | Purpose |
|----------|---------|
| `XSCHEM_ROOT` / `XSCHEM_HOME` | Xschem repo root |
| `XSCHEM_RC` | Alternate `xschemrc` file |
| `ROOM_ROOT` / `COMMONDB_ROOT` / `CORE_ROOT` | Room checkout (non-sibling layout) |
| `XSCHEM_SRC` | Source tree for `build-xschem.sh` (default: `XSchem-coredb/xschem-src`) |

DOOR sets these when opening a schematic/symbol view with attached tech or design libraries:

| Variable | Purpose |
|----------|---------|
| `CORE_PRIMITIVE_LIBS_FILE` | Temp file with one `*.symbol.room` path per line (preferred for large libraries) |
| `CORE_PRIMITIVE_LIB` | First attached symbol core (legacy fallback) |
| `LIBMAN_TECH_LIBRARY` | Semicolon-separated library names (`commonLib;ihp_sg13g2;…`) |
| `QUCS_PRIMITIVE_LIB` | Qucs `<Lib>` bundle name for PDK cells |
| `XSCHEM_PRIMITIVE_CACHE` | Temp dir where `core.tcl` materializes `.sym` files for Xschem |

`open-xschem-wsl.sh` converts Windows paths to WSL (`/mnt/c/...`) and passes variables through `WSLENV`.

DOOR Tool Manager still needs the **full Windows path** to `open-xschem-wsl.bat` on your machine (one-time setup in Settings → Tools).

### What the launcher does

1. DOOR passes a **Windows** path to the view file (e.g. `C:\...\sg13g2_stdcell.schematic.room`).
2. `.bat` calls WSL with that path as `%1`.
3. `.sh` converts `C:\...` → `/mnt/c/...` (`wslpath` or fallback).
4. By extension:
   - `*.schematic.room` / `*.symbol.room` → sets `XSCHEM_OPEN_CORE` → `core.tcl` opens via ROOM API after idle
   - `*.sch` / `*.sym` → sets `XSCHEM_OPEN_FILE` → direct `xschem load`
5. Xschem starts with `--rcfile` pointing at project `xschemrc` (loads `core.tcl`).

Supported arguments: one file path only.

## DOOR Tool Manager setup

**Settings → Tools** (Tool Manager):

| Tab / field | Value |
|-------------|--------|
| Tool name | e.g. `schematic` (or your custom tool id) |
| **Executable** | Full path to `open-xschem-wsl.bat` |

Example (use your actual checkout path):

```text
D:\work\XSchem-coredb\integrations\open-xschem-wsl.bat
```

| Property | Typical value |
|----------|----------------|
| **Name(s)** for schematic tool | `schematic` (and optionally `symbol`) |

Do **not** put `schematic.core` in Name(s) — DOOR matches the **view name** (`schematic`, `symbol`), not the file suffix.

Default schematic-related views come from ROOM file names (`schematic`, `symbol`, `layout`). Register `schematic` and `symbol` with the same Xschem launcher if both use WSL.

### Opening from DOOR

1. Project file contains e.g. `define("lib", ".../cell.schematic.room");` — see [Project Editor](PROJECT_EDITOR.md).
2. Select **Cell** → double-click **schematic** (or **symbol**).
3. DOOR runs the bat file with the absolute path to the `.core` file.

## ROOM + Xschem behaviour

Authoritative storage is the **ROOM file** (`.schematic.room` / `.symbol.room`), not `payload/*.sch`.

With the native bridge (Xschem built from **XSchem-coredb** with `load_data` / `get_data`):

| Step | Action |
|------|--------|
| Open | `coreapi_export_cell_data` → `xschem load_data` (records in RAM; no temp `.sch`) |
| Edit | User edits in Xschem |
| Save | `xschem get_data` → `coreapi_import_data` → writes `.core` |

`payload/<cell>.sch` is no longer used as interchange for ROOM-bound views. Older Xschem builds without `load_data` fall back to ephemeral temp files via `core.tcl`.

Window/tab title shows the **ROOM filename** (e.g. `cell.schematic.room`), not `payload/cell.sch`.

`xschemrc` sets `initial_geometry` so a corrupted `~/.xschem/geometry` (`1x1+...`) does not shrink the window to a tiny strip.

### File naming (CommonDB convention)

| View | ROOM file |
|------|-----------|
| Schematic | `<cell>.schematic.room` |
| Symbol | `<cell>.symbol.room` |
| Layout | `<cell>.layout.room` |

## Project layout example

```text
sg13g2_stdcell/
  sg13g2_stdcell/
    sg13g2_stdcell.schematic.room   ← DOOR opens this
    sg13g2_stdcell.layout.room
    payload/                        ← optional legacy cache (not used by native bridge)
      sg13g2_stdcell.sch
```

## EM lookalike / emmodel netlisting

Layout-lookalike cells (`em.lookalike=1` on `*.symbol.room`, published with `*.emmodel.room`)
netlist like Qucs-S-roomdb LibComp — **same EMTest TB works in Qucs and Xschem**:

1. Xschem export stamps `format="@name @pinlist 0 @symname"` and `spice_stop=true` (pins + trailing gnd).
2. `coreapi_export_cell` / `room_to_xschem` writes a sibling `cell.spice` via CommonDB S2Spice (`touchstone_to_spice`) from the Touchstone path in the emmodel.
3. `integrations/room.tcl` prepends `.include` of those spice files into the batch netlist before Simulate.
4. Qucs `Pac` / `.SP` / `NutmegEq` map to ngspice `port` + `sp` (+ optional `let`) on Xschem export (`Pac.sym` in analogLib).

Instance line shape: `x1 <pin nets…> 0 <cellname>` — the trailing `0` is the S2Spice reference node.

Fixture: `tests/data/EMTest/testbench` (Pac + lookalike `cmim_2u3_tm1fix` + `.SP`). Open the same `testbench.schematic.room` from DOOR with either schematic tool.

## Troubleshooting

| Symptom | What to check |
|---------|----------------|
| DOOR says tool not configured | Tool Manager: schematic tool → `open-xschem-wsl.bat` |
| `xschemrc not found` | Run from Xschem repo layout; or set `XSCHEM_ROOT` / `XSCHEM_RC` |
| `coretcl.so not found` | Run `build-core-tcl.sh` in WSL; ensure `../CommonDB` exists or set `COMMONDB_ROOT` |
| Xschem window is a tiny title bar | Corrupt `~/.xschem/geometry` with `1x1+` lines — remove them or delete file; `initial_geometry` in `xschemrc` helps |
| Title shows `.sch` not `.core` | Update `integrations/core.tcl` (title hooks); restart Xschem |
| Blank schematic | ROOM file empty or export failed — check Messages in DOOR / Xschem stderr in WSL terminal |
| WSL `file not found` | Path conversion: ensure DOOR passes existing file; test `wslpath -u 'C:\...'` |
| `MISSING SYMBOL` for lab_pin, PDK devices | Check `CORE_PRIMITIVE_LIBS_FILE` / `LIBMAN_TECH_LIBRARY` in WSL (`echo $CORE_PRIMITIVE_LIBS`); see `/tmp/xschem-libman-launch.log` |
| Lookalike sim: undefined subckt / no S-params | Need sibling `*.emmodel.room` + readable `snpPath`; rematerialize symbol (`.spice` next to `.sym`); check netlist starts with `.include …/*.spice` |
| `extra characters after close-quote` (Tcl) | Outdated `core.tcl` — update XSchem-coredb; usually a Tcl syntax error in symbol index build |
| No GUI in WSL | WSLg / DISPLAY; run `xschem` manually in WSL first |

### Primitive libraries (commonLib / PDK)

DOOR auto-discovers `<projectDir>/<lib>/**/*.symbol.room` for attached tech libraries and writes paths to `CORE_PRIMITIVE_LIBS_FILE` before launching Xschem or Qucs-S. `commonLib` holds Xschem device symbols (`lab_pin`, `res`, `title-3`, …); PDK cells come from `sg13g2_pr` or design libraries. Qucs skips Xschem-only decorations (`lab_wire`, `code_shown`) but maps passives to native Qucs components.

### Clean geometry cache (WSL)

```bash
grep -v '1x1+' ~/.xschem/geometry > ~/.xschem/geometry.tmp && mv ~/.xschem/geometry.tmp ~/.xschem/geometry
```

### Manual test (same as DOOR)

```powershell
D:\work\XSchem-coredb\integrations\open-xschem-wsl.bat ^
  D:\work\DOOR\tests\data\sg13g2_stdcell\sg13g2_stdcell\sg13g2_stdcell.schematic.room
```

## Related docs

- [ROOM integration](ROOM_INTEGRATION.md) — building CommonDB / ROOM in DOOR
- [KLayout integration](KLAYOUT_INTEGRATION.md) — layout views on Windows
- [Project Editor](PROJECT_EDITOR.md) — adding `*.schematic.room` to the project file
- XSchem-coredb repo: `integrations/core.tcl`, `integrations/open-xschem-wsl.*`, `xschemrc` — [github.com/adatsuk/XSchem-coredb](https://github.com/adatsuk/XSchem-coredb)
