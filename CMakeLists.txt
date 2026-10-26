cmake_minimum_required(VERSION 3.16)

project(DOOR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src
)
set(CMAKE_AUTORCC ON)

add_compile_definitions(QT_NO_DEPRECATED_WARNINGS)

if(WIN32)
    add_compile_options(-Wa,-mbig-obj)
endif()

add_compile_options(-g1)

if(NOT Qt5_DIR)
    set(Qt5_DIR "C:/Qt/5.15.2/mingw81_64/lib/cmake/Qt5")
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)

if(WIN32)
    set(ZLIB_INCLUDE_DIR "C:/Qt/Tools/mingw810_64/x86_64-w64-mingw32/include")
    set(ZLIB_LIBRARY "C:/Qt/Tools/mingw810_64/x86_64-w64-mingw32/lib/libz.a")
endif()

find_package(ZLIB REQUIRED)

# -----------------------------------------------------------------------------
# Cap'n Proto paths
# -----------------------------------------------------------------------------

set(CAPNP_ROOT "${CMAKE_SOURCE_DIR}/capnp-install")
set(CAPNP_REPO_DIR "${CMAKE_SOURCE_DIR}/capnproto")
set(CAPNP_GEN_DIR "${CMAKE_SOURCE_DIR}/capnp")

set(CAPNP_GIT_URL "https://github.com/capnproto/capnproto.git")
set(CAPNP_VERSION_MODE "branch")
set(CAPNP_GIT_BRANCH "master")
set(CAPNP_GIT_TAG "")
set(CAPNP_GIT_COMMIT "")

set(LSTREAM_GIT_URL "https://codeberg.org/klayoutmatthias/lstream.git")
set(LSTREAM_VERSION_MODE "branch")
set(LSTREAM_GIT_BRANCH "main")
set(LSTREAM_GIT_TAG "")
set(LSTREAM_GIT_COMMIT "")
set(LSTREAM_SCHEMA_REPO_DIR "${CMAKE_SOURCE_DIR}/.deps/lstream")

# -----------------------------------------------------------------------------
# External dependency preparation
# -----------------------------------------------------------------------------

set(CAPNP_STAMP "${CAPNP_ROOT}/capnp_install_stamp")
set(LSTREAM_STAMP "${CAPNP_GEN_DIR}/schemas_built_stamp")

if(WIN32)
    add_custom_command(
        OUTPUT "${CAPNP_STAMP}"
        COMMAND cmd /c
            "${CMAKE_SOURCE_DIR}/scripts/mkcapnp.cmd"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building Cap'n Proto for Windows"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${LSTREAM_STAMP}"
        COMMAND cmd /c
            "${CMAKE_SOURCE_DIR}/scripts/mklstream.cmd"
        DEPENDS "${CAPNP_STAMP}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Updating LStream schemas for Windows"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${CAPNP_STAMP}"
        COMMAND bash
            "${CMAKE_SOURCE_DIR}/scripts/build_capnp_linux.sh"
            "${CAPNP_GIT_URL}"
            "${CAPNP_VERSION_MODE}"
            "${CAPNP_GIT_BRANCH}"
            "${CAPNP_GIT_TAG}"
            "${CAPNP_GIT_COMMIT}"
            "${CAPNP_REPO_DIR}"
            "${CAPNP_ROOT}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building Cap'n Proto for Linux"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${LSTREAM_STAMP}"
        COMMAND bash
            "${CMAKE_SOURCE_DIR}/scripts/update_lstream_schemas_linux.sh"
            "${LSTREAM_GIT_URL}"
            "${LSTREAM_VERSION_MODE}"
            "${LSTREAM_GIT_BRANCH}"
            "${LSTREAM_GIT_TAG}"
            "${LSTREAM_GIT_COMMIT}"
            "${LSTREAM_SCHEMA_REPO_DIR}"
            "${CAPNP_GEN_DIR}"
            "${CAPNP_ROOT}"
        DEPENDS "${CAPNP_STAMP}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Updating LStream schemas for Linux"
        VERBATIM
    )
endif()

add_custom_target(prepare_libman_deps
    DEPENDS "${LSTREAM_STAMP}"
)

# -----------------------------------------------------------------------------
# ROOM (CommonDB) — optional; private repository (auto-detected via GitHub probe)
# -----------------------------------------------------------------------------

include("${CMAKE_SOURCE_DIR}/cmake/ProbeCoreAccess.cmake")

