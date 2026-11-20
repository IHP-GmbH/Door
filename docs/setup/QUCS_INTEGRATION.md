# Qucs-S integration (Windows)

DOOR runs on **Windows**. **Qucs-S** (CORE-enabled) also runs natively on Windows — no WSL. Double-click a schematic/symbol view → `run-qucs-s.bat` → Qucs-S opens the `.schematic.room` / `.symbol.room` file. Simulation is done **inside Qucs-S** (Simulate → ngspice).

Xschem (WSL) remains available as a second schematic tool — see [Xschem integration](XSCHEM_INTEGRATION.md).

## Overview

```
DOOR (Windows)
  double-click schematic / symbol
    → run-qucs-s.bat
      → qucs-s.exe <path>
        → load *.schematic.room / *.symbol.room (ROOM bridge)
        → Simulation → ngspice_con.exe
```

| View in DOOR | File suffix | Tool |
|----------------|-------------|------|
| `schematic` | `*.schematic.room` | Qucs-S **or** Xschem (pick in Tool Manager / multi-tool) |
| `symbol` | `*.symbol.room` | same |
| `layout` | `*.layout.room` | [KLayout](KLAYOUT_INTEGRATION.md) |

## Prerequisites

### Qucs-S (ROOM build)

Checkout and build sibling to Room/DOOR:

```text
parent/
  Qucs-S-coredb/   ← https://github.com/adatsuk/Qucs-S-coredb
  Room/
  DOOR/
  XSchem-coredb/   ← optional second schematic tool
```

Build notes: `Qucs-S-coredb/docs/CORE.md`. Expect:

```text
Qucs-S-coredb\build\qucs\qucs-s.exe
Qucs-S-coredb\run-qucs-s.bat
```

### ngspice (Windows)

Qucs-S needs **`ngspice_con.exe`** on `PATH` (console binary, not the GUI `ngspice.exe`).

Recommended for IHP OSDI on Windows: **ngspice-46** (OSDI v0.4) with PE plugins in `osdi-win/`.

`run-qucs-s.bat` prefers, if present:

```text
%USERPROFILE%\Documents\VBIC_cryo\tools\ngspice\Spice64\bin\ngspice_con.exe
```

Fallback: ngspice-42 under `%USERPROFILE%\Spice64\…` (OSDI **v0.3** only — needs matching older PE plugins).

### IHP Open-PDK models + Qucs libraries

1. **Models / OSDI** (for `.LIB cornerMOSlv.lib mos_tt`):

   Primary models path (this machine):

   ```text
   C:\Work\IHP\ihp-sg13g2\libs.tech\ngspice\models\
   ```

   Fallback / WSL copy:

   ```text
   %USERPROFILE%\Documents\IHP-Open-PDK\ihp-sg13g2\libs.tech\ngspice\models\
   %USERPROFILE%\Documents\IHP-Open-PDK\ihp-sg13g2\libs.tech\ngspice\osdi\
   ```

