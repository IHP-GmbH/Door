# Python Command Line (CML)

DOOR embeds a Virtuoso-style **Command Line** under **Messages** for interactive [ROOM](https://github.com/IHP-GmbH/Room) Python (`import room`).

## UI

| Element | Role |
|---------|------|
| **Messages** | Log + echoed `>>>` commands and Python output |
| **Command Line** | Multiline editor for Python |
| **Window → Messages / Command Line** | Show or hide each panel (saved in `QSettings`) |

### Keys

| Key | Action |
|-----|--------|
| **Enter** | Run the buffer (or selection) |
| **Shift+Enter** | New line |
| **↑ / ↓** | Command history (on the first / last line of a multiline buffer) |

History keeps up to 200 distinct commands for the session.

## Startup

On launch DOOR starts a persistent Python process from [`python/door_console.py`](../../python/door_console.py) (copied next to the binary on build; override with `DOOR_CONSOLE_PY / LIBMAN_CONSOLE_PY`):

1. Finds `python` / `python3` (`DOOR_PYTHON / LIBMAN_PYTHON` overrides the executable).
2. Prefers Room’s package via `PYTHONPATH`, in order:
   - **Settings → Tools → Room → Path** (Room checkout or `…/Room/python`)
   - `DOOR_ROOM_PYTHON` / `LIBMAN_ROOM_PYTHON`
   - bundled `<dir(door.exe)>/python` (copied at build when `DOOR_DEPLOY_ROOM_PYTHON=ON`, default)
   - sibling `../Room/python` or Door’s `.deps/Room/python` (FetchContent)
   - If **Room → Path** is empty at startup and a package is found, Door writes that path into Tools automatically.
3. Sets `ROOM_C_DLL` when a built `room_c` / `libroom_c` is found next to DOOR, under the bundled `python/room/`, or under Room `build-python/python`.

The session namespace already has:

| Name | Role |
|------|------|
| `room` | ROOM package (`import room`) |
| `load(path)` | SKILL-style run a script into this session |
| `define(library, path)` | Register an existing view with the open project (updates UI + `.projects`, no reload dialog) |
| `undefine(library, path)` | Drop a view registration (file kept) |

If `import room` fails, a red warning appears in Messages; install bindings from the Room repo (`pip install ./python`) or set the env vars above.

## Examples

```python
db = room.open(r"C:/path/to/cell.schematic.room")
print(db.cell_names())
cell = db.find_cell(db.cell_names()[0])
cxt = cell.content()          # sole view when the file has one content
print(cxt.view(), len(cxt.instances()))
```

### Load a script (SKILL-style)

```python
load(r"C:/path/to/myscript.py")
```

Runs the file in the same persistent session as typed commands (`room` and any prior bindings stay visible). Prefer this over `import` when you want the whole script executed and re-runnable.

### Register a new cell/view without reloading

Create the `.room` file with ROOM, then tell DOOR:

```python
define("module_0_foundations", r"C:/path/to/my_rect/my_rect.layout.room")
```

DOOR adds the cell/view to the lists and writes `define(...)` into the open `.projects` file. External editors still get the reload prompt; CML `define`/`undefine` do not.

Do **not** edit `.projects` from Python yourself (backslash escaping is easy to get wrong). Always use `define` / `undefine`.

### Example: create a layout cell with a rectangle and register it

Assumes a library folder already attached in the open project (here `module_0_foundations`). Paths are examples — adjust to your tree.

Paste into the Command Line, or save as a `.py` / `.py.txt` and `load(...)` it:

```python
from pathlib import Path

lib = Path(r"C:/Users/anton/Documents/IHP-AnalogAcademy/modules/module_0_foundations")
name = "my_rect"
lib_name = "module_0_foundations"

out = (lib / name / f"{name}.layout.room").resolve()
out.parent.mkdir(parents=True, exist_ok=True)

with room.create() as db:
    db.lib_name = lib_name
    db.generator = "cml-example"
    if not db.layers():
        db.add_layer(1, 0, "Metal1", room.LayerPurpose.DRAWING)
    cell = db.get_or_create_cell(name)
    layout = cell.ensure_content(room.ViewType.LAYOUT, dbu_per_micron=1000.0)
    layout.add_rect(layer_id=0, llx=0, lly=0, urx=2000, ury=1000)
    db.save(out, room.ViewType.LAYOUT)
    print("saved", out)

define(lib_name, out)   # updates Project / Cell / View lists + .projects
```

After that, select library `module_0_foundations` → cell `my_rect` → view `layout`.

To drop only the project registration (file kept):

```python
undefine("module_0_foundations", out)
```

Open **`.room` files**, not view folders (e.g. `*.emsetup` is a directory).

Paths are trimmed by ROOM’s Python API (leading/trailing spaces are ignored).

Full API: [ROOM Python docs](https://ihp-gmbh.github.io/Room/python.html) · [Cell](https://ihp-gmbh.github.io/Room/python_cell.html).

## Related

- [ROOM integration](ROOM_INTEGRATION.md) — fetch/build ROOM with DOOR
- [Import](IMPORT.md) — GDS/Xschem/Qucs/OAS → `.room`
- [Project Editor](PROJECT_EDITOR.md) — `define(library, path)` lines in `.projects`