option(LIBMAN_FORCE_ROOM "Build with ROOM even if GitHub probe fails" OFF)
option(LIBMAN_FORCE_NO_ROOM "Build without ROOM even if GitHub probe succeeds" OFF)

libman_probe_core_access(LIBMAN_ROOM_ACCESSIBLE
    FORCE_ROOM ${LIBMAN_FORCE_ROOM}
    FORCE_NO_ROOM ${LIBMAN_FORCE_NO_ROOM}
)

set(LIBMAN_USE_ROOM ${LIBMAN_ROOM_ACCESSIBLE})

set(LIBMAN_ROOM_SOURCES)
set(LIBMAN_ROOM_LINK_LIBS)
if(LIBMAN_USE_ROOM)
    include("${CMAKE_SOURCE_DIR}/cmake/FetchRoom.cmake")
    list(APPEND LIBMAN_ROOM_SOURCES
        room/roomcellreader.cpp
        room/roomKlayoutBridge.cpp
        room/roomReadAsync.cpp
    )
    list(APPEND LIBMAN_ROOM_LINK_LIBS ROOM::room ROOM::room_utils)
    message(STATUS "DOOR: building with ROOM")
else()
    message(STATUS "DOOR: building without ROOM (CommonDB/Room not reachable). Set LIBMAN_CORE_GIT_TOKEN or LIBMAN_ROOM_SOURCE_DIR for full ROOM.")
endif()

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------

set(SOURCES
    room/room_path_utils.cpp
    room/room_file_lock.cpp
    room/converter_paths.cpp
    lstream/lstreamcellwriter.cpp
    oas/oasCreate.cpp
    src/main.cpp
    gds/gdsReadAsync.cpp
    src/libfileparser.cpp
    src/libdefine_utils.cpp
    oas/oasReadAsync.cpp
    oas/oasReader.cpp
    src/klayout_tools.cpp
    src/klayout_tools.h
    src/klayoutCellResolver.cpp
    src/lstreamcellreader.cpp
    src/mainwindow.cpp
    src/python_console.cpp
    src/assistant/assistantmcp.cpp
    src/assistant/assistantagent.cpp
    src/assistant/assistantchatpanel.cpp
    src/assistant/securestore.cpp
    src/assistant/roommcpclient.cpp

    extension/variantmanager.cpp
    extension/variantfactory.cpp
    extension/qlineeditd2.cpp
    extension/filepathmanager.cpp
    extension/fileeditfactory.cpp
    extension/fileedit.cpp

    QtPropertyBrowser/qtvariantproperty.cpp
    QtPropertyBrowser/qttreepropertybrowser.cpp
    QtPropertyBrowser/qtpropertymanager.cpp
    QtPropertyBrowser/qtpropertybrowserutils.cpp
    QtPropertyBrowser/qtpropertybrowser.cpp
    QtPropertyBrowser/qtgroupboxpropertybrowser.cpp
    QtPropertyBrowser/qteditorfactory.cpp
    QtPropertyBrowser/qtbuttonpropertybrowser.cpp

    gds/gdsreader.cpp
    src/projectmanager.cpp
    src/projecteditor.cpp
    src/importdialog.cpp
    src/exportdialog.cpp
    src/room_import_service.cpp
    src/room_export_service.cpp
    lstream/lstrReadAsync.cpp
    src/property.cpp
    src/toolcustomtool.cpp
    src/toolmanager.cpp
    src/view_tools.cpp
    src/viewtoolstablewidget.cpp
    src/toolpickerdialog.cpp
    src/projectfile.cpp
    src/categories.cpp
    src/viewcontextmenu.cpp
    src/groupcontextmenu.cpp
    src/projectcontextmenu.cpp
    src/categorycontextmenu.cpp
    src/about.cpp
    src/keybindings.cpp
    src/keybindingsdialog.cpp
    src/newview.cpp
    src/snapshotscene.cpp
    src/snapshotview.cpp

    capnp/cell.capnp.cc
    capnp/geometry.capnp.cc
    capnp/header.capnp.cc
    capnp/layoutView.capnp.cc
    capnp/library.capnp.cc
    capnp/metaData.capnp.cc
    capnp/metaDataView.capnp.cc
    capnp/propertySet.capnp.cc
    capnp/repetition.capnp.cc
    capnp/variant.capnp.cc
)

list(APPEND SOURCES ${LIBMAN_ROOM_SOURCES})

