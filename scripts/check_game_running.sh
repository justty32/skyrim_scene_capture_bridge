#!/usr/bin/env bash
# check_game_running.sh — 「Skyrim／MO2 是不是還在跑？」的偵測，抽成獨立腳本。
#
# 為什麼不用 `pgrep -f`（2026-08-26）：
#
#   pgrep -f 掃的是每個行程的**完整命令列**，包含執行這個檢查的 shell 自己。人手在
#   乾淨的互動 shell 打 `pgrep -f SkyrimSE.exe` 沒事，但 agent 透過工具驅動時，那一次
#   呼叫的命令列（連同外層 `zsh -c '...'` 的整段字串、heredoc、檔案路徑）字面就含有
#   "SkyrimSE.exe"——於是 pgrep 掃到呼叫端自己，回報「遊戲在跑」。實測連
#   `[S]kyrimSE\.exe` 這種括號拆字寫法也擋不住，因為命令列裡照樣有那串字面。
#
#   誤判的代價不是「多喊一次」：deploy.sh 會 exit 2 拒絕部署，逼人養成每次都加
#   --force 的習慣，而那支守門存在的理由是 2026-07-12 覆寫執行中 DLL 造成遊戲無日誌
#   暴斃的教訓。廢掉守門比誤判更糟。
#
#   修法見 ../../agentctl/docs/resource-locks.md「pgrep 自我匹配陷阱」：比對 `ps -eo comm=`
#   （核心記的執行檔名，不是命令列，所以呼叫端寫什麼都掃不進來），並且把檢查放進這支
#   獨立腳本、由 deploy.sh 分開呼叫。
#
# 用法：
#   check_game_running.sh [目標檔案...]
#
# 輸出：每個命中一行，`GAME <說明>` 或 `MO2 <說明>`。
# Exit：0 = 有任何命中；1 = 全部沒命中；2 = 用法錯誤。
#   （注意方向：0 代表「在跑」，不是「沒事」。呼叫端要看輸出的分類決定怎麼處理。）
set -uo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    sed -n '/^# 用法：/,/^# *（注意方向/p' "$0" | sed 's/^# \?//'
    exit 0
fi

# comm 的分類。核心的 TASK_COMM_LEN 是 16，所以 comm 最長 15 字元、更長的會被截斷
# （"ModOrganizer.exe" 有 16 字元 → comm 是 "ModOrganizer.ex"），比對時兩種都認。
GAME_COMMS=(SkyrimSE.exe skse64_loader.exe)
MO2_COMMS=(ModOrganizer.exe)

hits=()

match_comm() {
    local comm="$1" want
    shift
    for want in "$@"; do
        [[ "$comm" == "$want" || "$comm" == "${want:0:15}" ]] && return 0
    done
    return 1
}

while IFS= read -r comm; do
    [[ -n "$comm" ]] || continue
    if match_comm "$comm" "${GAME_COMMS[@]}"; then
        hits+=("GAME 行程執行中：comm=$comm")
    elif match_comm "$comm" "${MO2_COMMS[@]}"; then
        hits+=("MO2 行程執行中：comm=$comm")
    fi
done < <(ps -eo comm=)

# 第二個訊號，跟行程叫什麼名字無關：**目標檔案本身是不是正被某個行程 mmap 進來**。
# 這才是 2026-07-12 那個坑的精確條件——DLL 的程式碼頁是從檔案 demand-page 進來的，
# 被 mmap 著的時候就不能寫穿它的 inode。Wine 底下的 comm 未必總是等於 exe 名，這條當
# 保險；同時它也完全免疫自我匹配（掃的是 /proc/*/maps 的映射路徑，不是命令列）。
self=$$
for target in "$@"; do
    [[ -e "$target" ]] || continue
    real="$(readlink -f -- "$target")" || continue
    for maps in /proc/[0-9]*/maps; do
        pid="${maps#/proc/}"
        pid="${pid%/maps}"
        [[ "$pid" == "$self" ]] && continue
        if grep -qF -- "$real" "$maps" 2>/dev/null; then
            pcomm="$(cat "/proc/$pid/comm" 2>/dev/null || echo '?')"
            hits+=("GAME 目標檔案正被 mmap：$real ← pid $pid ($pcomm)")
        fi
    done
done

# 同一支程式可能有多個 thread／多個命中，去重後輸出
if [[ "${#hits[@]}" -gt 0 ]]; then
    printf '%s\n' "${hits[@]}" | sort -u
    exit 0
fi
exit 1
