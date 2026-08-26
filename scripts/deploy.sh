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

# Detection lives in scripts/check_game_running.sh, called as a SEPARATE process on
# purpose. `pgrep -f` used to be inlined here and it scans full command lines — including
# the command line of whatever invoked deploy.sh. When an agent drives this through a
# tool call whose command line happens to contain the literal "SkyrimSE.exe", pgrep -f
# matched the CALLER and this script refused to deploy with nothing running. The
# `[S]kyrimSE\.exe` bracket trick does not help either: the caller's command line
# contains that literal too. The checker compares `ps -eo comm=` (the kernel's exe name,
# not the command line) and also looks for the target file being mmap'd by a live
# process, which is the actual dangerous condition. See its header, and
# ../../agentctl/docs/resource-locks.md "pgrep 自我匹配陷阱".
CHECKER="$(dirname "$0")/check_game_running.sh"
running="$("$CHECKER" "${TARGETS[@]}" || true)"
game_hits="$(grep '^GAME' <<<"$running" || true)"
mo2_hits="$(grep '^MO2' <<<"$running" || true)"

if [[ -n "$game_hits" ]]; then
    echo "deploy: ⚠️  THE GAME IS RUNNING:" >&2
    sed 's/^/deploy:     /' <<<"$game_hits" >&2
    if [[ "$FORCE" != "--force" ]]; then
        echo "deploy: refusing — swapping a mapped DLL under a live game is how you get a" >&2
        echo "        silent, log-less crash. Quit the game and re-run."                   >&2
        if [[ -n "$mo2_hits" ]]; then
            echo "deploy: MO2 is up too; it holds wineserver open and can keep a dead" >&2
            echo "        SkyrimSE.exe around. Close it as well if the game won't go:"  >&2
            sed 's/^/deploy:     /' <<<"$mo2_hits" >&2
        fi
        exit 2
    fi
    echo "deploy: --force given; using the tmp+rename path so the LIVE game keeps its" >&2
    echo "        old inode. It still won't see the new code until a full restart."    >&2
elif [[ -n "$mo2_hits" ]]; then
    # MO2 alone does not map the DLL, so it is not a reason to refuse — but it does need
    # an F5 before the new file shows up in its mod list.
    echo "deploy: note — MO2 is running (no game process found). Deploying anyway;" >&2
    echo "        press F5 in MO2 afterwards to pick the new file up."              >&2
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
