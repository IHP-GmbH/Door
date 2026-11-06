#!/usr/bin/env bash
# Clone CommonDB (ROOM) and build static libs for qmake builds.
set -euo pipefail

LIBMAN_ROOT="${1:?LibMan repository root required}"
CAPNP_ROOT="${LIBMAN_ROOT}/capnp-install"
ROOM_BUILD="${LIBMAN_ROOT}/.deps/room-build"
STAMP="${ROOM_BUILD}/libman_room_built.stamp"

ROOM_GIT_URL="${ROOM_GIT_URL:-https://github.com/IHP-GmbH/Room.git}"
ROOM_GIT_BRANCH="${ROOM_GIT_BRANCH:-main}"

room_git_clone_url() {
    local url="$1"
    local token="${LIBMAN_CORE_GIT_TOKEN:-${GITHUB_TOKEN:-}}"
    if [ -n "$token" ] && [[ "$url" == https://github.com/* ]]; then
        echo "https://x-access-token:${token}@${url#https://}"
    else
        echo "$url"
    fi
}

if [ -f "$STAMP" ]; then
    echo "ROOM already built ($STAMP)"
    exit 0
fi

if [ -n "${LIBMAN_ROOM_SOURCE_DIR:-}" ] && [ -d "${LIBMAN_ROOM_SOURCE_DIR}" ]; then
    ROOM_SRC="${LIBMAN_ROOM_SOURCE_DIR}"
    echo "Using local ROOM tree: ${ROOM_SRC}"
else
    ROOM_SRC="${LIBMAN_ROOT}/.deps/Room"
    if [ -f "${ROOM_SRC}/src/room_paths.h" ]; then
        echo "Using existing ROOM checkout: ${ROOM_SRC}"
    elif [ ! -d "$ROOM_SRC/.git" ]; then
        mkdir -p "$(dirname "$ROOM_SRC")"
        if [ -d "$ROOM_SRC" ] && [ -n "$(ls -A "$ROOM_SRC" 2>/dev/null || true)" ]; then
            echo "ERROR: ${ROOM_SRC} exists but is not a valid ROOM checkout" >&2
            exit 1
        fi
        clone_url="$(room_git_clone_url "$ROOM_GIT_URL")"
        echo "Cloning ROOM from ${ROOM_GIT_URL} (${ROOM_GIT_BRANCH})..."
        git clone --depth 1 --branch "$ROOM_GIT_BRANCH" "$clone_url" "$ROOM_SRC"
    fi
fi

if [ ! -f "${CAPNP_ROOT}/include/capnp/message.h" ]; then
    echo "ERROR: Cap'n Proto not found in ${CAPNP_ROOT}. Run capnp_install first." >&2
    exit 1
fi

cmake -S "$ROOM_SRC" -B "$ROOM_BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DROOM_BOOTSTRAP_CAPNP=OFF \
    -DCAPNP_ROOT="$CAPNP_ROOT" \
    -DROOM_BUILD_TESTS=OFF \
    -DROOM_BUILD_OAS_TESTS=OFF \
    -DROOM_BUILD_EXAMPLES=ON

cmake --build "$ROOM_BUILD" --target room room_utils room_oas gds_to_room xschem_to_room qucs_to_room room_to_gds room_to_xschem room_to_qucs -j"$(nproc 2>/dev/null || echo 2)"
cmake --build "$ROOM_BUILD" --target oas_to_room -j"$(nproc 2>/dev/null || echo 2)" || true

touch "$STAMP"
echo "ROOM built in ${ROOM_BUILD}"
