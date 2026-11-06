@echo off
rem Fetch and build ROOM (CommonDB) for qmake builds. Run from repo root.
setlocal EnableExtensions
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "CAPNP_ROOT=%ROOT%\capnp-install"
set "ROOM_BUILD=%ROOT%\.deps\room-build"
set "STAMP=%ROOM_BUILD%\libman_room_built.stamp"

if exist "%STAMP%" exit /b 0

if defined LIBMAN_ROOM_SOURCE_DIR (
    if exist "%LIBMAN_ROOM_SOURCE_DIR%" (
        set "ROOM_SRC=%LIBMAN_ROOM_SOURCE_DIR%"
        echo Using local ROOM tree: %ROOM_SRC%
        goto :configure
    )
)

set "ROOM_SRC=%ROOT%\.deps\CommonDB"
if exist "%ROOM_SRC%\src\room_paths.h" (
    echo Using existing ROOM checkout: %ROOM_SRC%
    goto :configure
)
if not exist "%ROOM_SRC%\.git" (
    if exist "%ROOM_SRC%\*" (
        echo ERROR: %ROOM_SRC% exists but is not a valid ROOM checkout
        exit /b 1
    )
    if defined LIBMAN_CORE_GIT_TOKEN (
        set "CLONE_URL=https://x-access-token:%LIBMAN_CORE_GIT_TOKEN%@github.com/IHP-GmbH/Room.git"
    ) else if defined GITHUB_TOKEN (
        set "CLONE_URL=https://x-access-token:%GITHUB_TOKEN%@github.com/IHP-GmbH/Room.git"
    ) else (
        set "CLONE_URL=https://github.com/IHP-GmbH/Room.git"
    )
    echo Cloning ROOM from GitHub...
    git clone --depth 1 --branch main "%CLONE_URL%" "%ROOM_SRC%"
    if errorlevel 1 exit /b 1
)

:configure
if not exist "%CAPNP_ROOT%\include\capnp\message.h" (
    echo ERROR: Cap'n Proto not found in %CAPNP_ROOT%. Run mkcapnp.cmd first.
    exit /b 1
)

rem Prefer MinGW 64-bit (Qt win64_mingw81). tools_mingw81 also ships mingw810_32 first on PATH.
if defined IQTA_TOOLS if exist "%IQTA_TOOLS%\mingw810_64\bin\g++.exe" (
    set "PATH=%IQTA_TOOLS%\mingw810_64\bin;%PATH%"
)
if exist "C:\Qt\Tools\mingw810_64\bin\g++.exe" (
    set "PATH=C:\Qt\Tools\mingw810_64\bin;%PATH%"
)

cmake -S "%ROOM_SRC%" -B "%ROOM_BUILD%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DROOM_BOOTSTRAP_CAPNP=OFF ^
    -DCAPNP_ROOT="%CAPNP_ROOT%" ^
    -DROOM_BUILD_TESTS=OFF ^
    -DROOM_BUILD_OAS_TESTS=OFF ^
    -DROOM_BUILD_EXAMPLES=ON ^
    -G "MinGW Makefiles"
if errorlevel 1 exit /b 1

cmake --build "%ROOM_BUILD%" --target room room_utils room_oas gds_to_room xschem_to_room qucs_to_room room_to_gds room_to_xschem room_to_qucs -j
if errorlevel 1 exit /b 1

cmake --build "%ROOM_BUILD%" --target oas_to_room -j 2>nul

type nul > "%STAMP%"
echo ROOM built in %ROOM_BUILD%
exit /b 0
