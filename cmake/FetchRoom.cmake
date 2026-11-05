# Fetch and build ROOM (CommonDB) from GitHub as a CMake subproject.

include(FetchContent)

set(ROOM_GIT_URL "https://github.com/IHP-GmbH/Room.git"
    CACHE STRING "ROOM (CommonDB) Git repository URL")
set(ROOM_GIT_TAG "main"
    CACHE STRING "ROOM Git branch, tag, or commit hash")
set(LIBMAN_ROOM_SOURCE_DIR ""
    CACHE PATH "Local ROOM/CommonDB checkout (skips git fetch; for development)")

option(LIBMAN_FETCH_ROOM "Download and build ROOM from GitHub" ON)

include("${CMAKE_CURRENT_LIST_DIR}/EnsureCapnp.cmake")

function(_libman_configure_room_subproject)
    libman_ensure_capnp()

    set(CAPNP_ROOT "${CMAKE_SOURCE_DIR}/capnp-install" CACHE PATH "Cap'n Proto install prefix" FORCE)
    set(ROOM_BOOTSTRAP_CAPNP OFF CACHE BOOL "ROOM uses LibMan Cap'n Proto prefix" FORCE)
    set(ROOM_BUILD_TESTS OFF CACHE BOOL "Do not build ROOM tests inside LibMan" FORCE)
    set(ROOM_BUILD_OAS_TESTS OFF CACHE BOOL "Do not build ROOM OAS tests inside LibMan" FORCE)
    set(ROOM_BUILD_EXAMPLES ON CACHE BOOL "Build ROOM converter tools for LibMan Import" FORCE)
endfunction()

function(_libman_add_room_aliases)
    if(TARGET room AND NOT TARGET ROOM::room)
        add_library(ROOM::room ALIAS room)
    endif()
    if(TARGET room_utils AND NOT TARGET ROOM::room_utils)
        add_library(ROOM::room_utils ALIAS room_utils)
    endif()
    if(TARGET room_oas AND NOT TARGET ROOM::room_oas)
        add_library(ROOM::room_oas ALIAS room_oas)
    endif()
endfunction()

if(LIBMAN_ROOM_SOURCE_DIR)
  if(NOT IS_DIRECTORY "${LIBMAN_ROOM_SOURCE_DIR}")
    message(FATAL_ERROR "LIBMAN_ROOM_SOURCE_DIR is not a directory: ${LIBMAN_ROOM_SOURCE_DIR}")
  endif()
  message(STATUS "Using local ROOM tree: ${LIBMAN_ROOM_SOURCE_DIR}")
  _libman_configure_room_subproject()
  add_subdirectory("${LIBMAN_ROOM_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/commondb-build")
  _libman_add_room_aliases()
elseif(LIBMAN_FETCH_ROOM)
  _libman_configure_room_subproject()

  set(_ROOM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.deps/Room")

  FetchContent_Declare(
      commondb
      GIT_REPOSITORY "${ROOM_GIT_URL}"
      GIT_TAG "${ROOM_GIT_TAG}"
      GIT_SHALLOW TRUE
      SOURCE_DIR "${_ROOM_SOURCE_DIR}"
      BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/commondb-build"
  )

  message(STATUS "Fetching ROOM from ${ROOM_GIT_URL} (${ROOM_GIT_TAG})...")
  FetchContent_MakeAvailable(commondb)
  _libman_add_room_aliases()
else()
  find_package(ROOM REQUIRED)
endif()

if(NOT TARGET ROOM::room)
    message(FATAL_ERROR "ROOM target ROOM::room is missing after configuration.")
endif()

message(STATUS "ROOM linked (ROOM::room, ROOM::room_utils, ROOM::room_oas)")
