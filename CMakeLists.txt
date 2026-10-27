cmake_minimum_required(VERSION 3.20)

# Aggressive Inline (Xbox, 2002), statically recompiled with xboxrecomp.
# The toolkit sits next to this folder (../xboxrecomp); the game code is
# lifted into src/recomp/gen/ by lift.sh and never committed.

project(aggro_royale C)
# add_compile_definitions(RECOMP_ABI_CHECK)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common definitions
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── xboxrecomp toolkit ──────────────────────────────────────────
#
# Option A: xboxrecomp as a subdirectory (simplest)
#   Place xboxrecomp/ next to your game project and use a relative path.
#
# Option B: find_package (if you install xboxrecomp system-wide)
#   find_package(xboxrecomp REQUIRED)
#
# Adjust this path to point to your xboxrecomp clone:
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# Add xboxrecomp as a subdirectory -- this builds all 6 runtime libraries
# and provides the 'xboxrecomp' umbrella target to link against.
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# ── Recompiled game executable ──────────────────────────────────

# Collect generated recompiled code.
# The recomp pipeline outputs files into src/recomp/gen/.
# With --split 1000, you get gen_0000.c, gen_0001.c, etc.
# CONFIGURE_DEPENDS so re-running the recompiler picks up added/removed files
# without a manual re-configure. A plain GLOB is evaluated once at configure
# time, so a newly generated file (an extra chunk, recomp_stubs_unresolved.c)
# is silently left out of the build and shows up as unresolved externals.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "src/recomp/gen/*.c")

# Core game sources
set(GAME_SOURCES
    src/main.c
    src/game_check.c
    # First-run import of the player's game, and the disc-image reader it uses.
    src/game_import.c
    src/xiso.c
    src/recomp_manual.c
    # Add game-specific sources here, e.g.:
    # src/asset_loader.c
    # src/custom_renderer.c
)

# The recomp pipeline also generates a dispatch header and function declarations.
# These are included by the generated .c files:
#   src/recomp/gen/recomp_funcs.h    - extern declarations for all recompiled functions
#   src/recomp/recomp_dispatch.h     - binary search dispatch table
#   src/recomp/recomp_types.h        - register model, ICALL macros, MEM helpers

add_executable(${PROJECT_NAME} WIN32
    ${GAME_SOURCES}
    ${RECOMP_GEN_SOURCES}
)

# Link against the xboxrecomp umbrella target (all 6 runtime libraries)
target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

# Include paths for generated recomp headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp
    ${XBOXRECOMP_DIR}/include
)

# Windows SDK libraries needed at link time
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    dxgi
    dxguid
    xinput
    winmm
    # SHA-1 of default.xbe for the game check (src/game_check.c).
    bcrypt
    # Lets the fault handler resolve the host RIP back to the generated
    # function's symbol, i.e. to the guest function that faulted.
    dbghelp
)

# MSVC-specific: large address aware, increase section limit for big generated files
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        /LARGEADDRESSAWARE
    )
    # The generated code files can be very large. Increase object file section limit.
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj /MP4)
endif()

# ── Game-specific additions ─────────────────────────────────────
#
# Add any game-specific libraries, loaders, or custom renderers below.
# Examples:
#
# # Custom texture loader for your game's format
# target_sources(${PROJECT_NAME} PRIVATE src/texture_loader.c)
#
# # If your game uses RenderWare, you may need a RW bridge layer
# target_sources(${PROJECT_NAME} PRIVATE src/rw_bridge.c)
#
# # Additional compile definitions for debugging
# target_compile_definitions(${PROJECT_NAME} PRIVATE
#     ENABLE_ICALL_TRACE=1
#     VERBOSE_KERNEL_LOG=1
# )

# sampling profiler (off unless RECOMP_PROF=1)
target_sources(${PROJECT_NAME} PRIVATE src/prof_sampler.c)
target_link_options(${PROJECT_NAME} PRIVATE /MAP)

# The window's title prefix and its settings file (aggroroyale.ini), see
# xboxrecomp/src/video/fb_present.c.
target_compile_definitions(xbox_video PRIVATE RECOMP_APP_NAME="AggroRoyale")

# Last place main.c looks for the game files (after RECOMP_GAME_DIR and the
# assets folders): the toolkit's game_files, where regen.sh reads the XBE.
# AR_RELEASE: a downloadable build (tools/make_release.py). It looks for the
# game only where a player has it -- next to the exe, or a folder they
# picked -- and carries no path from the machine that built it.
option(AR_RELEASE "Build for a downloadable release" OFF)
if(AR_RELEASE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE AR_RELEASE=1)
else()
    file(TO_NATIVE_PATH "${XBOXRECOMP_DIR}/game_files" AR_FALLBACK_GAME_DIR_NATIVE)
    string(REPLACE "\\" "\\\\" AR_FALLBACK_GAME_DIR_C "${AR_FALLBACK_GAME_DIR_NATIVE}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        AR_FALLBACK_GAME_DIR="${AR_FALLBACK_GAME_DIR_C}")
endif()

# The overlay's logo and the launcher's title card, embedded in the exe
# (RECOMP_OVERLAY_LOGO, RECOMP_TITLE_CARD, RECOMP_TITLE_MARK). The .rc is
# configured with absolute paths so rc.exe finds the PNGs from any folder.
set(AR_OVERLAY_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/assets/aggro-royale-logo.png")
set(AR_TITLE_CARD "${CMAKE_CURRENT_SOURCE_DIR}/assets/aggro-royale-wordmark.png")
set(AR_TITLE_MARK "${CMAKE_CURRENT_SOURCE_DIR}/assets/asukaneo-mark.png")
configure_file(src/aggro_royale.rc.in ${CMAKE_CURRENT_BINARY_DIR}/aggro_royale.rc @ONLY)
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/aggro_royale.rc)

# aggro_setup.exe: builds the version of the game the player wants from the
# games they own -- Aggressive Inline, plus Dave Mirra Freestyle BMX 2 and
# BMX XXX levels and music if they have them (src/setup/setup.c). Plain C,
# no toolkit: it only reads and writes files.
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
    # The spawn heights match tools/zaxis_port.py only without contracted
    # (fused) multiply-adds.
    target_compile_options(aggro_setup PRIVATE /fp:precise /W3)
endif()

# The version the launcher shows.
set(AR_VERSION "dev" CACHE STRING "Version shown on the launcher")
target_compile_definitions(${PROJECT_NAME} PRIVATE AR_VERSION="${AR_VERSION}")
