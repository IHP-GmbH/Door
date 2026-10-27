# -------------------------------------------------
# LibMan GUI tests (QtTest)
# -------------------------------------------------

QT += core gui widgets testlib concurrent network
LIBS += -lz
win32:LIBS += -lcrypt32

TEMPLATE = app
TARGET = tst_libman_gui
CONFIG += console c++17 debug
CONFIG+=coverage
DEFINES += QT_NO_DEPRECATED_WARNINGS LIBMAN_TESTING

win32:QMAKE_CXXFLAGS += -Wa,-mbig-obj

INCLUDEPATH += \
    $$PWD/.. \
    $$PWD/../src \
    $$PWD/../src/assistant \
    $$PWD/../gds \
    $$PWD/../oas \
    $$PWD/../lstream \
    $$PWD/../extension \
    $$PWD/../QtPropertyBrowser

DEPENDPATH += $$INCLUDEPATH

# IMPORTANT:
# Do NOT include src/main.cpp here (QTEST_MAIN provides main())!
SOURCES += \
    $$PWD/tst_libman_gui_gds.cpp \
    $$PWD/../gds/gdsReadAsync.cpp \
    $$PWD/../oas/oasReadAsync.cpp \
    $$PWD/../oas/oasReader.cpp \
    $$PWD/../oas/oasCreate.cpp \
    $$PWD/../lstream/lstreamcellwriter.cpp \
    $$PWD/../lstream/lstrReadAsync.cpp \
    $$PWD/../src/libfileparser.cpp \
    $$PWD/../src/libdefine_utils.cpp \
    $$PWD/../src/door_pdk.cpp \
    $$PWD/../src/door_pdk_descriptor_dialog.cpp \
    $$PWD/../src/klayout_tools.cpp \
    $$PWD/../src/klayoutCellResolver.cpp \
    $$PWD/../src/lstreamcellreader.cpp \
    $$PWD/../src/mainwindow.cpp \
    $$PWD/../src/assistant/assistantmcp.cpp \
    $$PWD/../src/assistant/assistantagent.cpp \
    $$PWD/../src/assistant/assistantchatpanel.cpp \
    $$PWD/../src/assistant/securestore.cpp \
    $$PWD/../src/assistant/roommcpclient.cpp \
    $$PWD/../extension/variantmanager.cpp \
    $$PWD/../extension/variantfactory.cpp \
    $$PWD/../extension/qlineeditd2.cpp \
    $$PWD/../extension/filepathmanager.cpp \
    $$PWD/../extension/fileeditfactory.cpp \
    $$PWD/../extension/fileedit.cpp \
    $$PWD/../QtPropertyBrowser/qtvariantproperty.cpp \
    $$PWD/../QtPropertyBrowser/qttreepropertybrowser.cpp \
    $$PWD/../QtPropertyBrowser/qtpropertymanager.cpp \
    $$PWD/../QtPropertyBrowser/qtpropertybrowserutils.cpp \
    $$PWD/../QtPropertyBrowser/qtpropertybrowser.cpp \
    $$PWD/../QtPropertyBrowser/qtgroupboxpropertybrowser.cpp \
    $$PWD/../QtPropertyBrowser/qteditorfactory.cpp \
    $$PWD/../QtPropertyBrowser/qtbuttonpropertybrowser.cpp \
    $$PWD/../gds/gdsreader.cpp \
    $$PWD/../src/projectmanager.cpp \
    $$PWD/../src/projecteditor.cpp \
    $$PWD/../src/importdialog.cpp \
    $$PWD/../src/exportdialog.cpp \
    $$PWD/../src/room_import_service.cpp \
    $$PWD/../src/room_export_service.cpp \
    $$PWD/../src/property.cpp \
    $$PWD/../src/toolcustomtool.cpp \
    $$PWD/../src/view_tools.cpp \
    $$PWD/../src/viewtoolstablewidget.cpp \
    $$PWD/../src/toolpickerdialog.cpp \
    $$PWD/../src/toolmanager.cpp \
    $$PWD/../src/projectfile.cpp \
    $$PWD/../src/categories.cpp \
    $$PWD/../src/viewcontextmenu.cpp \
    $$PWD/../src/groupcontextmenu.cpp \
    $$PWD/../src/projectcontextmenu.cpp \
    $$PWD/../src/categorycontextmenu.cpp \
    $$PWD/../src/about.cpp \
    $$PWD/../src/python_console.cpp \
    $$PWD/../src/keybindings.cpp \
    $$PWD/../src/keybindingsdialog.cpp \
    $$PWD/../src/newview.cpp \
    $$PWD/../src/snapshotscene.cpp \
    $$PWD/../src/snapshotview.cpp \
    $$PWD/../room/room_path_utils.cpp \
    $$PWD/../room/room_file_lock.cpp \
    $$PWD/../room/converter_paths.cpp \
    main.cpp \

