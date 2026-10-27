# libsa1 with any C99 compiler (MSVC, GCC, Clang).  The reference build is build.py
# (zig, three targets); this file is for hosts that embed libsa1 in their own build,
# e.g. a Garry's Mod module built with MSVC:
#
#   cmake -S . -B build/cmake -A x64        (or -A Win32, or -G Ninja on Linux)
#   cmake --build build/cmake --config Release
#   ctest --test-dir build/cmake -C Release
#
# Targets: sa1 (shared library), sa1_static (static library; consumers get SA1_STATIC),
# unit_test, rip_test.
#
# The world core (src/world: the arena that runs the decompiled game code, sadc
# docs/libsa1_integration.md section 6) is opt-in here: -DSA1_WORLD=ON with GCC or Clang
# and -DSA1_SADC_DIR=<sadc root> (for include/sa1/guest.h).  Without game code the code
# map is empty (ticking reports SA1_ERR_NOT_IMPLEMENTED).  With SA1_WORLD=OFF (the default)
# every target is compiled with SA1_NO_WORLD: the asset layer and the API without the world
# glue; sa1_character_tick reports SA1_ERR_NOT_IMPLEMENTED (MSVC defines it by itself).
#
# Game code: run "py -3.12 build.py" once (it lowers the game with sadc tools/pclower.py and
# generates the boundary stubs), then add -DSA1_GAME_CMAKE=<libsa1>/build/gen/sa1g_game.cmake
# (needs SA1_WORLD=ON and Clang: the lowered code is compiled with the one flag set of sadc
# tools/pc_cflags.txt, design 8.1 / D11).  The certified binaries are build.py's (zig's
# pinned clang); MSVC builds of game code are unsupported.
cmake_minimum_required(VERSION 3.16)
project(libsa1 VERSION 0.2.0 LANGUAGES C)

