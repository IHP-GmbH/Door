#-------------------------------------------------
#
# Project created by QtCreator 2023-05-03T09:50:11
#
#-------------------------------------------------

QT += core gui concurrent network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = door
TEMPLATE = app

CONFIG += c++17
DEFINES += QT_NO_DEPRECATED_WARNINGS
INCLUDEPATH += src/assistant

LIBS += -lz
win32:LIBS += -lcrypt32

win32 {
    QMAKE_CXXFLAGS += -Wa,-mbig-obj
    RC_ICONS = icons/door.ico
}
QMAKE_CXXFLAGS += -g1

SOURCES += \
    lstream/lstreamcellwriter.cpp \
    oas/oasCreate.cpp \
    src/main.cpp \
    gds/gdsReadAsync.cpp \
    src/libfileparser.cpp \
    src/libdefine_utils.cpp \
    oas/oasReadAsync.cpp \
    oas/oasReader.cpp \
    src/klayout_tools.cpp \
    src/klayoutCellResolver.cpp \
    src/lstreamcellreader.cpp \
    src/mainwindow.cpp \
    src/assistant/assistantmcp.cpp \
    src/assistant/assistantagent.cpp \
    src/assistant/assistantchatpanel.cpp \
    src/assistant/securestore.cpp \
    src/assistant/roommcpclient.cpp \
    src/python_console.cpp \
    extension/variantmanager.cpp \
    extension/variantfactory.cpp \
    extension/qlineeditd2.cpp \
    extension/filepathmanager.cpp \
    extension/fileeditfactory.cpp \
    extension/fileedit.cpp \
    QtPropertyBrowser/qtvariantproperty.cpp \
    QtPropertyBrowser/qttreepropertybrowser.cpp \
    QtPropertyBrowser/qtpropertymanager.cpp \
    QtPropertyBrowser/qtpropertybrowserutils.cpp \
    QtPropertyBrowser/qtpropertybrowser.cpp \
    QtPropertyBrowser/qtgroupboxpropertybrowser.cpp \
    QtPropertyBrowser/qteditorfactory.cpp \
    QtPropertyBrowser/qtbuttonpropertybrowser.cpp \
    gds/gdsreader.cpp \
    src/projectmanager.cpp \
    src/projecteditor.cpp \
    src/importdialog.cpp \
    src/exportdialog.cpp \
    src/room_import_service.cpp \
    src/room_export_service.cpp \
    lstream/lstrReadAsync.cpp \
    src/property.cpp \
    src/toolcustomtool.cpp \
    src/toolmanager.cpp \
    src/view_tools.cpp \
    src/viewtoolstablewidget.cpp \
    src/toolpickerdialog.cpp \
    src/projectfile.cpp \
    src/categories.cpp \
    src/viewcontextmenu.cpp \
    src/groupcontextmenu.cpp \
    src/projectcontextmenu.cpp \
    src/categorycontextmenu.cpp \
    src/about.cpp \
    src/keybindings.cpp \
    src/keybindingsdialog.cpp \
    src/newview.cpp \
    src/snapshotscene.cpp \
    src/snapshotview.cpp \
    room/room_path_utils.cpp \
    room/room_file_lock.cpp \
    room/converter_paths.cpp

LIBMAN_ROOT = $$dirname(_PRO_FILE_)
include(core_build_config.pri)

contains(CONFIG, no_room) {
    DEFINES += LIBMAN_NO_ROOM
    SOURCES += room/libman_no_room_stubs.cpp
} else {
    SOURCES += \
        room/roomcellreader.cpp \
        room/roomReadAsync.cpp \
        room/roomKlayoutBridge.cpp
}