if(NOT LIBMAN_USE_ROOM)
    list(APPEND SOURCES room/libman_no_room_stubs.cpp)
endif()

set(HEADERS
    room/room_path_utils.h
    room/room_file_lock.h
    room/converter_paths.h
    room/roomcellreader.h
    room/roomKlayoutBridge.h
    lstream/lstreamcellwriter.h
    src/mainwindow.h
    src/python_console.h
    src/assistant/assistantmcp.h
    src/assistant/assistantagent.h
    src/assistant/assistantchatpanel.h
    src/assistant/securestore.h
    src/assistant/roommcpclient.h

    extension/variantmanager.h
    extension/variantfactory.h
    extension/qlineeditd2.h
    extension/filepathmanager.h
    extension/fileeditfactory.h
    extension/fileedit.h

    QtPropertyBrowser/qtvariantproperty.h
    QtPropertyBrowser/qttreepropertybrowser.h
    QtPropertyBrowser/qtpropertymanager.h
    QtPropertyBrowser/qtpropertybrowserutils_p.h
    QtPropertyBrowser/qtpropertybrowser.h
    QtPropertyBrowser/qtgroupboxpropertybrowser.h
    QtPropertyBrowser/qteditorfactory.h
    QtPropertyBrowser/qtbuttonpropertybrowser.h

    gds/gdsreader.h
    src/libfileparser.h
    src/libdefine_utils.h
    oas/oasReader.h
    src/lstreamcellreader.h
    src/projectmanager.h
    src/projecteditor.h
    src/importdialog.h
    src/exportdialog.h
    src/room_import_service.h
    src/room_export_service.h
    src/property.h
    src/toolmanager.h
    src/view_tools.h
    src/viewtoolstablewidget.h
    src/toolpickerdialog.h
    src/about.h
    src/keybindings.h
    src/keybindingsdialog.h
    src/newview.h

    capnp/cell.capnp.h
    capnp/geometry.capnp.h
    capnp/header.capnp.h
    capnp/layoutView.capnp.h
    capnp/library.capnp.h
    capnp/metaData.capnp.h
    capnp/metaDataView.capnp.h
    capnp/propertySet.capnp.h
    capnp/repetition.capnp.h
    capnp/variant.capnp.h
)

set(FORMS
    src/mainwindow.ui
    src/projectmanager.ui
    src/projecteditor.ui
    src/importdialog.ui
    src/exportdialog.ui
    src/toolmanager.ui
    src/about.ui
    src/newview.ui
)

set(RESOURCES
    icons.qrc
)

add_executable(door
    ${SOURCES}
    ${HEADERS}
    ${FORMS}
    ${RESOURCES}
)

if(WIN32)
    target_sources(door PRIVATE "${CMAKE_SOURCE_DIR}/icons/door.rc")
endif()

add_dependencies(door prepare_libman_deps)
if(TARGET room)
    add_dependencies(door room)
endif()

if(NOT LIBMAN_USE_ROOM)
    target_compile_definitions(door PRIVATE LIBMAN_NO_ROOM)
endif()

target_include_directories(door PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/assistant
    ${CMAKE_SOURCE_DIR}/extension
    ${CMAKE_SOURCE_DIR}/QtPropertyBrowser
    ${CAPNP_GEN_DIR}
    ${CAPNP_ROOT}/include
)

target_link_directories(door PRIVATE
    ${CAPNP_ROOT}/lib
)

target_link_libraries(door PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
    Qt5::Concurrent
    ZLIB::ZLIB
    capnp
    kj
    ${LIBMAN_ROOM_LINK_LIBS}
)

if(WIN32)
    target_link_libraries(door PRIVATE crypt32)
endif()

# CML driver next to the binary (python/door_console.py).
add_custom_command(TARGET door POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:door>/python"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/python/door_console.py"
            "$<TARGET_FILE_DIR:door>/python/door_console.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/python/libman_console.py"
            "$<TARGET_FILE_DIR:door>/python/libman_console.py"
    COMMENT "Deploying door_console.py next to door"
)

if(LIBMAN_USE_ROOM)
foreach(_converter gds_to_room xschem_to_room qucs_to_room oas_to_room json_to_room room_to_gds room_to_xschem room_to_qucs room_to_json)
    if(TARGET ${_converter})
        add_custom_command(TARGET door POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${_converter}>"
                    "$<TARGET_FILE_DIR:door>"
            COMMENT "Deploying ${_converter} next to door"
        )
    endif()
endforeach()
endif()
