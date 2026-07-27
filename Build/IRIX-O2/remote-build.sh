#!/usr/bin/env bash
# Sync this repo to an SGI O2 (IRIX 6.5) box over SSH and build it there.
#
# Usage:
#   AA_O2_HOST=aa-o2 ./Build/IRIX-O2/remote-build.sh [make-target...]
#   AA_O2_HOST=aa-o2 ./Build/IRIX-O2/remote-build.sh --sync-assets [make-target...]
#
# Env vars:
#   AA_O2_HOST   (required) ssh destination, e.g. "bushmac@10.0.0.42" or a
#                ~/.ssh/config Host alias.
#   AA_O2_PATH   remote directory to sync into
#                (default: ~/aa-o2-build/AmuletsArmor)
#   AA_O2_PORT   ssh port (default: 22)
#   AA_O2_MAKE   make binary to invoke remotely (default: the only GNU Make
#                actually on this box, from a bundled cross-toolchain kit --
#                see README.md. There is no Nekoware make package, and
#                IRIX's own /usr/bin/make is MIPSPro's, not GNU.)
#
# No rsync exists on this box (Nekoware doesn't package it, and IRIX's own
# /usr/bin/rsync is an unrelated legacy tool with the same name -- an old
# RCS-adjacent utility, not the file-sync one). Syncing goes over a plain
# tar-over-ssh pipe instead. Exe/ (game resource files, ~111MB, essentially
# static) is excluded from the default sync since it dominates transfer
# time for no benefit on repeat builds -- pass --sync-assets the first time
# (or whenever Exe/ actually changes) to include it.
#
# Any extra arguments are passed through to `make` on the remote box, e.g.:
#   ./Build/IRIX-O2/remote-build.sh clean all
#   ./Build/IRIX-O2/remote-build.sh run

set -euo pipefail

if [ -z "${AA_O2_HOST:-}" ]; then
    echo "error: set AA_O2_HOST to the ssh destination of the O2 (user@host, or a ~/.ssh/config alias)" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REMOTE_PATH="${AA_O2_PATH:-~/aa-o2-build/AmuletsArmor}"
SSH_PORT="${AA_O2_PORT:-22}"
AA_O2_MAKE="${AA_O2_MAKE:-/usr/people/bushmac/sh-SOA960904-hms/bin/make}"

SYNC_ASSETS=0
if [ "${1:-}" = "--sync-assets" ]; then
    SYNC_ASSETS=1
    shift
fi
MAKE_TARGETS=("$@")
if [ "${#MAKE_TARGETS[@]}" -eq 0 ]; then
    MAKE_TARGETS=(all)
fi

TAR_EXCLUDES=(
    --exclude='.git'
    --exclude='build'
    --exclude='build_*'
    --exclude='Build/MacOSX-PPC/build'
    --exclude='Build/IRIX-O2/build'
    --exclude='dist'
)
if [ "$SYNC_ASSETS" -eq 0 ]; then
    TAR_EXCLUDES+=(--exclude='Exe')
    echo "==> Syncing $REPO_ROOT to $AA_O2_HOST:$REMOTE_PATH (source only; pass --sync-assets to include Exe/)"
else
    TAR_EXCLUDES+=(--exclude='Exe/S0000000')
    TAR_EXCLUDES+=(--exclude='Exe/resolution.ini')
    echo "==> Syncing $REPO_ROOT to $AA_O2_HOST:$REMOTE_PATH (including Exe/ assets)"
fi

ssh -p "$SSH_PORT" "$AA_O2_HOST" "mkdir -p $REMOTE_PATH"
# Plain tar, no compression: IRIX's own /bin/tar is SysV-style and has no
# gzip integration (no `z` flag), and there's no guarantee gzip/GNU tar
# exists remotely either. LAN-local, so the extra bytes don't matter much.
tar cf - -C "$REPO_ROOT" "${TAR_EXCLUDES[@]}" . | \
    ssh -p "$SSH_PORT" "$AA_O2_HOST" "cd $REMOTE_PATH && tar xf -"

echo "==> Building on $AA_O2_HOST ($AA_O2_MAKE ${MAKE_TARGETS[*]})"
ssh -p "$SSH_PORT" "$AA_O2_HOST" \
    "cd $REMOTE_PATH/Build/IRIX-O2 && $AA_O2_MAKE ${MAKE_TARGETS[*]}"

echo "==> Done. Binary at $REMOTE_PATH/Build/IRIX-O2/build/amulets-armor on $AA_O2_HOST"