2. **User spiceinit** — `%USERPROFILE%\.spiceinit` must set `sourcepath` to the models directory and load **Windows** OSDI plugins.

   > The files under `…/ngspice/osdi/` in the PDK tree are usually **Linux ELF**. On Windows they fail with  
   > `%1 is not a valid Win32 application`.  
   > Put PE (Windows) builds in `…/ngspice/osdi-win/` (compile with [OpenVAF](https://openvaf.github.io/download/) from an **x64 Native Tools** prompt, or copy known-good PE plugins).

3. **Qucs user libraries** — `%USERPROFILE%\.qucs\user_lib\`:

   ```text
   IHP_PDK_nonlinear_components.lib
   IHP_PDK_basic_components.lib
   IHP_PDK_stdcells.lib
   ```

   Source in PDK: `ihp-sg13g2/libs.tech/qucs-s/user_lib/`.  
   `run-qucs-s.bat` copies them once if missing and `PDK_ROOT` is set.

Example `%USERPROFILE%\.spiceinit`:

```text
setcs sourcepath = ( $sourcepath C:/Work/IHP/ihp-sg13g2/libs.tech/ngspice/models )

osdi 'C:/Users/<you>/Documents/IHP-Open-PDK/ihp-sg13g2/libs.tech/ngspice/osdi-win/psp103.osdi'
osdi 'C:/Users/<you>/Documents/IHP-Open-PDK/ihp-sg13g2/libs.tech/ngspice/osdi-win/psp103_nqs.osdi'
osdi 'C:/Users/<you>/Documents/IHP-Open-PDK/ihp-sg13g2/libs.tech/ngspice/osdi-win/r3_cmc.osdi'
osdi 'C:/Users/<you>/Documents/IHP-Open-PDK/ihp-sg13g2/libs.tech/ngspice/osdi-win/mosvar.osdi'
```

Build Windows OSDI (once), from `libs.tech/verilog-a/…` with OpenVAF **on Windows** + MSVC x64 linker, e.g.:

```bat
cd %PDK_ROOT%\ihp-sg13g2\libs.tech\ngspice\osdi-win
openvaf ..\..\verilog-a\psp103\psp103.va
```

## DOOR Tool Manager

**Settings → Tools**:

| Field | Value |
|-------|--------|
| Tool name | e.g. `qucs` or reuse `schematic` |
| **Executable** | Full path to `run-qucs-s.bat` |
| **Name(s)** | `schematic` (and optionally `symbol`) |

Example:

```text
C:\Users\anton\Documents\Qucs-S-coredb\run-qucs-s.bat
```

If both Xschem and Qucs are registered for `schematic`, DOOR will ask which tool to use on open.

Do **not** put `open-xschem-wsl.sh` next to this `.bat` — DOOR prefers the WSL launcher when that script sits beside the chosen bat.

## What DOOR passes

Same primitive env as Xschem (`projectfile.cpp`):

| Variable | Purpose |
|----------|---------|
| `CORE_PRIMITIVE_LIBS_FILE` | List of `*.symbol.room` (design + tech) |
| `LIBMAN_TECH_LIBRARY` | Library names |
| `QUCS_PRIMITIVE_LIB` | `IHP_PDK_nonlinear_components` (Qucs `<Lib>` bundle) |

Qucs-S ROOM load uses `PrimitiveResolver` + skips Xschem-only decorations (`code_shown`, `launcher`, …).

## Simulation tips

1. Open a **Qucs-native** TB (has `.TR` / `.DC` + `INCLSCR` + `Lib` / ROOM hierarchy) — not only an Xschem TB that relied on `code_shown` NGSPICE blocks.
2. In Qucs-S: check **Simulation → Simulators Settings** → Ngspice = `ngspice_con.exe` (or full path under `Spice64\bin`).
3. Press **Simulate** (F2 / toolbar). Plots use diagram nodes like `ngspice/v(vout)`.

### Smoke tests

| Schematic | Path |
|-----------|------|
| Official IHP DC NMOS | `IHP-Open-PDK/.../libs.tech/qucs-s/examples/dc_lv_nmos.sch` (File → Open) |
| DOOR Qucs examples | `DOOR/tests/data/Qucs_examples/dc_lv_nmos/dc_lv_nmos.schematic.room` |
| Flat inverter TB (Qucs) | `DOOR/tests/data/_invcheck/inverter_tb_qucs_sim.sch` |

Xschem `inverter_tb.schematic.room` opens in Qucs but **drops** NGSPICE `code_shown` — add a Qucs `.TR` + `INCLSCR` (or use the flat TB above) before Simulate works like Xschem.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `qucs-s.exe` missing | Build Qucs-S-coredb (`docs/CORE.md`) |
| Ngspice not found | Install ngspice-42; ensure `ngspice_con.exe` on PATH via `run-qucs-s.bat` |
| `cornerMOSlv.lib` not found | Set `.spiceinit` `sourcepath` + copy PDK `models/` |
| OSDI `%1 is not a valid Win32 application` | PDK `osdi/*.osdi` are Linux ELF — use `osdi-win/` PE builds in `.spiceinit` |
| Empty / missing Lib components | Install `~/.qucs/user_lib/IHP_PDK_*.lib` |
| Hierarchy missing pins | Attach tech/design libs in DOOR so `CORE_PRIMITIVE_LIBS_FILE` is set |

## Related

- [Xschem integration](XSCHEM_INTEGRATION.md) — WSL schematic path
- [Import](IMPORT.md) — `qucs_to_core` / `core_to_qucs`
- [ROOM integration](ROOM_INTEGRATION.md)
- Qucs-S ROOM notes: `Qucs-S-coredb/docs/CORE.md`
