#!/usr/bin/env bash
# Sync this repo to a PowerPC Mac OS X Tiger box over SSH and build the
# G3/Panther platform there (Apple gcc 4.0 + 10.3.9 SDK + G3-native SDL/SDL_net;
# see the Makefile). The Tiger box is the build host because it carries the
# 10.3.9 SDK and the Apple compilers; the *output* targets Panther/G3.
#
# Requires the G3-native SDL/SDL_net to already be built into ~/aa-g3/prefix on
# the build box (see Build/MacOSX-PPC-G3/README.md).
#
# Usage:
#   AA_PPC_HOST=user@tiger-box ./Build/MacOSX-PPC-G3/remote-build.sh [make-target...]
#
# Env vars:
#   AA_PPC_HOST   (required) ssh destination, e.g. "aa-tiger" (a ~/.ssh alias).
#   AA_PPC_PATH   remote directory to sync into (default: ~/aa-ppc-g3-build/AmuletsArmor)
#   AA_PPC_PORT   ssh port (default: 22)
#   AA_PPC_MAKE   make binary (default: auto-detect gmake; Tiger's /usr/bin/make
#                 3.80 is too buggy for this Makefile's vpath rules -- use gmake).

set -euo pipefail

if [ -z "${AA_PPC_HOST:-}" ]; then
    echo "error: set AA_PPC_HOST to the ssh destination of the Tiger box (user@host)" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REMOTE_PATH="${AA_PPC_PATH:-~/aa-ppc-g3-build/AmuletsArmor}"
SSH_PORT="${AA_PPC_PORT:-22}"
MAKE_TARGETS=("$@")
if [ "${#MAKE_TARGETS[@]}" -eq 0 ]; then
    MAKE_TARGETS=(all)
fi

echo "==> Syncing $REPO_ROOT to $AA_PPC_HOST:$REMOTE_PATH"
ssh -p "$SSH_PORT" "$AA_PPC_HOST" "mkdir -p $REMOTE_PATH"
rsync -avz --delete \
    -e "ssh -p $SSH_PORT" \
    --exclude='.git/' \
    --exclude='out/' \
    --exclude='build/' \
    --exclude='build_*/' \
    --exclude='Build/MacOSX-PPC/build/' \
    --exclude='Build/MacOSX-PPC-G3/build/' \
    --exclude='dist/' \
    "$REPO_ROOT/" "$AA_PPC_HOST:$REMOTE_PATH/"

if [ -z "${AA_PPC_MAKE:-}" ]; then
    AA_PPC_MAKE=$(ssh -p "$SSH_PORT" "$AA_PPC_HOST" \
        'PATH="/usr/local/bin:$PATH"; command -v gmake || command -v make')
fi

echo "==> Building G3 on $AA_PPC_HOST ($AA_PPC_MAKE ${MAKE_TARGETS[*]})"
ssh -p "$SSH_PORT" "$AA_PPC_HOST" \
    "export PATH=/usr/local/bin:\$PATH; cd $REMOTE_PATH/Build/MacOSX-PPC-G3 && $AA_PPC_MAKE ${MAKE_TARGETS[*]}"

echo "==> Done. Binary at $REMOTE_PATH/Build/MacOSX-PPC-G3/build/amulets-armor on $AA_PPC_HOST"
