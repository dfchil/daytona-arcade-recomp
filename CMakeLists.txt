cmake_minimum_required(VERSION 3.20)
project(daytona_recomp LANGUAGES C CXX)
if(IOS)
    enable_language(OBJCXX)
endif()

# Mobile frontends use the same runtime/app code as desktop. Android needs
# SDL's shared JNI library; iOS uses the static library in an app bundle.
set(M2_MOBILE OFF)
if(ANDROID OR IOS)
    set(M2_MOBILE ON)
endif()
set(M2_GEN_ROOT "${CMAKE_BINARY_DIR}/gen" CACHE PATH
    "Directory containing recompiled daytona93/daytona93_tgp/daytona93_snd sources")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /bigobj: the generated game code has many labels per object file
    add_compile_options(/W4 /fp:strict /bigobj /utf-8)
else()
    # No -ffast-math, ever (rules.md): exact rounding is the point. No FP
    # contraction either: a fused multiply-add (ARM64 always has one) rounds
    # once where the TGP's code rounds twice.
    add_compile_options(-Wall -Wextra -fno-fast-math -ffp-contract=off)
endif()

# Berkeley SoftFloat 3e (scripts/fetch_softfloat.sh): the extF80 reference
# model for i960 FP. 8086-SSE specialisation, FAST_INT64 build.
set(SOFTFLOAT_DIR ${CMAKE_SOURCE_DIR}/extern/softfloat)
include(cmake/softfloat_sources.cmake)
add_library(softfloat STATIC ${SOFTFLOAT_SOURCES})
# SoftFloat's GCC platform header needs __int128, __builtin_clz and GNU inline
# rules, which MSVC rejects (and clang-cl's are not GCC's). Those compilers get
# our portable one (cmake/softfloat-portable): same results, plain functions.
# M2_SOFTFLOAT_PORTABLE forces it anywhere, to test that path off Windows.
option(M2_SOFTFLOAT_PORTABLE "Build SoftFloat without GCC extensions" OFF)
if(MSVC OR M2_SOFTFLOAT_PORTABLE)
    set(SOFTFLOAT_PLATFORM ${CMAKE_SOURCE_DIR}/cmake/softfloat-portable)
    set(SOFTFLOAT_INLINE)
else()
    set(SOFTFLOAT_PLATFORM ${SOFTFLOAT_DIR}/build/Linux-x86_64-GCC)
    set(SOFTFLOAT_INLINE INLINE_LEVEL=5)
endif()
target_include_directories(softfloat
    PUBLIC ${SOFTFLOAT_DIR}/source/include
    PRIVATE ${SOFTFLOAT_PLATFORM} ${SOFTFLOAT_DIR}/source/8086-SSE)
target_compile_definitions(softfloat PRIVATE
    SOFTFLOAT_ROUND_ODD ${SOFTFLOAT_INLINE} SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32)
# FAST_INT64 selects the by-value extF80 API and LITTLEENDIAN the field order of
# extFloat80_t in the public headers, so users must see both (a mismatch silently
# swaps signExp and signif across the call boundary). Little-endian hosts only.
# Rounding mode and flags are globals in SoftFloat: make them per thread.
# C++ users get __thread, not thread_local: on macOS an extern thread_local is
# reached through a C++ wrapper function the C definitions never emit. MSVC and
# clang-cl (Windows TLS has no such wrapper) use __declspec(thread) in C, which
# both accept where MSVC's C may not take _Thread_local, and thread_local in C++.
if(MSVC)
    set(SOFTFLOAT_TLS_C "__declspec(thread)")
    set(SOFTFLOAT_TLS_CXX thread_local)
else()
    set(SOFTFLOAT_TLS_C _Thread_local)
    set(SOFTFLOAT_TLS_CXX __thread)
endif()
target_compile_definitions(softfloat PUBLIC SOFTFLOAT_FAST_INT64 LITTLEENDIAN=1
    $<$<COMPILE_LANGUAGE:C>:THREAD_LOCAL=${SOFTFLOAT_TLS_C}>
    $<$<COMPILE_LANGUAGE:CXX>:THREAD_LOCAL=${SOFTFLOAT_TLS_CXX}>)
set_target_properties(softfloat PROPERTIES C_STANDARD 11)
if(NOT MSVC)
    target_compile_options(softfloat PRIVATE -w) # upstream code, not ours
endif()

add_library(i960 STATIC
    src/i960/isa.cpp
    src/i960/decode.cpp
    src/i960/reach.cpp
    src/i960/fp.cpp
)
target_include_directories(i960 PUBLIC src)
target_link_libraries(i960 PUBLIC softfloat)

add_library(trace STATIC src/trace/trace.cpp)
target_include_directories(trace PUBLIC src)

# Runtime the recompiled game runs on: i960 context and services (call/ret,
# interrupts, memory; MAME's semantics, transplanted, BSD-3), replay bus and
# lockstep. No instruction interpreter.
# The ROM set the game is recompiled from: daytona93 (Daytona USA Deluxe '93,
# the default) or daytona (Revision A, 1994). One set per build directory:
# its generated code, seeds and saves are its own.
set(M2_ROMSET daytona93 CACHE STRING "MAME ROM set to recompile: daytona93 or daytona")
set_property(CACHE M2_ROMSET PROPERTY STRINGS daytona93 daytona)
if(NOT M2_ROMSET MATCHES "^(daytona93|daytona)$")
    message(FATAL_ERROR "M2_ROMSET must be daytona93 or daytona, not ${M2_ROMSET}")
endif()

add_library(runtime STATIC
    src/runtime/cpu.cpp
    src/runtime/tgp.cpp
    src/runtime/m2_tgp_board.cpp
    src/runtime/geo.cpp
    src/runtime/raster.cpp
    src/runtime/video.cpp
    src/runtime/m2_board.cpp
    src/runtime/game_loop.cpp
    src/runtime/zip.cpp
    src/runtime/rom_import.cpp
    src/runtime/archive.cpp
    src/runtime/m2_replay_bus.cpp
    src/runtime/lockstep.cpp
    src/runtime/snd_cpu.cpp
    src/runtime/multipcm.cpp
    src/runtime/sound_board.cpp
    src/runtime/native_sample_mixer.cpp
    src/runtime/native_sound_sequencer.cpp
    src/runtime/native_sound_engine.cpp
    src/runtime/enhance.cpp
    src/runtime/comm_board.cpp
)
target_include_directories(runtime PUBLIC src)
target_compile_definitions(runtime PUBLIC M2_ROMSET="${M2_ROMSET}")
# 7z ROM sets: the 7-Zip LZMA SDK's decoder (public-domain files only),
# fetched by scripts/setup.py into extern/lzma.
set(LZMA_DIR ${CMAKE_SOURCE_DIR}/extern/lzma/C)
if(EXISTS ${LZMA_DIR}/7zArcIn.c)
    add_library(lzma7z STATIC
        ${LZMA_DIR}/7zArcIn.c ${LZMA_DIR}/7zDec.c ${LZMA_DIR}/7zBuf.c ${LZMA_DIR}/7zCrc.c ${LZMA_DIR}/7zCrcOpt.c
        ${LZMA_DIR}/7zStream.c ${LZMA_DIR}/7zAlloc.c ${LZMA_DIR}/Alloc.c ${LZMA_DIR}/LzmaDec.c ${LZMA_DIR}/Lzma2Dec.c
        ${LZMA_DIR}/Bra.c ${LZMA_DIR}/Bra86.c ${LZMA_DIR}/BraIA64.c ${LZMA_DIR}/Bcj2.c ${LZMA_DIR}/Delta.c
        ${LZMA_DIR}/CpuArch.c ${LZMA_DIR}/Ppmd7.c ${LZMA_DIR}/Ppmd7Dec.c)
    target_include_directories(lzma7z SYSTEM PUBLIC ${LZMA_DIR})
    target_compile_definitions(lzma7z PRIVATE Z7_PPMD_SUPPORT) # 7z's PPMd method too, not only LZMA/LZMA2
    if(NOT MSVC)
        target_compile_options(lzma7z PRIVATE -w)
    endif()
    target_link_libraries(runtime PUBLIC lzma7z)
    target_compile_definitions(runtime PRIVATE M2_HAVE_7Z)
