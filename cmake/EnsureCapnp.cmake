# Ensure Door's Cap'n Proto prefix exists before configuring ROOM (FetchContent).

function(libman_ensure_capnp)
    set(_capnp_header "${CMAKE_SOURCE_DIR}/capnp-install/include/capnp/message.h")
    if(EXISTS "${_capnp_header}")
        return()
    endif()

    message(STATUS "Cap'n Proto not found in ${CMAKE_SOURCE_DIR}/capnp-install")
    message(STATUS "Bootstrapping Cap'n Proto (first configure may take several minutes)...")

    if(WIN32)
        execute_process(
            COMMAND cmd /c "${CMAKE_SOURCE_DIR}/scripts/mkcapnp.cmd"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _capnp_result
            OUTPUT_VARIABLE _capnp_out
            ERROR_VARIABLE _capnp_err
        )
    else()
        execute_process(
            COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/build_capnp_linux.sh"
                "https://github.com/capnproto/capnproto.git"
                "branch"
                "master"
                ""
                ""
                "${CMAKE_SOURCE_DIR}/capnproto"
                "${CMAKE_SOURCE_DIR}/capnp-install"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE _capnp_result
            OUTPUT_VARIABLE _capnp_out
            ERROR_VARIABLE _capnp_err
        )
    endif()

    if(_capnp_result)
        message("${_capnp_out}")
        message("${_capnp_err}")
        message(FATAL_ERROR
            "Failed to bootstrap Cap'n Proto (exit ${_capnp_result}).\n"
            "Run manually: scripts/mkcapnp.cmd (Windows) or scripts/build_capnp_linux.sh (Linux).")
    endif()

    if(NOT EXISTS "${_capnp_header}")
        message(FATAL_ERROR
            "Cap'n Proto bootstrap finished but ${_capnp_header} is missing.")
    endif()

    message(STATUS "Cap'n Proto ready at ${CMAKE_SOURCE_DIR}/capnp-install")
endfunction()
