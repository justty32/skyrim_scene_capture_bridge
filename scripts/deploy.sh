#!/usr/bin/env bash
# deploy.sh — put the freshly built DLL into the MO2 mod folders, SAFELY.
#
# WHY THIS SCRIPT EXISTS (2026-07-12, learned the hard way — it crashed a live game):
#
# A plain `cp new target` on Linux opens the target with O_TRUNC and writes the new
# bytes into THE SAME INODE. Windows would refuse (the file is locked by the running
# process); Linux/Proton does not. A loaded DLL's code pages are DEMAND-PAGED from
# that file — the game maps it and faults pages in lazily, as execution reaches them.
# Overwrite the file underneath a running game and the next page-in reads bytes from
# the NEW file at an offset that meant something else in the OLD one. The instruction
# stream is garbage: the game dies WITHOUT A CRASH LOG (the handler itself may be in
# an un-faulted page). This is brutal to diagnose after the fact — it looks like "the
# new DLL is broken" when the DLL is fine.
#
# The fix is to never write through an existing inode:
#   cp new target.tmp && mv target.tmp target        # rename(2) = atomic inode swap
# The running game keeps its mapping to the OLD (now unlinked) inode and finishes its
# session undisturbed; the next launch opens the new one.
#
# Even so: deploying under a running game means the game is running code that no
# longer matches any file on disk. We refuse by default; --force only if you know why.
set -euo pipefail

MODS="${MODFORGE_MO2_MODS:-$HOME/games/mod-organizer-2-skyrimspecialedition/modorganizer2/mods}"
SRC="${1:-$(dirname "$0")/../build/release-clang-cl-linux/SceneCaptureBridge.dll}"
FORCE="${2:-}"

# The folder WITH the esp is the one the game actually loads; the "Release" one is a
# staging copy. Keep both in step so a reinstall can't silently revert one.
TARGETS=(
    "$MODS/SceneCaptureBridge/SKSE/Plugins/SceneCaptureBridge.dll"
    "$MODS/SceneCaptureBridge Release/SKSE/Plugins/SceneCaptureBridge.dll"
)

[[ -f "$SRC" ]] || { echo "deploy: no DLL at $SRC (build first)" >&2; exit 1; }

if pgrep -f "SkyrimSE.exe" >/dev/null 2>&1; then
    echo "deploy: ⚠️  SkyrimSE.exe IS RUNNING." >&2
    if [[ "$FORCE" != "--force" ]]; then
        echo "deploy: refusing — swapping a mapped DLL under a live game is how you get a" >&2
        echo "        silent, log-less crash. Quit the game (and check for a leftover"      >&2
        echo "        ModOrganizer.exe holding wineserver open) and re-run."                >&2
        exit 2
    fi
    echo "deploy: --force given; using the tmp+rename path so the LIVE game keeps its" >&2
    echo "        old inode. It still won't see the new code until a full restart."    >&2
fi

crc=$(python3 -c "import zlib,sys;print('%08x'%(zlib.crc32(open(sys.argv[1],'rb').read())&0xffffffff))" "$SRC")
for t in "${TARGETS[@]}"; do
    mkdir -p "$(dirname "$t")"
    cp "$SRC" "$t.tmp"     # write a NEW inode
    mv -f "$t.tmp" "$t"    # rename(2): atomic swap, never touches the old inode's pages
    echo "deploy: $t"
done
echo "deploy: DLL crc32 $crc"
echo "deploy: ⚠️  a running game must be FULLY restarted to load the new DLL."