HEADERS += \
    lstream/lstreamcellwriter.h \
    src/mainwindow.h \
    src/assistant/assistantmcp.h \
    src/assistant/assistantagent.h \
    src/assistant/assistantchatpanel.h \
    src/assistant/securestore.h \
    src/assistant/roommcpclient.h \
    src/python_console.h \
    src/klayout_tools.h \
    extension/variantmanager.h \
    extension/variantfactory.h \
    extension/qlineeditd2.h \
    extension/filepathmanager.h \
    extension/fileeditfactory.h \
    extension/fileedit.h \
    QtPropertyBrowser/qtvariantproperty.h \
    QtPropertyBrowser/qttreepropertybrowser.h \
    QtPropertyBrowser/qtpropertymanager.h \
    QtPropertyBrowser/qtpropertybrowserutils_p.h \
    QtPropertyBrowser/qtpropertybrowser.h \
    QtPropertyBrowser/qtgroupboxpropertybrowser.h \
    QtPropertyBrowser/qteditorfactory.h \
    QtPropertyBrowser/qtbuttonpropertybrowser.h \
    gds/gdsreader.h \
    src/libfileparser.h \
    src/libdefine_utils.h \
    oas/oasReader.h \
    src/lstreamcellreader.h \
    src/projectmanager.h \
    src/projecteditor.h \
    src/importdialog.h \
    src/exportdialog.h \
    src/room_import_service.h \
    src/room_export_service.h \
    src/property.h \
    src/toolmanager.h \
    src/view_tools.h \
    src/viewtoolstablewidget.h \
    src/toolpickerdialog.h \
    src/about.h \
    src/keybindings.h \
    src/keybindingsdialog.h \
    src/newview.h \
    src/snapshotscene.h \
    src/snapshotview.h \
    room/roomcellreader.h \
    room/roomKlayoutBridge.h \
    room/room_path_utils.h \
    room/room_file_lock.h \
    room/converter_paths.h

FORMS += \
    src/mainwindow.ui \
    src/projectmanager.ui \
    src/projecteditor.ui \
    src/importdialog.ui \
    src/exportdialog.ui \
    src/toolmanager.ui \
    src/about.ui \
    src/newview.ui

RESOURCES += icons.qrc

# -----------------------------
# Cap'n Proto (clone + build on first make via capnp_deps.pri)
# -----------------------------
# Repo root (directory of this .pro file), not the shadow-build cwd.
LIBMAN_ROOT = $$dirname(_PRO_FILE_)
include(capnp_deps.pri)
!contains(CONFIG, no_room) {
    include(room_deps.pri)
}

# Generated Cap'n Proto files
# Must be listed explicitly so qmake knows them.
# -----------------------------
SOURCES += \
    capnp/cell.capnp.cc \
    capnp/geometry.capnp.cc \
    capnp/header.capnp.cc \
    capnp/layoutView.capnp.cc \
    capnp/library.capnp.cc \
    capnp/metaData.capnp.cc \
    capnp/metaDataView.capnp.cc \
    capnp/propertySet.capnp.cc \
    capnp/repetition.capnp.cc \
    capnp/variant.capnp.cc

HEADERS += \
    capnp/cell.capnp.h \
    capnp/geometry.capnp.h \
    capnp/header.capnp.h \
    capnp/layoutView.capnp.h \
    capnp/library.capnp.h \
    capnp/metaData.capnp.h \
    capnp/metaDataView.capnp.h \
    capnp/propertySet.capnp.h \
    capnp/repetition.capnp.h \
    capnp/variant.capnp.h

include(capnp_deps_finalize.pri)
!contains(CONFIG, no_room) {
    include(room_deps_finalize.pri)
    include(core_converter_deploy.pri)
}

# CML driver next to the binary
CONSOLE_DST = $$OUT_PWD
!isEmpty(DESTDIR): CONSOLE_DST = $$OUT_PWD/$$DESTDIR
win32 {
    QMAKE_POST_LINK += $$quote(cmd //c if not exist $$shell_path($$CONSOLE_DST/python) mkdir $$shell_path($$CONSOLE_DST/python))$$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(cmd //c copy /Y $$shell_path($$LIBMAN_ROOT/python/door_console.py) $$shell_path($$CONSOLE_DST/python/door_console.py))$$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(cmd //c copy /Y $$shell_path($$LIBMAN_ROOT/python/libman_console.py) $$shell_path($$CONSOLE_DST/python/libman_console.py))$$escape_expand(\\n\\t)
} else {
    QMAKE_POST_LINK += $$quote(mkdir -p $$shell_path($$CONSOLE_DST/python) && cp -f $$shell_path($$LIBMAN_ROOT/python/door_console.py) $$shell_path($$CONSOLE_DST/python/door_console.py) && cp -f $$shell_path($$LIBMAN_ROOT/python/libman_console.py) $$shell_path($$CONSOLE_DST/python/libman_console.py))$$escape_expand(\\n\\t)
}