else()
    message(STATUS "extern/lzma not found: 7z ROM sets unsupported (run scripts/setup.py)")
endif()
target_link_libraries(runtime PUBLIC trace i960)

# YM3438 (the sound board's FM chip): ymfm, BSD-3, fetched by
# scripts/setup.py into extern/ymfm.
set(YMFM_DIR ${CMAKE_SOURCE_DIR}/extern/ymfm/src)
if(NOT EXISTS ${YMFM_DIR}/ymfm_opn.cpp)
    message(FATAL_ERROR "extern/ymfm not found: run scripts/setup.py")
endif()
add_library(ymfm STATIC ${YMFM_DIR}/ymfm_opn.cpp ${YMFM_DIR}/ymfm_adpcm.cpp ${YMFM_DIR}/ymfm_ssg.cpp)
target_include_directories(ymfm SYSTEM PUBLIC ${YMFM_DIR})
if(NOT MSVC)
    target_compile_options(ymfm PRIVATE -w)
endif()
target_link_libraries(runtime PUBLIC ymfm)

# Reference interpreter (MAME's i960 executor, transplanted, BSD-3). Test
# oracle only: m2replay links it; the game never does.
add_library(refcore STATIC src/refcore/i960_ref.cpp)
target_link_libraries(refcore PUBLIC runtime)
if(NOT MSVC)
    # Upstream MAME code: keep its own warnings out of ours.
    set_source_files_properties(src/runtime/cpu.cpp src/refcore/i960_ref.cpp src/runtime/geo.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare;-Wno-unused-but-set-variable;-Wno-attributes")
endif()

add_executable(m2replay tools/m2replay/main.cpp)
target_link_libraries(m2replay PRIVATE refcore)

# The recompiler (ours; committed) and, when it has been run, its output.
add_executable(m2recomp tools/m2recomp/main.cpp)
target_link_libraries(m2recomp PRIVATE i960)

# The sound CPU (68000) decoder and static recompiler.
add_library(m68k STATIC src/m68k/decode.cpp src/m68k/reach.cpp)
target_include_directories(m68k PUBLIC src)
add_executable(m2sndrecomp tools/m2sndrecomp/main.cpp)
target_link_libraries(m2sndrecomp PRIVATE m68k)

# The windowed app: SDL3 (window, input, SDL_GPU: Vulkan, Direct3D 12, Metal),
# fetched by scripts/setup.py into extern/sdl3, Dear ImGui for the launcher,
# and the app's own code. Built whenever SDL is there, ROM set or not, so CI
# compiles all of it; the daytona executable itself also needs the generated
# game code (below).
if(EXISTS ${CMAKE_SOURCE_DIR}/extern/sdl3/CMakeLists.txt)
    if(ANDROID)
        # SDLActivity loads libSDL3.so before the game's libmain.so.
        set(M2_SDL_TARGET SDL3::SDL3-shared)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(M2_SDL_TARGET SDL3::SDL3-static)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    if(NOT TARGET ${M2_SDL_TARGET})
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        # SDL builds with its own flags, not ours: MSVC's /fp:strict (our FP
        # rule, for our code) rejects SDL's float constant tables (C2099).
        get_directory_property(M2_COMPILE_OPTIONS COMPILE_OPTIONS)
        set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
        add_subdirectory(${CMAKE_SOURCE_DIR}/extern/sdl3 ${CMAKE_BINARY_DIR}/sdl3 EXCLUDE_FROM_ALL SYSTEM)
        set_directory_properties(PROPERTIES COMPILE_OPTIONS "${M2_COMPILE_OPTIONS}")
    endif()

    # Dear ImGui (launcher UI), with its SDL3 and SDL_GPU backends
    if(NOT ANDROID AND NOT IOS)
        add_executable(test_gpu_startup tests/test_gpu_startup.cpp)
        target_include_directories(test_gpu_startup PRIVATE src ${CMAKE_SOURCE_DIR}/extern/sdl3/include)
        add_test(NAME gpu_startup COMMAND test_gpu_startup)
    endif()
    set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp)
    target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR})
    target_link_libraries(imgui PUBLIC ${M2_SDL_TARGET})
    if(NOT MSVC)
        target_compile_options(imgui PRIVATE -w)
    endif()
    # The hardware renderer (SDL_GPU), shared by the app and m2gpushot.
    add_library(gpurender STATIC src/app/gpu/gpu_renderer.cpp)
    target_include_directories(gpurender PUBLIC src)
    target_link_libraries(gpurender PUBLIC runtime ${M2_SDL_TARGET})
    add_library(daytona_app OBJECT src/app/main.cpp src/app/launcher.cpp src/app/config.cpp src/app/controls.cpp src/app/ffb.cpp src/app/link_socket.cpp src/app/pacing.cpp)
    target_link_libraries(daytona_app PRIVATE runtime imgui gpurender ${M2_SDL_TARGET})
    if(WIN32)
        target_link_libraries(daytona_app PUBLIC ws2_32) # link play's sockets
    endif()
    if(IOS)
        target_sources(daytona_app PRIVATE platform/mobile/ios/rom_picker.mm)
        set_source_files_properties(platform/mobile/ios/rom_picker.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_include_directories(daytona_app PRIVATE platform/mobile/ios)
    endif()
    if(M2_MOBILE)
        target_compile_definitions(daytona_app PRIVATE M2_MOBILE=1)
    endif()
endif()

# Recompiled game code lives in build/gen/<set>/ (derived from the game: never
# committed). scripts/recompile.sh generates it and re-runs CMake.
set(GEN_DIR ${M2_GEN_ROOT}/${M2_ROMSET})
set(TGP_GEN ${M2_GEN_ROOT}/${M2_ROMSET}_tgp/tgp_gen.cpp)
set(SND_GEN ${M2_GEN_ROOT}/${M2_ROMSET}_snd/snd_gen.cpp)
if(EXISTS ${GEN_DIR}/gen_table.cpp AND EXISTS ${TGP_GEN})
    file(GLOB GEN_SOURCES ${GEN_DIR}/*.cpp)
    # Silent analysis/builds need neither sound ROMs nor generated 68000 code.
    # Only GameLoop differs; the board, TGP and video use the shared runtime.
    add_library(runtime_silent STATIC src/runtime/game_loop.cpp)
    target_compile_definitions(runtime_silent PUBLIC M2_NO_SOUND)
    target_link_libraries(runtime_silent PUBLIC runtime)
    add_executable(m2segment tools/m2segment/main.cpp dreamcast-native/code/drive.c dreamcast-native/code/contact.c dreamcast-native/code/path.c dreamcast-native/code/road.c dreamcast-native/code/expert_actors.c ${GEN_SOURCES} ${TGP_GEN})
    target_link_libraries(m2segment PRIVATE runtime_silent refcore)
    if(NOT MSVC)
        set_source_files_properties(${GEN_SOURCES} ${TGP_GEN} PROPERTIES COMPILE_OPTIONS "-Wno-unused-label")
    endif()
endif()
if(EXISTS ${GEN_DIR}/gen_table.cpp AND EXISTS ${TGP_GEN} AND EXISTS ${SND_GEN})
    file(GLOB GEN_SOURCES ${GEN_DIR}/*.cpp)
    add_executable(m2native tools/m2native/main.cpp ${GEN_SOURCES} ${TGP_GEN} ${SND_GEN})
    target_link_libraries(m2native PRIVATE runtime)
    # The game on its own: native board runtime, no trace.
    add_executable(m2run tools/m2run/main.cpp src/app/link_socket.cpp ${GEN_SOURCES} ${TGP_GEN} ${SND_GEN})
    target_link_libraries(m2run PRIVATE runtime)
    if(WIN32)
        target_link_libraries(m2run PRIVATE ws2_32) # link play's sockets
    endif()

    # The game in a window (needs the app objects below).
    if(TARGET daytona_app)
        if(ANDROID)
            # SDLActivity loads this as libmain.so and calls SDL_main from it.
            add_library(daytona SHARED $<TARGET_OBJECTS:daytona_app> ${GEN_SOURCES} ${TGP_GEN} ${SND_GEN})
            set_target_properties(daytona PROPERTIES OUTPUT_NAME main)
        elseif(IOS)
            add_executable(daytona MACOSX_BUNDLE $<TARGET_OBJECTS:daytona_app> ${GEN_SOURCES} ${TGP_GEN} ${SND_GEN})
            set_target_properties(daytona PROPERTIES
                MACOSX_BUNDLE_BUNDLE_NAME "Daytona USA"
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/mobile/ios/Info.plist.in"
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.boucydesigns.daytona"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
                MACOSX_BUNDLE_BUNDLE_VERSION "1"
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.boucydesigns.daytona"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
        else()
            add_executable(daytona WIN32 $<TARGET_OBJECTS:daytona_app> ${GEN_SOURCES} ${TGP_GEN} ${SND_GEN})
        endif()
        target_link_libraries(daytona PRIVATE runtime imgui gpurender ${M2_SDL_TARGET})
        if(WIN32)
            target_link_libraries(daytona PRIVATE ws2_32) # link play's sockets
        endif()
        # Headless frames through the hardware renderer, to compare with m2run's.
        add_executable(m2gpushot tools/m2gpushot/main.cpp ${GEN_SOURCES} ${TGP_GEN} ${SND_GEN})
        target_link_libraries(m2gpushot PRIVATE runtime gpurender)
    else()
        message(STATUS "extern/sdl3 not found: skipping the windowed game (run scripts/setup.py)")
    endif()
    if(NOT MSVC)
        set_source_files_properties(${GEN_SOURCES} ${TGP_GEN} ${SND_GEN} PROPERTIES COMPILE_OPTIONS "-Wno-unused-label")
    endif()
endif()

# The TGP recompiler and, when it has been run, the lockstep check of its
# output against MAME's TGP log.
# The ROM set importer (zip or 7z) for the build.
add_executable(m2import tools/m2import/main.cpp)
target_link_libraries(m2import PRIVATE runtime)

add_executable(m2tgprecomp tools/m2tgprecomp/main.cpp)
target_include_directories(m2tgprecomp PRIVATE src)
if(EXISTS ${TGP_GEN})
    add_executable(m2tgpcheck tools/m2tgpcheck/main.cpp ${TGP_GEN})
    target_link_libraries(m2tgpcheck PRIVATE runtime)
    # The per-instruction hook exists only in this test build.
    target_compile_definitions(m2tgpcheck PRIVATE M2TGP_WITH_HOOK)
    if(NOT MSVC)
        set_source_files_properties(${TGP_GEN} PROPERTIES COMPILE_OPTIONS "-Wno-unused-label")
    endif()
endif()

# The lockstep check of the recompiled sound 68000 against MAME's log
# (M2TRACE_SNDLOG, patches/mame/0003).
if(EXISTS ${SND_GEN})
    add_executable(m2sndcheck tools/m2sndcheck/main.cpp ${SND_GEN})
    target_link_libraries(m2sndcheck PRIVATE runtime)
    if(NOT MSVC)
        set_source_files_properties(${SND_GEN} PROPERTIES COMPILE_OPTIONS "-Wno-unused-label")
    endif()
endif()

add_executable(i960dis tools/i960dis/main.cpp)
target_link_libraries(i960dis PRIVATE i960)

add_executable(tracediff tools/tracediff/main.cpp)
target_link_libraries(tracediff PRIVATE trace)

add_executable(tracedev tools/tracedev/main.cpp)
target_link_libraries(tracedev PRIVATE trace)

add_executable(traceinfo tools/traceinfo/main.cpp)
target_link_libraries(traceinfo PRIVATE trace)

enable_testing()

add_executable(test_scenery tests/test_scenery.cpp src/runtime/enhance.cpp)
target_include_directories(test_scenery PRIVATE src)
add_test(NAME scenery COMMAND test_scenery)

add_executable(test_runtime_memory tests/test_runtime_memory.cpp)
target_link_libraries(test_runtime_memory PRIVATE runtime)
add_test(NAME runtime_memory COMMAND test_runtime_memory)

add_executable(test_paged_rom tests/test_paged_rom.cpp)
target_include_directories(test_paged_rom PRIVATE src)
add_test(NAME paged_rom COMMAND test_paged_rom ${CMAKE_CURRENT_BINARY_DIR})

# Geometrizer clipping and display-list streaming (shared runtime code).
add_executable(test_geo_clip tests/test_geo_clip.cpp)
target_include_directories(test_geo_clip PRIVATE src)
add_test(NAME geo_clip COMMAND test_geo_clip)

add_executable(test_geo_stream tests/test_geo_stream.cpp)
target_include_directories(test_geo_stream PRIVATE src)
add_test(NAME geo_stream COMMAND test_geo_stream)
add_executable(test_native_models dreamcast-native/tests/test_export_models.cpp)
target_include_directories(test_native_models PRIVATE src)
add_test(NAME native_models COMMAND test_native_models)
add_executable(test_native_visibility dreamcast-native/tests/test_visibility.c dreamcast-native/code/visibility.c)
set_property(TARGET test_native_visibility PROPERTY C_STANDARD 11)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_native_visibility PRIVATE m)
endif()
add_test(NAME native_visibility COMMAND test_native_visibility)
add_executable(test_native_road dreamcast-native/tests/test_road.c dreamcast-native/code/road.c)
add_test(NAME native_road COMMAND test_native_road)
add_executable(test_native_drive dreamcast-native/tests/test_drive.c dreamcast-native/code/drive.c)
set_property(TARGET test_native_drive PROPERTY C_STANDARD 11)
add_test(NAME native_drive COMMAND test_native_drive)
add_executable(test_native_contact dreamcast-native/tests/test_contact.c dreamcast-native/code/contact.c dreamcast-native/code/drive.c)
set_property(TARGET test_native_contact PROPERTY C_STANDARD 11)
add_test(NAME native_contact COMMAND test_native_contact)
set(NATIVE_RACE_TEST_ASSETS "" CACHE PATH "Owned prepared course0 packs for the autonomous native race check")
set(NATIVE_RACE_TEST_HEADERS "" CACHE PATH "Owned generated intro_drive_data.h directory for the native race check")
set(NATIVE_RACE_TEST_INTENTS "" CACHE PATH "Owned generated race_intent_data.h directory for silent native race events")
if(NATIVE_RACE_TEST_ASSETS AND NATIVE_RACE_TEST_HEADERS)
    add_executable(test_native_race_movement dreamcast-native/tests/test_race_movement.c
        dreamcast-native/code/drive.c dreamcast-native/code/contact.c
        dreamcast-native/code/road.c dreamcast-native/code/path.c dreamcast-native/code/visibility.c)
    set_property(TARGET test_native_race_movement PROPERTY C_STANDARD 11)
    target_include_directories(test_native_race_movement PRIVATE dreamcast-native/include ${NATIVE_RACE_TEST_HEADERS})
    if(NATIVE_RACE_TEST_INTENTS)
        target_include_directories(test_native_race_movement PRIVATE ${NATIVE_RACE_TEST_INTENTS})
        target_compile_definitions(test_native_race_movement PRIVATE NATIVE_INTENT_DATA)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(test_native_race_movement PRIVATE m)
    endif()
    add_test(NAME native_race_movement COMMAND test_native_race_movement
        ${NATIVE_RACE_TEST_ASSETS}/drive.dnd ${NATIVE_RACE_TEST_ASSETS}/course0.dnc
        ${NATIVE_RACE_TEST_ASSETS}/course0.dnr ${NATIVE_RACE_TEST_ASSETS})
endif()
add_executable(test_native_practice dreamcast-native/tests/test_practice.c
    dreamcast-native/code/drive.c dreamcast-native/code/contact.c
    dreamcast-native/code/road.c dreamcast-native/code/path.c
    dreamcast-native/code/geometry.c dreamcast-native/code/meshpack.c
    dreamcast-native/code/material_pack.c dreamcast-native/code/visibility.c)
set_property(TARGET test_native_practice PROPERTY C_STANDARD 11)
# Requires user-owned prepared packs, passed explicitly; no ROM test fixture.
if(UNIX AND NOT APPLE)
    target_link_libraries(test_native_practice PRIVATE m)
endif()
add_executable(test_native_path dreamcast-native/tests/test_path.c dreamcast-native/code/path.c dreamcast-native/code/drive.c)
set_property(TARGET test_native_path PROPERTY C_STANDARD 11)
add_test(NAME native_path COMMAND test_native_path)
add_executable(test_native_slot_machine dreamcast-native/tests/test_slot_machine.c)
set_property(TARGET test_native_slot_machine PROPERTY C_STANDARD 11)
add_test(NAME native_slot_machine COMMAND test_native_slot_machine)
add_executable(test_native_wrong_way dreamcast-native/tests/test_wrong_way.c)
set_property(TARGET test_native_wrong_way PROPERTY C_STANDARD 11)
add_test(NAME native_wrong_way COMMAND test_native_wrong_way)
add_executable(test_native_contact_clear dreamcast-native/tests/test_contact_clear.c)
set_property(TARGET test_native_contact_clear PROPERTY C_STANDARD 11)
add_test(NAME native_contact_clear COMMAND test_native_contact_clear)
add_executable(check_native_meshpack dreamcast-native/tests/check_meshpack.c dreamcast-native/code/meshpack.c)
set_property(TARGET check_native_meshpack PROPERTY C_STANDARD 11)
add_executable(check_native_material_pack dreamcast-native/tests/check_material_pack.c dreamcast-native/code/material_pack.c)
set_property(TARGET check_native_material_pack PROPERTY C_STANDARD 11)
find_package(ZLIB REQUIRED)
target_link_libraries(check_native_material_pack PRIVATE ZLIB::ZLIB)
target_link_libraries(test_native_practice PRIVATE ZLIB::ZLIB)
add_executable(test_native_geometry dreamcast-native/tests/test_geometry.cpp
    dreamcast-native/code/geometry.c dreamcast-native/code/meshpack.c)
set_property(TARGET test_native_geometry PROPERTY C_STANDARD 11)
target_include_directories(test_native_geometry PRIVATE src)
target_compile_options(test_native_geometry PRIVATE -ffp-contract=off)
add_test(NAME native_geometry COMMAND test_native_geometry)
# The tight HUD geometry (dreamcast-native/include/render/ui/hud_draw.h) against the whole quads it
# replaced: every recorded HUD state rasterised both ways, compared pixel for pixel
# (dreamcast-native/tests/check_hud_tight.c). NATIVE_HUD_TEST_PACK: tools/prepare_hud.py output
# (hud_data.h, hud/); NATIVE_HUD_TEST_ROWS: files of `trace_race_native --hud` rows, ";" between them.
set(NATIVE_HUD_TEST_PACK "" CACHE PATH "Prepared HUD pack (hud_data.h, hud/) for the tight HUD geometry check")
set(NATIVE_HUD_TEST_ROWS "" CACHE STRING "HUD row files of recorded races (trace_race_native --hud) for the tight HUD geometry check")
if(NATIVE_HUD_TEST_PACK AND NATIVE_HUD_TEST_ROWS)
    add_executable(check_native_hud_tight dreamcast-native/tests/check_hud_tight.c)
    set_property(TARGET check_native_hud_tight PROPERTY C_STANDARD 11)
    target_include_directories(check_native_hud_tight PRIVATE dreamcast-native/include ${NATIVE_HUD_TEST_PACK})
    target_compile_options(check_native_hud_tight PRIVATE -fno-fast-math -ffp-contract=off)
    if(UNIX AND NOT APPLE)
        target_link_libraries(check_native_hud_tight PRIVATE m)
    endif()
    add_test(NAME native_hud_tight COMMAND check_native_hud_tight ${NATIVE_HUD_TEST_PACK} ${NATIVE_HUD_TEST_ROWS})
endif()

# Native implementation and headers have separate roots on host and SH4.
foreach(native_target IN ITEMS m2segment test_native_visibility test_native_road
        test_native_drive test_native_contact test_native_race_movement
        test_native_practice test_native_path check_native_meshpack
        check_native_material_pack test_native_geometry test_native_slot_machine
        test_native_wrong_way test_native_contact_clear)
    if(TARGET ${native_target})
        target_include_directories(${native_target} PRIVATE dreamcast-native/include)
    endif()
endforeach()

add_executable(test_dreamcast_budget tests/test_dreamcast_budget.cpp)
target_include_directories(test_dreamcast_budget PRIVATE src)
add_test(NAME dreamcast_budget COMMAND test_dreamcast_budget)

add_executable(test_dreamcast_assets tests/test_dreamcast_assets.cpp)
target_include_directories(test_dreamcast_assets PRIVATE src)
target_link_libraries(test_dreamcast_assets PRIVATE runtime)
add_test(NAME dreamcast_assets COMMAND test_dreamcast_assets)

add_executable(test_texture_pack tests/test_texture_pack.c)
add_test(NAME texture_pack COMMAND test_texture_pack)

add_executable(test_material_pack tests/test_material_pack.cpp)
target_include_directories(test_material_pack PRIVATE src)
add_test(NAME material_pack COMMAND test_material_pack)

add_executable(test_pvr_geometry tests/test_pvr_geometry.cpp)
target_include_directories(test_pvr_geometry PRIVATE src)
add_test(NAME pvr_geometry COMMAND test_pvr_geometry)
add_executable(test_dc_system24 tests/test_dc_system24.cpp)
target_link_libraries(test_dc_system24 PRIVATE runtime)
add_test(NAME dc_system24 COMMAND test_dc_system24)

add_executable(test_decode tests/test_decode.cpp)
target_link_libraries(test_decode PRIVATE i960)
add_test(NAME decode COMMAND test_decode)

add_executable(test_m68k tests/test_m68k.cpp)
target_link_libraries(test_m68k PRIVATE m68k runtime)
add_test(NAME m68k COMMAND test_m68k)

add_executable(test_fp tests/test_fp.cpp)
target_link_libraries(test_fp PRIVATE i960)
find_package(Threads REQUIRED)
target_link_libraries(test_fp PRIVATE Threads::Threads)
add_test(NAME fp COMMAND test_fp)

add_executable(test_native_sample_mixer tests/test_native_sample_mixer.cpp src/runtime/native_sample_mixer.cpp)
target_include_directories(test_native_sample_mixer PRIVATE src/runtime)
add_test(NAME native_sample_mixer COMMAND test_native_sample_mixer)

add_executable(test_multipcm_volumes tests/test_multipcm_volumes.cpp src/runtime/multipcm.cpp)
target_include_directories(test_multipcm_volumes PRIVATE src)
add_test(NAME multipcm_volumes COMMAND test_multipcm_volumes)

add_executable(test_app_pacing tests/test_app_pacing.cpp src/app/pacing.cpp)
target_include_directories(test_app_pacing PRIVATE src)
add_test(NAME app_pacing COMMAND test_app_pacing)

add_executable(test_test_hold tests/test_test_hold.cpp)
target_include_directories(test_test_hold PRIVATE src)
add_test(NAME test_hold COMMAND test_test_hold)

add_executable(test_native_sound_sequencer tests/test_native_sound_sequencer.cpp src/runtime/native_sound_sequencer.cpp)
target_include_directories(test_native_sound_sequencer PRIVATE src)
add_test(NAME native_sound_sequencer COMMAND test_native_sound_sequencer)

# Link play's communication board, two boards wired in memory (no ROM, no network).
add_executable(test_comm_board tests/test_comm_board.cpp src/runtime/comm_board.cpp)
target_include_directories(test_comm_board PRIVATE src)
add_test(NAME comm_board COMMAND test_comm_board)

# Wheel and pedal bindings against SDL virtual joysticks.
if(TARGET SDL3::SDL3-static)
    add_executable(test_app_config tests/test_app_config.cpp src/app/config.cpp src/app/controls.cpp)
    target_include_directories(test_app_config PRIVATE src)
    target_link_libraries(test_app_config PRIVATE runtime SDL3::SDL3-static)
    add_test(NAME app_config COMMAND test_app_config)
    add_executable(test_app_controls tests/test_app_controls.cpp src/app/controls.cpp src/app/ffb.cpp)
    target_include_directories(test_app_controls PRIVATE src)
    target_link_libraries(test_app_controls PRIVATE runtime SDL3::SDL3-static)
    add_test(NAME app_controls COMMAND test_app_controls)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/extern/sdl3/include/SDL3/SDL_audio.h)
    add_executable(test_app_native_audio tests/test_app_native_audio.cpp)
    target_include_directories(test_app_native_audio PRIVATE extern/sdl3/include)
    target_link_libraries(test_app_native_audio PRIVATE Threads::Threads)
    add_test(NAME app_native_audio COMMAND test_app_native_audio)
endif()

# PS Vita frontend tests: platform/vita code against narrow host mocks of the
# Vita SDK. ROM-free; they check CPU data and lifetimes, not GXM rendering or
# device frame rates. Off by default: not part of the desktop build.
option(M2_VITA_TESTS "Build the PS Vita frontend's host tests" OFF)
if(M2_VITA_TESTS)
    add_executable(test_vita_imgui tests/test_vita_imgui.cpp
        extern/imgui/imgui.cpp extern/imgui/imgui_draw.cpp
        extern/imgui/imgui_tables.cpp extern/imgui/imgui_widgets.cpp)
    target_include_directories(test_vita_imgui PRIVATE . tests/vita_gpu_shim extern/imgui)
    add_test(NAME vita_imgui COMMAND test_vita_imgui)
    # The same menu through the vitaGL backend (GPU_GL builds), gpu_gl.h mocked.
    add_executable(test_vita_imgui_gl tests/test_vita_imgui_gl.cpp
        extern/imgui/imgui.cpp extern/imgui/imgui_draw.cpp
        extern/imgui/imgui_tables.cpp extern/imgui/imgui_widgets.cpp)
    target_include_directories(test_vita_imgui_gl PRIVATE . tests/vita_gl_shim tests/vita_gpu_shim src platform/vita extern/imgui)
    add_test(NAME vita_imgui_gl COMMAND test_vita_imgui_gl)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_vita_link tests/test_vita_link.cpp src/app/link_socket.cpp src/runtime/comm_board.cpp)
        target_include_directories(test_vita_link PRIVATE tests/vita_link_shim src)
        add_test(NAME vita_link COMMAND test_vita_link)
    endif()
    add_executable(test_vita_controls tests/test_vita_controls.cpp)
    add_executable(test_vita_core_policy tests/test_vita_core_policy.cpp)
    add_test(NAME vita_core_policy COMMAND test_vita_core_policy)
    add_test(NAME vita_controls COMMAND test_vita_controls)
    add_executable(test_vita_gpu_memory tests/test_vita_gpu_memory.cpp)
    target_include_directories(test_vita_gpu_memory PRIVATE tests/vita_gpu_shim platform/vita)
    add_test(NAME vita_gpu_memory COMMAND test_vita_gpu_memory)

    add_executable(test_vita_gpu_lifetime tests/test_vita_gpu_lifetime.cpp
        src/runtime/video.cpp src/runtime/raster.cpp)
    target_include_directories(test_vita_gpu_lifetime PRIVATE tests/vita_gpu_shim src)
    target_compile_definitions(test_vita_gpu_lifetime PRIVATE M2_VITA_RENDER_OPT=1)
    add_test(NAME vita_gpu_lifetime COMMAND test_vita_gpu_lifetime)

    add_executable(test_vita_tessellation tests/test_vita_tessellation.cpp)
    target_include_directories(test_vita_tessellation PRIVATE tests/vita_gpu_shim platform/vita)
    add_test(NAME vita_tessellation COMMAND test_vita_tessellation)

    add_executable(test_vita_polygon_order tests/test_vita_polygon_order.cpp)
    target_include_directories(test_vita_polygon_order PRIVATE platform/vita src)
    add_test(NAME vita_polygon_order COMMAND test_vita_polygon_order)

    # The multicore/vitaGL helpers: texel index, flat index, reference audio pacing.
    add_executable(test_vita_texel_index tests/test_vita_texel_index.cpp)
    target_include_directories(test_vita_texel_index PRIVATE src)
    add_test(NAME vita_texel_index COMMAND test_vita_texel_index)
    add_executable(test_vita_flat_index tests/test_vita_flat_index.cpp)
    add_test(NAME vita_flat_index COMMAND test_vita_flat_index)
    add_executable(test_vita_audio_rate tests/test_vita_audio_rate.cpp)
    add_test(NAME vita_audio_rate COMMAND test_vita_audio_rate)

    add_executable(test_vita_native_audio tests/test_vita_native_audio.cpp)
    target_include_directories(test_vita_native_audio PRIVATE tests/vita_audio_shim)
    target_link_libraries(test_vita_native_audio PRIVATE Threads::Threads)
    add_test(NAME vita_native_audio COMMAND test_vita_native_audio)

    add_executable(test_vita_sound_worker tests/test_vita_sound_worker.cpp)
    target_include_directories(test_vita_sound_worker PRIVATE tests/vita_audio_shim)
    target_link_libraries(test_vita_sound_worker PRIVATE Threads::Threads)
    add_test(NAME vita_sound_worker COMMAND test_vita_sound_worker)

    add_executable(test_vita_audio_worker tests/test_vita_audio_worker.cpp)
    target_include_directories(test_vita_audio_worker PRIVATE tests/vita_audio_shim)
    target_link_libraries(test_vita_audio_worker PRIVATE Threads::Threads)
    add_test(NAME vita_audio_worker COMMAND test_vita_audio_worker)

    add_executable(test_vita_async_log tests/test_vita_async_log.cpp)
    target_include_directories(test_vita_async_log PRIVATE tests/vita_log_shim)
    target_link_libraries(test_vita_async_log PRIVATE Threads::Threads)
    add_test(NAME vita_async_log COMMAND test_vita_async_log)
    set_tests_properties(vita_async_log PROPERTIES TIMEOUT 20)

    add_executable(test_vita_log tests/test_vita_log.cpp)
    target_include_directories(test_vita_log PRIVATE tests/vita_log_shim)
    add_test(NAME vita_log COMMAND test_vita_log)
endif()

# Measurement, not a test: where MAME's double FP disagrees with the model.
add_executable(fp_vs_mame tests/fp_vs_mame.cpp)
target_link_libraries(fp_vs_mame PRIVATE i960 Threads::Threads)

add_executable(test_reach tests/test_reach.cpp)
target_link_libraries(test_reach PRIVATE i960)
add_test(NAME reach COMMAND test_reach)

add_executable(test_trace tests/test_trace.cpp)
target_link_libraries(test_trace PRIVATE trace)
add_test(NAME trace COMMAND test_trace ${CMAKE_CURRENT_BINARY_DIR})

# The MAME plugin's Lua core against the C++ trace library (needs lupa).
find_package(Python3 COMPONENTS Interpreter)
# Slot machine lockstep (dreamcast-native/tests/test_slot_lockstep.py): the native race harness in production mode
# against the original's runs with START presses. ROM output only, passed explicitly: INCLUDES the generated header
# directories (intro_drive_data.h of work/intro, race_intent_data.h, race_start_data.h, race_record_data.h,
# podium_data.h; ";" between them), ASSETS the course0 packs, TRACES the directories holding reference-slot-*.trace
# and reference-finish-fast.trace (m2segment --trace-race; ";" between them).
set(NATIVE_SLOT_TEST_INCLUDES "" CACHE STRING "Header dirs for tests/trace_race_native.c (';' between them)")
set(NATIVE_SLOT_TEST_ASSETS "" CACHE PATH "drive.dnd, course0.dn*, scenery.dnv, course0-route*.dnp")
set(NATIVE_SLOT_TEST_TRACES "" CACHE STRING "Dirs with reference-slot-*.trace / reference-finish-fast.trace (';')")
set(NATIVE_DAMAGE_TEST_TRACES "" CACHE STRING "Dirs with reference-damage-*.trace (';')")
if(Python3_FOUND AND NATIVE_SLOT_TEST_INCLUDES AND NATIVE_SLOT_TEST_ASSETS)
    add_executable(trace_race_native dreamcast-native/tests/trace_race_native.c dreamcast-native/code/drive.c
        dreamcast-native/code/contact.c dreamcast-native/code/road.c dreamcast-native/code/path.c
        dreamcast-native/code/visibility.c dreamcast-native/code/meshpack.c dreamcast-native/code/render/scene/damage_bodies.c
        dreamcast-native/code/props.c dreamcast-native/code/props_scene.c dreamcast-native/code/expert_actors.c)
    set_property(TARGET trace_race_native PROPERTY C_STANDARD 11)
    target_include_directories(trace_race_native PRIVATE dreamcast-native/include ${NATIVE_SLOT_TEST_INCLUDES})
    target_compile_options(trace_race_native PRIVATE -O2 -fno-fast-math -ffp-contract=off)
    target_compile_definitions(trace_race_native PRIVATE NM_TRACE_PARTS=1) # --parts (meshpack.c, damage_bodies.c)
    if(UNIX AND NOT APPLE)
        target_link_libraries(trace_race_native PRIVATE m)
    endif()
    # Live-play robustness (dreamcast-native/tests/fuzz_race_native.c): seeded random races with abusive pad input
    # (grass, spins, walls, wrong way, START at the slot machine, time out) through the production tick of
    # code/practice.c; the original never rejects a tick, so any reject fails, naming the step and the state.
    add_executable(fuzz_race_native dreamcast-native/tests/fuzz_race_native.c dreamcast-native/code/drive.c
        dreamcast-native/code/contact.c dreamcast-native/code/road.c dreamcast-native/code/path.c
        dreamcast-native/code/visibility.c)
    set_property(TARGET fuzz_race_native PROPERTY C_STANDARD 11)
    target_include_directories(fuzz_race_native PRIVATE dreamcast-native/include ${NATIVE_SLOT_TEST_INCLUDES})
    target_compile_options(fuzz_race_native PRIVATE -O2 -fno-fast-math -ffp-contract=off)
    if(UNIX AND NOT APPLE)
        target_link_libraries(fuzz_race_native PRIVATE m)
    endif()
    # Seeds 1-300 (289: a contact leaves the player backwards, speed index -1, NaN drag; one race in three aims at
    # the pit lane at every lap's end: about 400 pit stops), plus seed 435: a race finished on profile 195 (the
    # finish entry's path cursor reads the route's guard records).
    add_test(NAME native_race_fuzz COMMAND fuzz_race_native ${NATIVE_SLOT_TEST_ASSETS} 1 300 40000)
    add_test(NAME native_race_fuzz_finish COMMAND fuzz_race_native ${NATIVE_SLOT_TEST_ASSETS} 435 1 40000)
    add_test(NAME native_slot_lockstep
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_slot_lockstep.py
                $<TARGET_FILE:trace_race_native> ${NATIVE_SLOT_TEST_ASSETS} "${NATIVE_SLOT_TEST_TRACES}")
    set_tests_properties(native_slot_lockstep PROPERTIES SKIP_RETURN_CODE 77)
    # Damage/wrong-way lockstep (tests/test_damage_lockstep.py): every player field on every row of the original's
    # wall-hit runs (reference-damage-*.trace from recipes/inputs/inputs-damage-*.txt, m2segment --trace-race), and
    # every opponent field (production: but the previous scene's tracking words; carry mode: all).
    add_test(NAME native_damage_lockstep
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_damage_lockstep.py
                $<TARGET_FILE:trace_race_native> ${NATIVE_SLOT_TEST_ASSETS} "${NATIVE_DAMAGE_TEST_TRACES}")
    set_tests_properties(native_damage_lockstep PROPERTIES SKIP_RETURN_CODE 77)
    # Pit stop lockstep (tests/test_pit_lockstep.py, include/pit.h): every player, timeline and opponent field on every
    # row of the original's five pit-lane runs (reference-pit-{P,E,C,F,H}.trace of spec-pitstop.md, m2segment
    # --trace-race), the pit camera's view and the native pit state by the spec's rules (production and carry modes).
    set(NATIVE_PIT_TEST_TRACES "" CACHE STRING "Dirs with reference-pit-*.trace (';')")
    add_test(NAME native_pit_lockstep
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_pit_lockstep.py
                $<TARGET_FILE:trace_race_native> ${NATIVE_SLOT_TEST_ASSETS} "${NATIVE_PIT_TEST_TRACES}")
    set_tests_properties(native_pit_lockstep PROPERTIES SKIP_RETURN_CODE 77)
    # Pit presentation (tests/test_pit_render.py): the native race of run P (--parts, --hud) against the pit log
    # log-P.bin (m2pit display lists and CPU records): the player's parts and matrices (wheels off, the jack's lift),
    # opponents hidden while 501a84 > 1, "PIT WORK!", no world actor while the crew works, and the six pit crew
    # figures (include/pit_crew.h): models, order and matrices of every crew draw. Runs E, C, F, H too where their
    # logs are in the log directories.
    set(NATIVE_PIT_TEST_LOGS "" CACHE STRING "Dirs with the pit log log-P.bin (';')")
    add_test(NAME native_pit_render
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_pit_render.py
                $<TARGET_FILE:trace_race_native> ${NATIVE_SLOT_TEST_ASSETS} "${NATIVE_PIT_TEST_TRACES}" "${NATIVE_PIT_TEST_LOGS}")
    set_tests_properties(native_pit_render PROPERTIES SKIP_RETURN_CODE 77)
    # The same with the crew's SH4ZAM chains (pit_crew.h NM_PIT_CREW_SHZ; SH4ZAM's software back-end from a checkout,
    # NATIVE_SH4ZAM_DIR with include/ and source/): every crew matrix also against the scalar reference.
    set(NATIVE_SH4ZAM_DIR "" CACHE PATH "SH4ZAM checkout (include/, source/) for the host check of the crew's SH4ZAM chains")
    if(NATIVE_SH4ZAM_DIR)
        add_executable(trace_race_native_shz dreamcast-native/tests/trace_race_native.c dreamcast-native/code/drive.c
            dreamcast-native/code/contact.c dreamcast-native/code/road.c dreamcast-native/code/path.c
            dreamcast-native/code/visibility.c dreamcast-native/code/meshpack.c dreamcast-native/code/render/scene/damage_bodies.c
            ${NATIVE_SH4ZAM_DIR}/source/sw/shz_xmtrx_sw.c ${NATIVE_SH4ZAM_DIR}/source/shz_xmtrx.c
            ${NATIVE_SH4ZAM_DIR}/source/shz_matrix.c)
        set_property(TARGET trace_race_native_shz PROPERTY C_STANDARD 11)
        set_property(TARGET trace_race_native_shz PROPERTY C_EXTENSIONS ON)
        target_include_directories(trace_race_native_shz PRIVATE dreamcast-native/include ${NATIVE_SLOT_TEST_INCLUDES}
            ${NATIVE_SH4ZAM_DIR}/include)
        target_compile_options(trace_race_native_shz PRIVATE -O2 -fno-fast-math -ffp-contract=off)
        target_compile_definitions(trace_race_native_shz PRIVATE NM_TRACE_PARTS=1 NM_PIT_CREW_SHZ=1)
        if(UNIX AND NOT APPLE)
            target_link_libraries(trace_race_native_shz PRIVATE m)
        endif()
        add_test(NAME native_pit_render_shz
            COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_pit_render.py
                    $<TARGET_FILE:trace_race_native_shz> ${NATIVE_SLOT_TEST_ASSETS} "${NATIVE_PIT_TEST_TRACES}" "${NATIVE_PIT_TEST_LOGS}")
        set_tests_properties(native_pit_render_shz PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    # Front-end lockstep (tests/test_frontend_lockstep.py, include/select.h, include/frontend.h): the native
    # course/transmission select on every iteration of the original's front-end traces (frontend-X.trace of
    # recipes/inputs/frontend-X.txt, m2segment --trace-frontend), and the races entered through the native attract,
    # game start and select (trace_race_native --frontend) on every row and field, collision tracking words included.
    set(NATIVE_FRONTEND_TEST_TRACES "" CACHE STRING "Dirs with frontend-*.trace and race-*.trace (';')")
    add_executable(trace_frontend_native dreamcast-native/tests/trace_frontend_native.c)
    set_property(TARGET trace_frontend_native PROPERTY C_STANDARD 11)
    target_include_directories(trace_frontend_native PRIVATE dreamcast-native/include)
    add_test(NAME native_frontend_lockstep
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_frontend_lockstep.py
                $<TARGET_FILE:trace_frontend_native> $<TARGET_FILE:trace_race_native> ${NATIVE_SLOT_TEST_ASSETS}
                "${NATIVE_FRONTEND_TEST_TRACES}")
    set_tests_properties(native_frontend_lockstep PROPERTIES SKIP_RETURN_CODE 77)
    # Live-play edge states (tests/test_negative_speed_lockstep.py): a negative speed index (the drag words before
    # and after the tables, NaN included) and the yaw step's cvtri overflow, against the original's runs in
    # NATIVE_NEGSPEED_TEST_TRACES (m2segment --trace-race, the speed written by M2_POKE_ROW/M2_POKE_SPEED).
    set(NATIVE_NEGSPEED_TEST_TRACES "" CACHE STRING "Dirs with reference-negspeed-*.trace, reference-yaw-overflow.trace (';')")
    add_test(NAME native_negative_speed_lockstep
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_negative_speed_lockstep.py
                $<TARGET_FILE:trace_race_native> ${NATIVE_SLOT_TEST_ASSETS} "${NATIVE_NEGSPEED_TEST_TRACES}")
    set_tests_properties(native_negative_speed_lockstep PROPERTIES SKIP_RETURN_CODE 77)
    # The player's drawn parts with damage (tests/test_damage_render.py): the rules of player_parts.h on every
    # race frame of the original's display-list logs (log-E.bin, log-V.bin of spec-damage.md: damage_log v3), and
    # the native race of run E (trace_race_native --parts) against log E, model by model and matrix by matrix.
    set(NATIVE_DAMAGE_TEST_LOGS "" CACHE PATH "Dir with the damage display-list logs log-E.bin, log-V.bin")
    add_executable(player_parts_check dreamcast-native/tests/player_parts_check.c dreamcast-native/code/drive.c
        dreamcast-native/code/road.c)
    set_property(TARGET player_parts_check PROPERTY C_STANDARD 11)
    target_include_directories(player_parts_check PRIVATE dreamcast-native/include)
    target_compile_options(player_parts_check PRIVATE -O2 -fno-fast-math -ffp-contract=off)
    if(UNIX AND NOT APPLE)
        target_link_libraries(player_parts_check PRIVATE m)
    endif()
    add_test(NAME native_damage_render
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_damage_render.py
                $<TARGET_FILE:trace_race_native> $<TARGET_FILE:player_parts_check> ${NATIVE_SLOT_TEST_ASSETS}
                "${NATIVE_DAMAGE_TEST_TRACES}" "${NATIVE_DAMAGE_TEST_LOGS}")
    set_tests_properties(native_damage_render PROPERTIES SKIP_RETURN_CODE 77)
    # The player's car effects (tests/test_effects_render.py, include/car_effects.h): the rules on every 9088 call of
    # the original's effects logs (fx-*.fx of build/dc-native-effects.*/ref/record.sh: draws, order, local matrices),
    # and the native races of the runs with a reference trace against the logs' display lists.
    set(NATIVE_FX_TEST_LOGS "" CACHE STRING "Dirs with the effects logs fx-{A,C,E,F,G,H,V,fast}.fx (';')")
    set(NATIVE_FX_TEST_ROM "" CACHE PATH "Dir with the daytona93 images program.bin, main_data.bin")
    add_executable(car_effects_check dreamcast-native/tests/car_effects_check.c dreamcast-native/code/drive.c)
    set_property(TARGET car_effects_check PROPERTY C_STANDARD 11)
    target_include_directories(car_effects_check PRIVATE dreamcast-native/include)
    target_compile_options(car_effects_check PRIVATE -O2 -fno-fast-math -ffp-contract=off)
    if(UNIX AND NOT APPLE)
        target_link_libraries(car_effects_check PRIVATE m)
    endif()
    add_test(NAME native_effects_render
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_effects_render.py
                $<TARGET_FILE:car_effects_check> $<TARGET_FILE:trace_race_native> ${NATIVE_SLOT_TEST_ASSETS}
                "${NATIVE_DAMAGE_TEST_TRACES};${NATIVE_SLOT_TEST_TRACES}" "${NATIVE_FX_TEST_LOGS}" "${NATIVE_FX_TEST_ROM}")
    set_tests_properties(native_effects_render PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(Python3_FOUND)
    add_test(NAME native_race_intent_assets
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_race_intents.py)
    add_test(NAME native_visibility_pack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_visibility.py $<TARGET_FILE:test_native_visibility>)
    add_test(NAME native_road_pack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_road.py $<TARGET_FILE:test_native_road>)
    add_test(NAME native_path_pack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_path.py $<TARGET_FILE:test_native_path>)
    add_test(NAME native_contact_pack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_contact.py $<TARGET_FILE:test_native_contact>)
    add_test(NAME native_drive_pack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_drive.py $<TARGET_FILE:test_native_drive>)
    add_test(NAME native_course
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tools/inspect_course.py --self-test)
    add_test(NAME native_camera
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tools/check_camera.py --self-test)
    add_test(NAME native_material_decode
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_material_decode.py)
    add_test(NAME native_material_pack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_materials.py
                $<TARGET_FILE:check_native_material_pack>)
    # The model renderer's PVR stream against the host reference renderer
    # (dreamcast-native/tests/reference_stream.h) over a recorded script, stages 3 and 5.
    set(NATIVE_STREAM_TEST_ASSETS "" CACHE PATH "Prepared car/world packs for the native stream reference check")
    set(NATIVE_STREAM_TEST_FRAMES "" CACHE FILEPATH "Frame table (.frm) of a recorded script for the native stream reference check")
    if(NATIVE_STREAM_TEST_ASSETS AND NATIVE_STREAM_TEST_FRAMES)
        add_test(NAME native_stream_reference
            COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/stream_sim/check_stream.py
                    ${NATIVE_STREAM_TEST_ASSETS} ${NATIVE_STREAM_TEST_FRAMES})
    endif()
    # The console heap of a build from its packs (dreamcast-native/tools/heap_budget.py): allocator model,
    # pack arithmetic, the hardware runs of 13 October 2026 when their asset sets are under build/, and,
    # with NATIVE_HEAP_TEST_ASSETS, that set against the budget (2 MiB never taken at the peak, 3 MiB free
    # while the scene runs) at NATIVE_HEAP_TEST_STAGE, without and with the sample profile's buffer.
    set(NATIVE_HEAP_TEST_ASSETS "" CACHE PATH "Prepared packs whose Dreamcast heap must stay inside the budget")
    set(NATIVE_HEAP_TEST_STAGE "6" CACHE STRING "Scenery stage of the native heap budget check")
    if(NATIVE_HEAP_TEST_ASSETS)
        add_test(NAME native_heap_budget
            COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_heap_budget.py
                    ${NATIVE_HEAP_TEST_ASSETS} ${NATIVE_HEAP_TEST_STAGE})
    else()
        add_test(NAME native_heap_budget
            COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_heap_budget.py)
    endif()
    add_test(NAME native_material_residency
        COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}"
                $<TARGET_FILE:Python3::Interpreter> ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_material_residency.py)
    # Palette RAM format (make NATIVE_PAL16): conversion rules, and with the heap check's race assets (and the HUD
    # pack) every palette the program writes against its ARGB8888 source, default build and ARGB1555.
    set(NATIVE_PALETTE_TEST_ARGS)
    if(NATIVE_HEAP_TEST_ASSETS)
        list(APPEND NATIVE_PALETTE_TEST_ARGS ${NATIVE_HEAP_TEST_ASSETS})
        if(NATIVE_HUD_TEST_PACK)
            list(APPEND NATIVE_PALETTE_TEST_ARGS ${NATIVE_HUD_TEST_PACK})
        endif()
    endif()
    add_test(NAME native_palette_ram
        COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}"
                $<TARGET_FILE:Python3::Interpreter> ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_palette_ram.py
                ${NATIVE_PALETTE_TEST_ARGS})
    add_test(NAME native_meshpack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_models.py
                $<TARGET_FILE:check_native_meshpack>)
    add_test(NAME native_checker_prune
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prune_checker_faces.py
                $<TARGET_FILE:check_native_meshpack>)
    add_test(NAME native_flat_cutouts
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_flat_cutouts.py
                $<TARGET_FILE:check_native_meshpack>)
    add_test(NAME native_asset_rules
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_asset_rules.py
                $<TARGET_FILE:check_native_meshpack>)
    add_test(NAME native_strip_pack
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_strips.py
                $<TARGET_FILE:check_native_meshpack>)
    add_test(NAME native_face_order
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_prepare_face_order.py
                $<TARGET_FILE:check_native_meshpack>)
    add_test(NAME native_split_cutouts
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_split_cutouts.py
                $<TARGET_FILE:check_native_meshpack>)
    set_tests_properties(native_split_cutouts PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME native_opaque_cores
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_opaque_cores.py
                $<TARGET_FILE:check_native_meshpack>)
    set_tests_properties(native_opaque_cores PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME native_lod_groups
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_lod_groups.py
                $<TARGET_FILE:check_native_meshpack>)
    set_tests_properties(native_lod_groups PROPERTIES SKIP_RETURN_CODE 77)
    # NATIVE_DIAG_LATE_CAPTURE (dreamcast-native/code/late_capture.c): frame buffer copy and 2x2 box against numpy,
    # slot policy, file and index, tools/late_capture_report.py (needs numpy and Pillow, else skipped).
    add_test(NAME native_late_capture
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_late_capture.py)
    set_tests_properties(native_late_capture PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME native_meshpack_alignment
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/dreamcast-native/tests/test_align_meshpack.py
                $<TARGET_FILE:check_native_meshpack>)
    add_test(NAME prepare_dreamcast_textures
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/test_prepare_dreamcast_textures.py)
    add_test(NAME lua_core
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/lua_core_test.py
                ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/lua_core)
    set_tests_properties(lua_core PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME lua_plugin_mock
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/lua_plugin_mock_test.py
                ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/lua_plugin_mock)
    set_tests_properties(lua_plugin_mock PROPERTIES SKIP_RETURN_CODE 77)
    # Second decode reference: Ghidra SLEIGH (third-party i960 module via
    # pypcode). Needs scripts/fetch_ghidra_i960.sh and pip install pypcode.
    if(EXISTS ${CMAKE_SOURCE_DIR}/extern/ghidra_i960/data/languages/i960.slaspec)
        add_test(NAME ghidra_oracle
            COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/ghidra_oracle.py
                    ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR}/extern/ghidra_i960 --random 200000)
        set_tests_properties(ghidra_oracle PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# Differential test against MAME's disassembler. Needs scripts/fetch_mame.sh.
set(MAME_DIR ${CMAKE_SOURCE_DIR}/extern/mame CACHE PATH "Pinned MAME checkout")
if(EXISTS ${MAME_DIR}/src/devices/cpu/i960/i960dis.cpp)
    add_executable(mame_oracle
        tests/mame_oracle.cpp
        ${MAME_DIR}/src/devices/cpu/i960/i960dis.cpp
    )
    target_include_directories(mame_oracle PRIVATE
        tests/mame_shim
        ${MAME_DIR}/src/devices
        ${MAME_DIR}/src/devices/cpu/i960
    )
    target_link_libraries(mame_oracle PRIVATE i960)
    find_package(Threads REQUIRED)
    target_link_libraries(mame_oracle PRIVATE Threads::Threads)
    # MAME's source, not ours: don't drown our warnings in its.
    set_source_files_properties(${MAME_DIR}/src/devices/cpu/i960/i960dis.cpp
        PROPERTIES COMPILE_OPTIONS "-w")
    add_test(NAME mame_oracle COMMAND mame_oracle)
else()
    message(STATUS "extern/mame not found: skipping mame_oracle (run scripts/fetch_mame.sh)")
endif()
