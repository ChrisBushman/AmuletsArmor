cmake_minimum_required(VERSION 3.20)

project(AmuletsArmor LANGUAGES C CXX)

option(AA_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(AA_ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(AA_EXE_NAME "amulets-armor")

file(GLOB AA_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.C")

list(REMOVE_ITEM AA_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/DEBUGBOR.C"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/TOSDATA.C"
    # DEPRECATED (see the file's own header comment); nothing else in the
    # codebase calls or includes it. Its UIBUTTON.H's T_buttonHandler
    # typedef genuinely conflicts with BUTTON.H's (different function
    # pointer signature) once both land in the same translation unit --
    # AppleClang tolerates the redefinition, other compilers (e.g. GCC on
    # the PowerPC/Tiger build) correctly reject it.
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UIBUTTON.C"
)

set(AA_PLATFORM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Build/Windows/VC2013/AA/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Build/Windows/VC2013/AA/direct.cpp"
)

list(APPEND AA_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/Win32/ipx_client.cpp")

if(UNIX)
    # TRUE-HIGHRES: RESSCALE lives under Source/Win32 (with ipx_client.cpp)
    # so it isn't picked up by the Source/*.C glob above; HIGHRES.C is
    # directly under Source/ and is picked up automatically. Needed on
    # both macOS and Linux -- anywhere the Windows VC2013 project doesn't
    # already wire it in some other way.
    list(APPEND AA_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/Win32/RESSCALE.C")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Win32/RESSCALE.C"
        PROPERTIES LANGUAGE C)
endif()

add_executable(${AA_EXE_NAME}
    ${AA_CORE_SOURCES}
    ${AA_PLATFORM_SOURCES}
)

add_executable(aa-character-dump
    "${CMAKE_CURRENT_SOURCE_DIR}/Utils/character_dump_json.c"
)

# The project uses uppercase .C files as C sources; force C language mode.
set_source_files_properties(${AA_CORE_SOURCES} PROPERTIES LANGUAGE C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl)
pkg_check_modules(SDL_NET REQUIRED IMPORTED_TARGET SDL2_net)

target_include_directories(${AA_EXE_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/Build/Windows/VC2013/AA"
)

target_include_directories(aa-character-dump
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(${AA_EXE_NAME}
    PRIVATE
        PkgConfig::SDL
        PkgConfig::SDL_NET
)

target_compile_definitions(${AA_EXE_NAME}
    PRIVATE
        WIN32=1
        NO_ASSEMBLY=1
        _CRT_SECURE_NO_WARNINGS=1
        _MBCS=1
        cdecl=
        _cdecl=
        __cdecl=
        __fastcall=
        __stdcall=
        SCREEN_WIDTH=320      # TRUE-HIGHRES: classic UI screen size,
        SCREEN_HEIGHT=200     # needed by HIGHRES/RESSCALE main.c integration
)

target_compile_definitions(${AA_EXE_NAME} PRIVATE WIN_IPX=1)

if(UNIX)
    # CMake's UNIX is true on both Linux and APPLE (macOS/PPC-Tiger are
    # Unix-based); this covers all of them with the one TARGET_UNIX path.
    target_compile_definitions(${AA_EXE_NAME} PRIVATE TARGET_UNIX=1)
    target_compile_definitions(aa-character-dump PRIVATE TARGET_UNIX=1)
endif()

if(UNIX AND NOT APPLE)
    # Linux (unlike macOS, which relies on a real Homebrew-provided
    # SDLmain to satisfy SDL_main.h's __MACOSX__-triggered `main` ->
    # `SDL_main` rename) has no SDLmain shim at all -- see main.c's own
    # AA_GENERIC_UNIX_MAIN block/comment, the same mechanism already
    # proven by the IRIX-O2 Makefile build for the same reason.
    target_compile_definitions(${AA_EXE_NAME} PRIVATE AA_GENERIC_UNIX_MAIN=1)

    # Let the release archive be self-contained: a lib/ folder copied in
    # alongside the binary (see .github/workflows/linux-release-build.yml)
    # is found via $ORIGIN-relative rpath, the same way IRIX-O2/PPC-Tiger
    # releases already bundle their own SDL libs instead of requiring the
    # end user to install sdl12-compat/SDL2_net themselves. --disable-new-dtags
    # forces the old-style DT_RPATH (transitive across the whole load
    # chain) instead of GCC's default DT_RUNPATH, which only resolves the
    # executable's own *direct* NEEDED entries -- SDL2_net's own transitive
    # need for libSDL2 would otherwise fail to find our bundled copy.
    target_link_options(${AA_EXE_NAME} PRIVATE -Wl,--disable-new-dtags)
    set_target_properties(${AA_EXE_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib"
    )
endif()

if(APPLE)
    # sdl12-compat loads SDL2 dynamically; add Homebrew and pkg-config library
    # directories to the runtime search path so startup does not depend on shell env.
    set(AA_RPATH_DIRS
        "/usr/local/opt/sdl12-compat/lib"
        "/usr/local/opt/sdl2/lib"
    )

    if(SDL_LIBRARY_DIRS)
        list(APPEND AA_RPATH_DIRS ${SDL_LIBRARY_DIRS})
    endif()

    if(SDL_NET_LIBRARY_DIRS)
        list(APPEND AA_RPATH_DIRS ${SDL_NET_LIBRARY_DIRS})
    endif()

    list(REMOVE_DUPLICATES AA_RPATH_DIRS)

    set_target_properties(${AA_EXE_NAME} PROPERTIES
        BUILD_RPATH "${AA_RPATH_DIRS}"
        INSTALL_RPATH "${AA_RPATH_DIRS}"
    )
endif()

if(AA_WARNINGS_AS_ERRORS)
    target_compile_options(${AA_EXE_NAME} PRIVATE -Werror)
endif()

if(AA_ENABLE_ASAN)
    target_compile_options(${AA_EXE_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${AA_EXE_NAME} PRIVATE -fsanitize=address)
endif()

set(AA_RUNTIME_DIR "${CMAKE_BINARY_DIR}/runtime")

add_custom_command(TARGET ${AA_EXE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AA_RUNTIME_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Exe"
        "${AA_RUNTIME_DIR}"
    COMMENT "Copying runtime assets to ${AA_RUNTIME_DIR}"
)
