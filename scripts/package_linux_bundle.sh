#!/usr/bin/env bash
# Package door + runtime libraries into a self-contained tar.gz bundle.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build}"
OUT_TAR="${2:-$ROOT/door-linux-bundle.tar.gz}"
BUNDLE_LABEL="${3:-linux}"

DIST="$ROOT/dist-bundle"
STAGE="$ROOT/.bundle-stage"
rm -rf "$DIST" "$STAGE"
mkdir -p "$DIST/bin" "$DIST/lib" "$STAGE"

find_binary() {
    local candidate
    for candidate in \
        "$BUILD_DIR/door" \
        "$BUILD_DIR/DOOR" \
        "$BUILD_DIR/release/door" \
        "$BUILD_DIR/release/DOOR"; do
        if [[ -f "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

BIN="$(find_binary)" || {
    echo "ERROR: door binary not found under $BUILD_DIR"
    ls -la "$BUILD_DIR" || true
    exit 1
}

cp "$BIN" "$STAGE/door"
chmod +x "$STAGE/door"

CAPNP_LIB="$ROOT/capnp-install/lib"
mkdir -p "$STAGE/lib"
if [[ -d "$CAPNP_LIB" ]]; then
    cp -a "$CAPNP_LIB"/lib*.so* "$STAGE/lib/" 2>/dev/null || true
fi

if [[ -n "${QT_ROOT_DIR:-}" && -d "${QT_ROOT_DIR}/bin" ]]; then
    export PATH="${QT_ROOT_DIR}/bin:${PATH}"
fi

DEPLOY_LDPATH="$STAGE/lib"
if [[ -n "${QT_ROOT_DIR:-}" && -d "${QT_ROOT_DIR}/lib" ]]; then
    DEPLOY_LDPATH="${DEPLOY_LDPATH}:${QT_ROOT_DIR}/lib"
fi
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
    DEPLOY_LDPATH="${DEPLOY_LDPATH}:${LD_LIBRARY_PATH}"
fi
# linuxdeployqt runs ldd on Qt plugins (libqxcb.so); need host libs visible during packaging.
DEPLOY_LDPATH="${DEPLOY_LDPATH}:/usr/lib64:/lib64"
export LD_LIBRARY_PATH="$DEPLOY_LDPATH"

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/lib' "$STAGE/door" || true
fi

bundle_manual() {
    bash "$(dirname "$0")/bundle_qt_libs_manual.sh" "$STAGE/door" "$STAGE"
}

if [[ "${BUNDLE_SKIP_LINUXDEPLOYQT:-}" == "1" ]]; then
    bundle_manual
elif command -v linuxdeployqt >/dev/null 2>&1; then
    if ! linuxdeployqt "$STAGE/door" -bundle-non-qt-libs -always-overwrite; then
        echo "linuxdeployqt failed; falling back to manual Qt bundling." >&2
        bundle_manual
    fi
else
    echo "WARNING: linuxdeployqt not found; using manual Qt bundling."
    bundle_manual
fi

cp "$STAGE/door" "$DIST/bin/door"
for so in "$STAGE"/*.so*; do
    [[ -e "$so" ]] || continue
    cp -a "$so" "$DIST/lib/"
done
if [[ -d "$STAGE/lib" ]]; then
    cp -a "$STAGE/lib/"* "$DIST/lib/" 2>/dev/null || true
fi
if [[ -d "$STAGE/plugins" ]]; then
    cp -a "$STAGE/plugins" "$DIST/"
fi

if [[ -d "$ROOT/capnp-install/lib" ]]; then
    cp -a "$ROOT/capnp-install/lib/"lib*.so* "$DIST/lib/" 2>/dev/null || true
fi

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib' "$DIST/bin/door" || true
fi

for dir in scripts keywords icons; do
    if [[ -d "$ROOT/$dir" ]]; then
        cp -a "$ROOT/$dir" "$DIST/"
    fi
done

cat >"$DIST/door-run.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
# Resolve symlinks so DIR is the real dist-bundle/ even when invoked via <prefix>/bin/door.
_src="${BASH_SOURCE[0]}"
while [[ -L "$_src" ]]; do
  _dir="$(cd -P "$(dirname "$_src")" && pwd)"
  _src="$(readlink "$_src")"
  [[ "$_src" != /* ]] && _src="$_dir/$_src"
done
DIR="$(cd -P "$(dirname "$_src")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
# Prefer bundled Room python next to the binary (…/dist-bundle/python/room).
if [[ -f "$DIR/python/room/__init__.py" ]]; then
  export DOOR_ROOM_PYTHON="${DOOR_ROOM_PYTHON:-$DIR/python}"
  export PYTHONPATH="$DIR/python${PYTHONPATH:+:$PYTHONPATH}"
fi
if [[ -z "${ROOM_C_DLL:-}" ]]; then
  for _c in "$DIR/python/room/libroom_c.so" "$DIR/python/libroom_c.so" "$DIR/lib/libroom_c.so" "$DIR/bin/libroom_c.so"; do
    if [[ -f "$_c" ]]; then
      export ROOM_C_DLL="$_c"
      break
    fi
  done
fi
exec "$DIR/bin/door" "$@"
EOF
chmod +x "$DIST/door-run.sh"

cat >"$DIST/BUNDLE.txt" <<EOF
DOOR portable bundle ($BUNDLE_LABEL)
Built: $(date -u +%Y-%m-%dT%H:%M:%SZ)
Run: ./door-run.sh [arguments]
Requires: glibc 2.28+ (RHEL 8.10 / Rocky Linux 8 / AlmaLinux 8 compatible)
EOF

rm -rf "$STAGE"
tar -C "$ROOT" -czf "$OUT_TAR" "$(basename "$DIST")"
echo "Created $OUT_TAR"
