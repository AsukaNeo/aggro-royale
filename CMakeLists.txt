cmake_minimum_required(VERSION 3.20)

# Aggro Royale: Aggressive Inline (Xbox, 2002), statically recompiled with
# xboxrecomp. The toolkit sits next to this folder (../xboxrecomp), and the
# game's code is lifted into src/recomp/gen/ by lift.sh from your own copy.
# That folder is never committed.

project(aggro_royale C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit (v0.16.0 with patches/ applied)")
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# ── The game ────────────────────────────────────────────────────

# CONFIGURE_DEPENDS so a re-lift that adds or removes a file (an extra chunk,
# recomp_stubs_unresolved.c) is picked up without re-running cmake by hand;
# otherwise the new file is left out and shows up as unresolved externals.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "src/recomp/gen/*.c")

add_executable(${PROJECT_NAME} WIN32
    src/main.c
    src/recomp_manual.c          # hand-written replacements for game functions
    src/game_check.c             # is this the supported default.xbe?
    src/game_import.c            # first-run setup from a disc image or folder
    src/xiso.c                   # Xbox disc image reader
    src/prof_sampler.c           # sampling profiler, off unless RECOMP_PROF=1
    ${RECOMP_GEN_SOURCES}
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp
    ${XBOXRECOMP_DIR}/include
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    xboxrecomp
    d3d11 dxgi dxguid xinput winmm
    bcrypt                       # SHA-1 for the game check
    dbghelp                      # crash reports name the guest function that faulted
)
if(MSVC)
    # The generated files are huge: /bigobj for their section count.
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj /MP4)
    target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE /MAP)
endif()

# Window title prefix and settings file name (aggroroyale.ini).
target_compile_definitions(xbox_video PRIVATE RECOMP_APP_NAME="AggroRoyale")

# AR_RELEASE is the downloadable build (tools/make_release.py): it only looks
# for the game where a player keeps it, next to the exe or a folder they
# chose, and carries no path from the machine that built it. A dev build also
# falls back to the toolkit's game_files folder.
option(AR_RELEASE "Build for a downloadable release" OFF)
if(AR_RELEASE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AR_RELEASE=1)
else()
    file(TO_NATIVE_PATH "${XBOXRECOMP_DIR}/game_files" AR_FALLBACK_GAME_DIR_NATIVE)
    string(REPLACE "\\" "\\\\" AR_FALLBACK_GAME_DIR_C "${AR_FALLBACK_GAME_DIR_NATIVE}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        AR_FALLBACK_GAME_DIR="${AR_FALLBACK_GAME_DIR_C}")
endif()

set(AR_VERSION "dev" CACHE STRING "Version shown on the launcher")
target_compile_definitions(${PROJECT_NAME} PRIVATE AR_VERSION="${AR_VERSION}")

# The art embedded in the exe: the F10 overlay's logo, and the launcher's
# title card and corner mark. The .rc gets absolute paths so rc.exe finds
# the PNGs whatever folder it runs in.
set(AR_OVERLAY_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/assets/aggro-royale-logo.png")
set(AR_TITLE_CARD "${CMAKE_CURRENT_SOURCE_DIR}/assets/aggro-royale-wordmark.png")
set(AR_TITLE_MARK "${CMAKE_CURRENT_SOURCE_DIR}/assets/asukaneo-mark.png")
configure_file(src/aggro_royale.rc.in ${CMAKE_CURRENT_BINARY_DIR}/aggro_royale.rc @ONLY)
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/aggro_royale.rc)

# ── The setup ───────────────────────────────────────────────────

# aggro_setup.exe sets the game up from the player's copies, plus the BMX 2
# and BMX XXX levels and music if they have those. Plain C, no toolkit: it
# only reads and writes files.
add_executable(aggro_setup WIN32
    src/setup/setup.c
    src/setup/setup_src.c
    src/setup/zaxis.c
    src/setup/level_menu.c
    src/game_check.c
    src/game_import.c
    src/xiso.c
)
target_link_libraries(aggro_setup PRIVATE bcrypt)
target_compile_definitions(aggro_setup PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    # No fused multiply-adds: the spawn heights must come out exactly as
    # tools/zaxis_port.py computes them.
    target_compile_options(aggro_setup PRIVATE /fp:precise /W3)
endif()
