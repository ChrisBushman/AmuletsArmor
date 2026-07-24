#!/usr/bin/env csh
# Launches the IRIX/O2 build from the correct working directory (the game
# looks for its resource files relative to cwd, not relative to the
# binary's location) regardless of where this script is invoked from.
#
# N32 binaries on IRIX resolve shared libs via LD_LIBRARYN32_PATH, not
# LD_LIBRARY_PATH (see Build/IRIX-O2/README.md) -- without this, the
# binary fails to find libgcc_s.so.1 (GCC's runtime, installed by TGCware)
# and the Nekoware SDL libs.
set script_dir = `dirname $0`
cd $script_dir
setenv LD_LIBRARYN32_PATH ".:/usr/nekoware/lib:/usr/tgcware/lib"
setenv LD_LIBRARY_PATH    ".:/usr/nekoware/lib:/usr/tgcware/lib"
../Build/IRIX-O2/build/amulets-armor $*
