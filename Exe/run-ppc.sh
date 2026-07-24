#!/bin/sh
# Launches the PowerPC/Tiger build from the correct working directory
# (the game looks for its resource files relative to cwd, not relative to
# the binary's location) regardless of where this script is invoked from.
# Output is also teed to /tmp/aa_run.log (in addition to this terminal) so
# it can be watched remotely regardless of how this script was launched.
cd "$(dirname "$0")" || exit 1
../Build/MacOSX-PPC/build/amulets-armor "$@" 2>&1 | tee /tmp/aa_run.log