LIBMAN_ROOT = $$dirname(_PRO_FILE_)/..
include($$LIBMAN_ROOT/core_build_config.pri)

contains(CONFIG, no_room) {
    DEFINES += LIBMAN_NO_ROOM
    SOURCES += $$PWD/../room/libman_no_room_stubs.cpp
} else {
    SOURCES += \
        $$PWD/../room/roomcellreader.cpp \
        $$PWD/../room/roomReadAsync.cpp \
        $$PWD/../room/roomKlayoutBridge.cpp
}

SOURCES += \
    tst_dialogs.cpp \
    tst_klayout_requests.cpp \
    tst_libfileparser.cpp \
    tst_libdefine_utils.cpp \
    tst_libman_layoutview_create.cpp \
    tst_libman_viewops.cpp \
    tst_lstream_writer.cpp \
    tst_mainwindow_categories.cpp \
    tst_mainwindow_loaders.cpp \
    tst_oas_writer.cpp \
    tst_toolmanager.cpp \
    tst_coverage_expansion.cpp \
    tst_coverage_80.cpp \
    tst_room_path_utils.cpp \
    tst_room_file_lock.cpp

HEADERS += \
    $$PWD/tst_libman_gui.h \
    $$PWD/../src/mainwindow.h \
    $$PWD/../src/door_pdk.h \
    $$PWD/../src/door_pdk_descriptor_dialog.h \
    $$PWD/../src/assistant/assistantmcp.h \
    $$PWD/../src/assistant/assistantagent.h \
    $$PWD/../src/assistant/assistantchatpanel.h \
    $$PWD/../src/assistant/securestore.h \
    $$PWD/../src/assistant/roommcpclient.h \
    $$PWD/../src/klayout_tools.h \
    $$PWD/../src/libfileparser.h \
    $$PWD/../src/lstreamcellreader.h \
    $$PWD/../extension/variantmanager.h \
    $$PWD/../extension/variantfactory.h \
    $$PWD/../extension/qlineeditd2.h \
    $$PWD/../extension/filepathmanager.h \
    $$PWD/../extension/fileeditfactory.h \
    $$PWD/../extension/fileedit.h \
    $$PWD/../QtPropertyBrowser/qtvariantproperty.h \
    $$PWD/../QtPropertyBrowser/qttreepropertybrowser.h \
    $$PWD/../QtPropertyBrowser/qtpropertymanager.h \
    $$PWD/../QtPropertyBrowser/qtpropertybrowserutils_p.h \
    $$PWD/../QtPropertyBrowser/qtpropertybrowser.h \
    $$PWD/../QtPropertyBrowser/qtgroupboxpropertybrowser.h \
    $$PWD/../QtPropertyBrowser/qteditorfactory.h \
    $$PWD/../QtPropertyBrowser/qtbuttonpropertybrowser.h \
    $$PWD/../gds/gdsreader.h \
    $$PWD/../oas/oasReader.h \
    $$PWD/../src/projectmanager.h \
    $$PWD/../src/projecteditor.h \
    $$PWD/../src/importdialog.h \
    $$PWD/../src/exportdialog.h \
    $$PWD/../src/room_import_service.h \
    $$PWD/../src/room_export_service.h \
    $$PWD/../src/property.h \
    $$PWD/../src/toolmanager.h \
    $$PWD/../src/viewtoolstablewidget.h \
    $$PWD/../src/toolpickerdialog.h \
    $$PWD/../src/about.h \
    $$PWD/../src/python_console.h \
    $$PWD/../src/keybindings.h \
    $$PWD/../src/keybindingsdialog.h \
    $$PWD/../src/newview.h \
    $$PWD/../src/snapshotscene.h \
    $$PWD/../src/snapshotview.h \
    $$PWD/../room/roomcellreader.h \
    $$PWD/../room/roomKlayoutBridge.h \
    tst_dialogs.h \
    tst_klayout_requests.h \
    tst_libfileparser.h \
    tst_libdefine_utils.h \
    tst_libman_layoutview_create.h \
    tst_libman_viewops.h \
    tst_lstream_writer.h \
    tst_mainwindow_categories.h \
    tst_mainwindow_loaders.h \
    tst_oas_writer.h \
    tst_toolmanager.h \
    tst_coverage_expansion.h \
    tst_coverage_80.h \
    tst_room_path_utils.h \
    tst_room_file_lock.h \
    test_paths.h

