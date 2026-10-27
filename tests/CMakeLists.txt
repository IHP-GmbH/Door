cmake_minimum_required(VERSION 3.16)

project(LibManTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LIBMAN_ENABLE_COVERAGE "Enable GCC/MinGW coverage instrumentation" ON)

if(LIBMAN_ENABLE_COVERAGE)
    if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-O0 -g --coverage -fprofile-abs-path)
        add_link_options(--coverage)
        add_compile_definitions(COVERAGE_BUILD)
    endif()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/../src
)
set(CMAKE_AUTORCC ON)

enable_testing()

set(LIBMAN_ROOT "${CMAKE_SOURCE_DIR}/..")

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Test)

if(WIN32)
    set(ZLIB_INCLUDE_DIR "C:/Qt/Tools/mingw810_64/x86_64-w64-mingw32/include")
    set(ZLIB_LIBRARY "C:/Qt/Tools/mingw810_64/x86_64-w64-mingw32/lib/libz.a")
endif()

find_package(ZLIB REQUIRED)

add_executable(tst_libman_gui
    main.cpp

    tst_libman_gui.h
    tst_dialogs.h
    tst_klayout_requests.h
    tst_libfileparser.h
    tst_libman_layoutview_create.h
    tst_libman_viewops.h
    tst_lstream_writer.h
    tst_mainwindow_categories.h
    tst_mainwindow_loaders.h
    tst_oas_writer.h
    tst_toolmanager.h
    tst_coverage_expansion.h
    tst_coverage_80.h

    tst_dialogs.cpp
    tst_klayout_requests.cpp
    tst_libfileparser.cpp
    tst_libman_gui_gds.cpp
    tst_libman_layoutview_create.cpp
    tst_libman_viewops.cpp
    tst_lstream_writer.cpp
    tst_mainwindow_categories.cpp
    tst_mainwindow_loaders.cpp
    tst_oas_writer.cpp
    tst_toolmanager.cpp
    tst_coverage_expansion.cpp
    tst_coverage_80.cpp

    ${LIBMAN_ROOT}/src/mainwindow.cpp
    ${LIBMAN_ROOT}/src/python_console.cpp
    ${LIBMAN_ROOT}/src/libfileparser.cpp
    ${LIBMAN_ROOT}/src/door_pdk.cpp
    ${LIBMAN_ROOT}/src/libdefine_utils.cpp
    ${LIBMAN_ROOT}/src/projectmanager.cpp
    ${LIBMAN_ROOT}/src/projectfile.cpp
    ${LIBMAN_ROOT}/src/property.cpp
    ${LIBMAN_ROOT}/src/toolmanager.cpp
    ${LIBMAN_ROOT}/src/toolcustomtool.cpp
    ${LIBMAN_ROOT}/src/view_tools.cpp
    ${LIBMAN_ROOT}/src/viewtoolstablewidget.cpp
    ${LIBMAN_ROOT}/src/viewtoolstablewidget.h
    ${LIBMAN_ROOT}/src/toolpickerdialog.cpp
    ${LIBMAN_ROOT}/src/toolpickerdialog.h
    ${LIBMAN_ROOT}/src/categories.cpp
    ${LIBMAN_ROOT}/src/viewcontextmenu.cpp
    ${LIBMAN_ROOT}/src/groupcontextmenu.cpp
    ${LIBMAN_ROOT}/src/projectcontextmenu.cpp
    ${LIBMAN_ROOT}/src/categorycontextmenu.cpp
    ${LIBMAN_ROOT}/src/about.cpp
    ${LIBMAN_ROOT}/src/keybindings.cpp
    ${LIBMAN_ROOT}/src/keybindingsdialog.cpp
    ${LIBMAN_ROOT}/src/newview.cpp
    ${LIBMAN_ROOT}/src/snapshotscene.cpp
    ${LIBMAN_ROOT}/src/snapshotview.cpp
    ${LIBMAN_ROOT}/src/klayout_tools.cpp
    ${LIBMAN_ROOT}/src/klayout_tools.h
    ${LIBMAN_ROOT}/src/klayoutCellResolver.cpp
    ${LIBMAN_ROOT}/src/lstreamcellreader.cpp

    ${LIBMAN_ROOT}/gds/gdsreader.cpp
    ${LIBMAN_ROOT}/gds/gdsReadAsync.cpp

    ${LIBMAN_ROOT}/oas/oasCreate.cpp
    ${LIBMAN_ROOT}/oas/oasReader.cpp
    ${LIBMAN_ROOT}/oas/oasReadAsync.cpp

    ${LIBMAN_ROOT}/lstream/lstreamcellwriter.cpp
    ${LIBMAN_ROOT}/lstream/lstrReadAsync.cpp

    ${LIBMAN_ROOT}/extension/variantmanager.cpp
    ${LIBMAN_ROOT}/extension/variantfactory.cpp
    ${LIBMAN_ROOT}/extension/qlineeditd2.cpp
    ${LIBMAN_ROOT}/extension/filepathmanager.cpp
    ${LIBMAN_ROOT}/extension/fileeditfactory.cpp
    ${LIBMAN_ROOT}/extension/fileedit.cpp

    ${LIBMAN_ROOT}/QtPropertyBrowser/qtvariantproperty.cpp
    ${LIBMAN_ROOT}/QtPropertyBrowser/qttreepropertybrowser.cpp
    ${LIBMAN_ROOT}/QtPropertyBrowser/qtpropertymanager.cpp
    ${LIBMAN_ROOT}/QtPropertyBrowser/qtpropertybrowserutils.cpp
    ${LIBMAN_ROOT}/QtPropertyBrowser/qtpropertybrowser.cpp
    ${LIBMAN_ROOT}/QtPropertyBrowser/qtgroupboxpropertybrowser.cpp
    ${LIBMAN_ROOT}/QtPropertyBrowser/qteditorfactory.cpp
    ${LIBMAN_ROOT}/QtPropertyBrowser/qtbuttonpropertybrowser.cpp

    ${LIBMAN_ROOT}/capnp/cell.capnp.cc
    ${LIBMAN_ROOT}/capnp/geometry.capnp.cc
    ${LIBMAN_ROOT}/capnp/header.capnp.cc
    ${LIBMAN_ROOT}/capnp/layoutView.capnp.cc
    ${LIBMAN_ROOT}/capnp/library.capnp.cc
    ${LIBMAN_ROOT}/capnp/metaData.capnp.cc
    ${LIBMAN_ROOT}/capnp/metaDataView.capnp.cc
    ${LIBMAN_ROOT}/capnp/propertySet.capnp.cc
    ${LIBMAN_ROOT}/capnp/repetition.capnp.cc
    ${LIBMAN_ROOT}/capnp/variant.capnp.cc

    ${LIBMAN_ROOT}/src/mainwindow.ui
    ${LIBMAN_ROOT}/src/projectmanager.ui
    ${LIBMAN_ROOT}/src/toolmanager.ui
    ${LIBMAN_ROOT}/src/about.ui
    ${LIBMAN_ROOT}/src/newview.ui

    ${LIBMAN_ROOT}/icons.qrc
)