option(SA1_BUILD_SHARED "Build the shared library (sa1)" ON)
option(SA1_BUILD_TESTS "Build unit_test and rip_test" ON)
option(SA1_WORLD "Build the world core (src/world; GCC/Clang only)" OFF)
set(SA1_SADC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sadc" CACHE PATH "sadc root (include/sa1/guest.h)")
set(SA1_GAME_CMAKE "" CACHE FILEPATH "lowered game code: build/gen/sa1g_game.cmake written by build.py (SA1_WORLD, Clang)")

file(GLOB SA1_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
if(SA1_WORLD)
    if(MSVC OR NOT (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU"))
        message(FATAL_ERROR "SA1_WORLD needs GCC or Clang (section attributes, setjmp recovery, MXCSR guard)")
    endif()
    if(NOT EXISTS "${SA1_SADC_DIR}/include/sa1/guest.h")
        message(FATAL_ERROR "SA1_WORLD: ${SA1_SADC_DIR}/include/sa1/guest.h not found (set SA1_SADC_DIR)")
    endif()
    file(GLOB SA1_WORLD_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/world/*.c)
    if(SA1_GAME_CMAKE)
        # the lowered game code replaces the empty code map
        if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "SA1_GAME_CMAKE: the lowered game code needs Clang (-frounding-math, the pc_cflags.txt set)")
        endif()
        if(NOT EXISTS "${SA1_GAME_CMAKE}")
            message(FATAL_ERROR "SA1_GAME_CMAKE: ${SA1_GAME_CMAKE} not found (run py -3.12 build.py first)")
        endif()
        include(${SA1_GAME_CMAKE})
        list(REMOVE_ITEM SA1_WORLD_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/world/sa1_world_nogame.c)
        separate_arguments(SA1G_FLAGS UNIX_COMMAND "${SA1G_GAME_FLAGS_ALL}")
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            separate_arguments(SA1G_ARCH UNIX_COMMAND "${SA1G_GAME_FLAGS_X86}")
        else()
            separate_arguments(SA1G_ARCH UNIX_COMMAND "${SA1G_GAME_FLAGS_X86_64}")
        endif()
        separate_arguments(SA1G_WARN UNIX_COMMAND "${SA1G_GAME_WARN}")
        # the -Wall -Wextra of sa1_configure() say nothing useful about decompiled code: build.py's set only
        set(SA1G_OPTS ${SA1G_FLAGS} ${SA1G_ARCH} -std=gnu11 -Wno-pedantic -Wno-unused-parameter -Wno-sign-compare
                      -Wno-sometimes-uninitialized ${SA1G_WARN} -DSA1G_TEXT_PRAGMA -include ${SA1G_TEXT_H})
        if(NOT WIN32)
            list(APPEND SA1G_OPTS -fvisibility=hidden)
        endif()
        set_source_files_properties(${SA1G_GAME_SOURCES} PROPERTIES COMPILE_OPTIONS "${SA1G_OPTS};-include;${SA1G_RT_INTRIN_H}")
        set(SA1G_EXTRA ${SA1G_GAME_INFO_C})
        if(SA1G_GAME_GLUE)
            set_source_files_properties(${SA1G_GAME_GLUE} PROPERTIES COMPILE_OPTIONS "${SA1G_OPTS}")
            list(APPEND SA1G_EXTRA ${SA1G_GAME_GLUE})
        endif()
        list(APPEND SA1_SOURCES ${SA1G_GAME_SOURCES} ${SA1G_EXTRA})
        message(STATUS "libsa1: game code linked (${SA1G_GAME_INFO})")
    endif()
    list(APPEND SA1_SOURCES ${SA1_WORLD_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/src/rt/rt_fsca_q.c)
endif()

function(sa1_configure target)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /wd4127)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    if(UNIX)
        # m; Threads: the audio loader thread (src/sa1_audio_load.c)
        find_package(Threads REQUIRED)
        target_link_libraries(${target} PRIVATE m Threads::Threads)
    endif()
    if(NOT SA1_WORLD)
        # src/sa1_api_world.h: no world core in this build (GCC / Clang would otherwise reference sa1w_*)
        target_compile_definitions(${target} PRIVATE SA1_NO_WORLD=1)
    endif()
    if(SA1_WORLD)
        set_source_files_properties(${SA1_WORLD_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/src/rt/rt_fsca_q.c
                                    PROPERTIES COMPILE_OPTIONS "-std=gnu11;-Wno-pedantic;-ffp-contract=off;-frounding-math;-fno-builtin")
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/world ${SA1_SADC_DIR}/include)
        if(SA1_GAME_CMAKE)
            target_include_directories(${target} PRIVATE ${SA1G_GAME_INCLUDES})
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)$")
            target_compile_options(${target} PRIVATE -msse2 -mfpmath=sse)
        endif()
        if(UNIX)
            find_package(Threads REQUIRED)
            target_link_libraries(${target} PRIVATE Threads::Threads)
        endif()
    endif()
endfunction()

add_library(sa1_static STATIC ${SA1_SOURCES})
target_include_directories(sa1_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(sa1_static PUBLIC SA1_STATIC)
set_target_properties(sa1_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
sa1_configure(sa1_static)

if(SA1_BUILD_SHARED)
    add_library(sa1 SHARED ${SA1_SOURCES})
    target_include_directories(sa1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(sa1 PRIVATE SA1_BUILD_SHARED)
    set_target_properties(sa1 PROPERTIES C_VISIBILITY_PRESET hidden VERSION ${PROJECT_VERSION})
    sa1_configure(sa1)
endif()

if(SA1_BUILD_TESTS)
    add_executable(unit_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit_test.c)
    target_include_directories(unit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(unit_test PRIVATE sa1_static)
    sa1_configure(unit_test)
    add_executable(rip_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/rip_test.c)
    target_link_libraries(rip_test PRIVATE sa1_static)
    sa1_configure(rip_test)
    enable_testing()
    add_test(NAME unit_test COMMAND unit_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    if(SA1_WORLD)
        add_executable(spike_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/spike_test.c)
        target_include_directories(spike_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(spike_test PRIVATE sa1_static)
        sa1_configure(spike_test)
        set_target_properties(spike_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
        add_test(NAME spike_test COMMAND spike_test ${SA1_SADC_DIR}/baserom/1ST_READ.BIN -
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()