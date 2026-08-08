#!/bin/sh
# Build an OS-9-mountable NDIF (Rdxx) HFS disk image from a folder holding the
# Amulets & Armor OS 9 app + game data, preserving the app's resource fork.
#
# Why NDIF/Rdxx and not a plain .dmg: classic Mac apps keep their code fragment
# (cfrg) and SIZE partition in a resource fork. A raw UDRW .dmg may not mount
# under OS 9's Disk Copy; an NDIF ("Rdxx") image of an HFS volume does, with the
# fork intact -- so the app launches straight from the mounted image.
#
# Run on Mac OS X (needs hdiutil, ditto, and /Developer/Tools Rez/SetFile/DeRez).
#
# Usage: make-os9-image.sh <src-folder> <output.img> [volume-name]
#   <src-folder>   folder holding AA (the game) + ALL game data: the .RES
#                  archives, L*.MAP (quests won't start without them), AAMUSIC/
#                  (music), MAPDESC/, the .FRM/level/config files, etc.
#   <output.img>   path to write the NDIF image (.img appended if missing)
#   [volume-name]  mounted-volume name (default "Amulets & Armor")
#
# The image contains one folder "AmuletsArmor" with the app + data; under OS 9
# you double-click the .img, open the folder, and run AA. (To save
# games you must run from a writable disk -- copy the folder to the OS 9 HD.)

set -e
SRC="$1"
OUT="${2%.img}"                 # strip .img; hdiutil convert appends it
VOL="${3:-Amulets & Armor}"
APPDIR="AmuletsArmor"
APP="AA"                         # game binary name (aligned with the other platforms)

[ -d "$SRC" ] && [ -n "$OUT" ] || {
    echo "usage: $0 <src-folder> <output.img> [volume-name]" >&2 ; exit 1 ; }

RW="/tmp/aa_os9_rw_$$.dmg"
ATT="/tmp/aa_os9_att_$$"
trap 'rm -f "$RW" "$ATT"' EXIT

# Size the read/write image ~30% over the source (minimum 64 MB).
SZM=`du -sk "$SRC" | awk '{m=int($1*1.3/1024)+1; if(m<64)m=64; print m}'`
echo ">>> create ${SZM}MB HFS image (no partition map -- NDIF requires none)"
hdiutil create -size "${SZM}m" -layout NONE -fs HFS -volname "$VOL" -ov "$RW" >/dev/null

echo ">>> populate (ditto --rsrc keeps the app's resource fork)"
hdiutil attach "$RW" -nobrowse >"$ATT" 2>&1
MNT="/Volumes/$VOL"
DEV=`awk -v m="$MNT" 'index($0,m){print $1; exit}' "$ATT"`
[ -d "$MNT" ] || { echo "mount failed" >&2 ; cat "$ATT" >&2 ; exit 1 ; }
mkdir -p "$MNT/$APPDIR"
ditto --rsrc "$SRC" "$MNT/$APPDIR"
rm -f "$MNT/$APPDIR/aa_boot.log" "$MNT/$APPDIR/stderr.txt" "$MNT/$APPDIR/stdout.txt"
[ -f "$MNT/$APPDIR/$APP" ] && /Developer/Tools/SetFile -t APPL -c AmAr "$MNT/$APPDIR/$APP"
sync ; hdiutil detach "$DEV" >/dev/null 2>&1 || hdiutil detach "$MNT" -force >/dev/null

echo ">>> convert to NDIF Rdxx"
rm -f "$OUT.img" "$OUT.dmg"
hdiutil convert "$RW" -format Rdxx -o "$OUT" >/dev/null
[ -f "$OUT.img" ] || mv "$OUT.dmg" "$OUT.img"
echo ">>> done: $OUT.img"
hdiutil imageinfo "$OUT.img" | grep -iE "Class Name: CNDIF|Checksum Type" || true
