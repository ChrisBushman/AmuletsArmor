#!/usr/bin/env bash
# Remove all Linux CMake build output.
# Mirrors the role of Build/MacOSX/clean.sh for the Linux CMake path.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "Cleaning!"

for dir in "$REPO_ROOT/out/linux" \
           "$REPO_ROOT/out/linux-asan" \
           "$REPO_ROOT/out/linux-release"; do
    if [ -d "$dir" ]; then
        rm -rf "$dir"
        echo "  removed $dir"
    fi
done

echo "Clean -- Done"
