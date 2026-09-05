#!/usr/bin/env bash
# test_game_detection.sh — check_game_running.sh 的契約測試（fixture，不需要真的遊戲）。
#
# 為什麼要有這支：deploy.sh 的守門是 2026-07-12 「覆寫執行中 DLL → 遊戲無日誌暴斃」那
# 次教訓留下來的，而它 100% 依賴 check_game_running.sh 判斷對不對。2026-09-05 發現偵測
# 靠 `ps -eo comm=` 比對 "SkyrimSE.exe" 在 Proton 底下是**保證偽陰性**（實測 comm 是
# `Main`／`main`），也就是守門在該擋的時候會放行。沒有測試就沒人會發現。
#
# 每條都做過紅燈證明：把 check_game_running.sh 的 maps 掃描拿掉，① ② 兩條會 FAIL。
#
# 用法：scripts/test_game_detection.sh
# Exit：0 = 全過；1 = 有失敗。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CHECKER="$HERE/check_game_running.sh"
FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/scb-game-detection-XXXXXX")"
HOLDERS=()

cleanup() {
    for p in ${HOLDERS[@]+"${HOLDERS[@]}"}; do kill "$p" 2>/dev/null; done
    rm -rf "$FIXTURE"
}
trap cleanup EXIT

fails=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

# 一個把檔案 mmap 住並活著不動的行程。它的 comm 是 `python3`——刻意的：那正是 Proton
# 底下的實況（行程名跟 exe 名無關），comm 比對抓不到它，maps 比對抓得到。
cat > "$FIXTURE/holder.py" <<'PY'
import mmap, os, sys, time
f = open(sys.argv[1], 'rb')
held = mmap.mmap(f.fileno(), 0, prot=mmap.PROT_READ)  # 一定要留參照，否則立刻被 GC 解除映射
print(os.getpid(), flush=True)
time.sleep(300)
PY

start_holder() {  # start_holder <要被映射的檔> -> echo pid
    local file="$1" out pid
    out="$FIXTURE/holder-pid-$$-${#HOLDERS[@]}"
    python3 "$FIXTURE/holder.py" "$file" > "$out" 2>/dev/null &
    for _ in $(seq 1 50); do
        pid="$(head -1 "$out" 2>/dev/null)"
        [[ -n "$pid" ]] && break
        sleep 0.1
    done
    [[ -n "$pid" ]] || return 1
    HOLDERS+=("$pid")
    echo "$pid"
}

# --- ① 遊戲執行檔被映射就要偵測到，即使 comm 完全不像 SkyrimSE ---
mkdir -p "$FIXTURE/game"
cp /bin/true "$FIXTURE/game/SkyrimSE.exe"
if pid="$(start_holder "$FIXTURE/game/SkyrimSE.exe")"; then
    out="$("$CHECKER")"; rc=$?
    comm="$(cat "/proc/$pid/comm" 2>/dev/null)"
    if [[ $rc -eq 0 ]] && grep -q '^GAME' <<<"$out"; then
        pass "SkyrimSE.exe 被映射時偵測到遊戲（該行程 comm=$comm，不是 exe 名——正是 Proton 的情況）"
    else
        fail "SkyrimSE.exe 被映射卻沒偵測到（rc=$rc）：comm=$comm 的行程被漏掉了"
    fi
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
else
    fail "無法啟動 fixture holder（①）"
fi

# --- ② MO2 執行檔被映射要分類成 MO2、不是 GAME ---
mkdir -p "$FIXTURE/mo2"
cp /bin/true "$FIXTURE/mo2/ModOrganizer.exe"
if pid="$(start_holder "$FIXTURE/mo2/ModOrganizer.exe")"; then
    out="$("$CHECKER")"; rc=$?
    if [[ $rc -eq 0 ]] && grep -q '^MO2' <<<"$out" && ! grep -q '^GAME' <<<"$out"; then
        pass "ModOrganizer.exe 被映射時分類成 MO2 而非 GAME"
    else
        fail "MO2 分類錯（rc=$rc）：$out"
    fi
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
else
    fail "無法啟動 fixture holder（②）"
fi

# --- ③ 目標 DLL 正被映射（2026-07-12 那個坑的精確條件）要偵測到 ---
mkdir -p "$FIXTURE/mods/SKSE/Plugins"
cp /bin/true "$FIXTURE/mods/SKSE/Plugins/SceneCaptureBridge.dll"
if pid="$(start_holder "$FIXTURE/mods/SKSE/Plugins/SceneCaptureBridge.dll")"; then
    out="$("$CHECKER" "$FIXTURE/mods/SKSE/Plugins/SceneCaptureBridge.dll")"; rc=$?
    if [[ $rc -eq 0 ]] && grep -q 'mmap' <<<"$out"; then
        pass "目標 DLL 正被 mmap 時偵測到"
    else
        fail "目標 DLL 被 mmap 卻沒偵測到（rc=$rc）：$out"
    fi
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
else
    fail "無法啟動 fixture holder（③）"
fi

# --- ④ 自我匹配免疫：呼叫端命令列含 "SkyrimSE.exe" 字面不得誤判 ---
# 這是 2026-08-26 廢掉 pgrep -f 的原因，換成 maps 掃描後必須仍然成立。
out="$(bash -c 'echo "SkyrimSE.exe skse64_loader.exe ModOrganizer.exe" >/dev/null; "$1"' _ "$CHECKER")"; rc=$?
if [[ $rc -eq 1 && -z "$out" ]]; then
    pass "呼叫端命令列含 SkyrimSE.exe 字面時不誤判（自我匹配免疫）"
else
    fail "自我匹配免疫失效（rc=$rc）：$out"
fi

# --- ⑤ 什麼都沒跑的時候要 exit 1、沒有輸出 ---
out="$("$CHECKER")"; rc=$?
if [[ $rc -eq 1 && -z "$out" ]]; then
    pass "沒有遊戲／MO2 在跑時 exit 1 且無輸出"
else
    fail "空環境誤報（rc=$rc）：$out"
fi

# --- ⑥ --help 要 exit 0 並印出用法 ---
out="$("$CHECKER" --help)"; rc=$?
if [[ $rc -eq 0 ]] && grep -q '用法' <<<"$out"; then
    pass "--help 回報成功並印出用法"
else
    fail "--help 壞了（rc=$rc）：$out"
fi

if [[ $fails -eq 0 ]]; then
    echo "all game-detection contract tests passed"
    exit 0
fi
echo "$fails test(s) failed" >&2
exit 1