FORMS += \
    $$PWD/../src/mainwindow.ui \
    $$PWD/../src/projectmanager.ui \
    $$PWD/../src/projecteditor.ui \
    $$PWD/../src/importdialog.ui \
    $$PWD/../src/exportdialog.ui \
    $$PWD/../src/toolmanager.ui \
    $$PWD/../src/about.ui \
    $$PWD/../src/newview.ui

RESOURCES += \
    $$PWD/../icons.qrc

TESTDATA += \
    $$PWD/data/sg13g2.projects \
    $$PWD/data/ota1336.projects \
    $$PWD/data/OTA1336/schematic/xschem/OTA1336.schematic.room \
    $$PWD/data/OTA1336/schematic/xschem/OTA1336.symbol.room \
    $$PWD/data/OTA1336/testbenches/ac/xschem/OTA1336_tb.schematic.room \
    $$PWD/data/sample.gds \
    $$PWD/data/sg13g2_stdcell/Test/Test.gds \
    $$PWD/data/sg13g2_stdcell/Test/Test.oas \
    $$PWD/data/sg13g2_stdcell/Test/Test.lstr \
    $$PWD/data/sg13g2_stdcell/sg13g2_io/sg13g2_io.gds \
    $$PWD/data/sg13g2_stdcell/sg13g2_stdcell/sg13g2_stdcell.gds \
    $$PWD/data/sg13g2_stdcell/sg13g2_stdcell/sg13g2_stdcell.oas \
    $$PWD/data/sg13g2_stdcell/sg13g2_stdcell/sg13g2_stdcell.lstr \
    $$PWD/data/sg13g2_stdcell/sg13g2_stdcell/sg13g2_stdcell.layout.room \
    $$PWD/data/sg13g2_stdcell/sg13g2_stdcell/sg13g2_stdcell.schematic.room \
    $$PWD/data/sg13g2_stdcell/lstr/sg13g2_stdcell.lstr

# Cap'n Proto (same clone-on-make flow as libman.pro)
LIBMAN_ROOT = $$dirname(_PRO_FILE_)/..
include($$LIBMAN_ROOT/capnp_deps.pri)
!contains(CONFIG, no_room) {
    include($$LIBMAN_ROOT/room_deps.pri)
}

CAPNP_GEN_DIR = $$LIBMAN_ROOT/capnp
SOURCES += $$files($$CAPNP_GEN_DIR/*.cc)
HEADERS += $$files($$CAPNP_GEN_DIR/*.h)

coverage {
    win32-g++|unix:!macx {
        QMAKE_CXXFLAGS += -O0 -g --coverage -fprofile-abs-path
        QMAKE_CFLAGS   += -O0 -g --coverage -fprofile-abs-path
        QMAKE_LFLAGS   += --coverage

        DEFINES += COVERAGE_BUILD

        QMAKE_DISTCLEAN += *.gcda *.gcno

        report.commands = gcovr -r $$PWD/.. --html-details -o coverage.html --print-summary --exclude ".*moc_.*" --exclude ".*qrc_.*"
        QMAKE_EXTRA_TARGETS += report
    }
}

include($$LIBMAN_ROOT/capnp_deps_finalize.pri)
!contains(CONFIG, no_room) {
    include($$LIBMAN_ROOT/room_deps_finalize.pri)
}