target_compile_definitions(tst_libman_gui PRIVATE
    QT_NO_DEPRECATED_WARNINGS
    LIBMAN_TESTING
)

if(WIN32)
    target_compile_options(tst_libman_gui PRIVATE -Wa,-mbig-obj)
endif()

target_include_directories(tst_libman_gui PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LIBMAN_ROOT}
    ${LIBMAN_ROOT}/src
    ${LIBMAN_ROOT}/gds
    ${LIBMAN_ROOT}/oas
    ${LIBMAN_ROOT}/lstream
    ${LIBMAN_ROOT}/extension
    ${LIBMAN_ROOT}/QtPropertyBrowser
    ${LIBMAN_ROOT}/capnp
    ${LIBMAN_ROOT}/capnp-install/include
)

target_link_directories(tst_libman_gui PRIVATE
    ${LIBMAN_ROOT}/capnp-install/lib
)

target_link_libraries(tst_libman_gui PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
    Qt5::Test
    ZLIB::ZLIB
    capnp
    kj
)

add_test(NAME tst_libman_gui COMMAND tst_libman_gui)

find_program(GCOVR_EXECUTABLE gcovr)

if(GCOVR_EXECUTABLE)
    add_custom_target(report
        COMMAND ${GCOVR_EXECUTABLE}
            -r ${LIBMAN_ROOT}
            --html-details
            -o coverage.html
            --print-summary
            --merge-mode-functions=merge-use-line-min

            --filter "${LIBMAN_ROOT}/src/.*"
            --filter "${LIBMAN_ROOT}/gds/.*"
            --filter "${LIBMAN_ROOT}/oas/.*"
            --filter "${LIBMAN_ROOT}/lstream/.*"
            --filter "${LIBMAN_ROOT}/extension/.*"
            --filter "${LIBMAN_ROOT}/QtPropertyBrowser/.*"

            --exclude ".*moc_.*"
            --exclude ".*qrc_.*"
            --exclude ".*ui_.*"
            --exclude ".*/tests/build/.*"
            --exclude ".*/build.*/.*"
            --exclude ".*/Desktop_Qt_.*"
            --exclude ".*/capnp/.*"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report"
    )
endif()