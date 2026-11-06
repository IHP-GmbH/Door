# Fetch and link ROOM (CommonDB) for qmake builds.
# Set LIBMAN_ROOT before including. Requires capnp_deps.pri first.

isEmpty(LIBMAN_ROOT) {
    error("room_deps.pri: LIBMAN_ROOT must be set to the repository root")
}

isEmpty(CAPNP_BUILD_PHONY) {
    error("room_deps.pri: include capnp_deps.pri before this file")
}

ROOM_BUILD_DIR = $$LIBMAN_ROOT/.deps/room-build
ROOM_SRC_DIR = $$LIBMAN_ROOT/.deps/Room
isEmpty(LIBMAN_ROOM_SOURCE_DIR) {
    LIBMAN_ROOM_SOURCE_DIR = $$(LIBMAN_ROOM_SOURCE_DIR)
}
!isEmpty(LIBMAN_ROOM_SOURCE_DIR) {
    ROOM_SRC_DIR = $$LIBMAN_ROOM_SOURCE_DIR
}

ROOM_STAMP = $$shell_path($$ROOM_BUILD_DIR/libman_room_built.stamp)
ROOM_FETCH_PHONY = room_fetch

INCLUDEPATH += \
    $$ROOM_SRC_DIR/src \
    $$ROOM_SRC_DIR/utils \
    $$ROOM_BUILD_DIR/generated

# room_oas (OasReader) before room_utils/room for GNU static link order.
LIBS += -L$$ROOM_BUILD_DIR -lroom_oas -lroom_utils -lroom
# ROOM archives depend on capnp; repeat for GNU static link order.
LIBS += -lcapnp -lkj

win32 {
    _fetchroom = $$replace($$shell_path($$LIBMAN_ROOT/scripts/fetch_room.cmd), \\, /)
    ROOM_FETCH_CMD = cmd /c \"$$_fetchroom\"
    !isEmpty(LIBMAN_ROOM_SOURCE_DIR) {
        _roomsrc = $$replace($$shell_path($$LIBMAN_ROOM_SOURCE_DIR), \\, /)
        ROOM_FETCH_CMD = cmd /c \"set \"LIBMAN_ROOM_SOURCE_DIR=$$_roomsrc\" && $$_fetchroom\"
    }
} else {
    ROOM_FETCH_CMD = bash $$shell_path($$LIBMAN_ROOT/scripts/fetch_room_linux.sh)
    ROOM_FETCH_CMD += \"$$shell_path($$LIBMAN_ROOT)\"
    !isEmpty(LIBMAN_ROOM_SOURCE_DIR) {
        ROOM_FETCH_CMD = LIBMAN_ROOM_SOURCE_DIR=$$shell_path($$LIBMAN_ROOM_SOURCE_DIR) $$ROOM_FETCH_CMD
    }
}

room_fetch.target = $$ROOM_FETCH_PHONY
room_fetch.commands = $$ROOM_FETCH_CMD
room_fetch.depends = $$CAPNP_BUILD_PHONY
QMAKE_EXTRA_TARGETS += room_fetch

!exists($$ROOM_STAMP) {
    PRE_TARGETDEPS += $$ROOM_FETCH_PHONY
}
