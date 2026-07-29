# Copies Exe/ game data into a platform build's runtime/release output,
# excluding Windows/DOS-only executables, DLLs, and launcher scripts.
#
# Exe/ dual-purposes as both the ready-to-ship Windows distribution folder
# (AA.exe, AALauncher.exe, AAServer.exe, SDL.dll, DOSBoxRun*.bat, etc. have
# lived there since the original Windows release) and the source-of-truth
# game data (FRM/SRP/RES/... ) every other platform's build pulls from --
# so a non-Windows build must not blanket-copy it, or those Windows-only
# files leak into e.g. a Linux/macOS release archive where they can never
# run. Shared (via -P from CMakeLists.txt's POST_BUILD step, and directly
# by CI packaging steps like linux-release-build.yml) so the exclude list
# only needs to be maintained in one place.
#
# Usage: cmake -DAA_EXE_SRC_DIR=<src> -DAA_EXE_DST_DIR=<dst> -P CopyRuntimeAssets.cmake

file(COPY "${AA_EXE_SRC_DIR}/"
    DESTINATION "${AA_EXE_DST_DIR}"
    PATTERN "*.exe" EXCLUDE
    PATTERN "*.EXE" EXCLUDE
    PATTERN "*.dll" EXCLUDE
    PATTERN "*.DLL" EXCLUDE
    PATTERN "*.bat" EXCLUDE
    PATTERN "*.BAT" EXCLUDE
    PATTERN "*.386" EXCLUDE
    REGEX "Utils/DOS/.*" EXCLUDE
)
