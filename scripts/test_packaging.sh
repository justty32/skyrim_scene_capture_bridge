#!/usr/bin/env bash
# pack.sh 的打包契約測試（POSIX 版）。
#
# 手法：在暫存目錄造一個最小 fixture（假的 CMakeCache、2 bytes 的假 DLL、一份假的
# config/），把 pack.sh 複製進去跑，然後驗 zip 裡的路徑佈局與 CLI 契約。
#
# 本 repo 自己沒有 config/ 目錄（設定檔目前由 runtime 產生），所以 fixture 是**自己造**
# 一份 config/SceneCaptureBridge.ini。測的是 pack.sh「有 config/ 就搬到
# Data/SKSE/Plugins/<CONFIG_FOLDER>/」這條行為，不是 repo 現況有沒有那個目錄。
#
# PACK_SCRIPT 可覆寫成一份被故意破壞的 pack.sh 副本，用來證明每一條斷言真的會變紅
# （綠燈不等於有檢查）。
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACK_SCRIPT="${PACK_SCRIPT:-$REPO_ROOT/scripts/pack.sh}"
FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/scene-capture-bridge-packaging-XXXXXX")"
trap 'rm -rf "$FIXTURE"' EXIT

failures=0
check() {
  if [[ "$1" == "0" ]]; then echo "PASS: $2"; else echo "FAIL: $2" >&2; failures=$((failures + 1)); fi
}

BUILD_DIR="$FIXTURE/build/release-clang-cl-linux"
mkdir -p "$FIXTURE/scripts" "$FIXTURE/config" "$BUILD_DIR"
cp "$PACK_SCRIPT" "$FIXTURE/scripts/pack.sh"
chmod +x "$FIXTURE/scripts/pack.sh"
printf '[General]\nbLogging=1\n' > "$FIXTURE/config/SceneCaptureBridge.ini"
# 本 repo 的 CMakeLists.txt 把 CONFIG_FOLDER 寫在 if(DEFINED OUTPUT_FOLDER) 區塊內的
# 普通變數，不會進 CMakeCache——所以這裡的 cache fixture 也刻意不放
# PLUGIN_CONFIG_FOLDER，pack.sh 必須靠自己寫死的常數拿到子目錄名。
cat > "$BUILD_DIR/CMakeCache.txt" <<'CACHE'
CMAKE_PROJECT_NAME:STATIC=SceneCaptureBridge
CMAKE_PROJECT_VERSION:STATIC=0.0.1
CACHE
printf 'MZ' > "$BUILD_DIR/SceneCaptureBridge.dll"

# cwd 契約。注意：單純比對呼叫前後的 $PWD 是**恆真斷言**——pack.sh 是子行程，它怎麼
# cd 都不可能改到父 shell 的 cwd，任何注入都弄不紅。所以這條改成驗可觀測的那一半：
# 從別的目錄以**相對** --output-dir 呼叫，zip 必須落在 repo root 底下，而不是呼叫端的
# cwd 底下（pack.sh 內部的 `cd "$REPO_ROOT"` 是為了讓所有相對路徑以 repo 為基準）。
# 順帶仍檢查父 shell 的 $PWD 沒被動到。
CWD_PROBE="$FIXTURE/cwd-probe"
mkdir -p "$CWD_PROBE"
STARTING_PWD="$PWD"
(cd "$CWD_PROBE" && "$FIXTURE/scripts/pack.sh" --output-dir rel-dist >/dev/null) || true
check "$([[ "$PWD" == "$STARTING_PWD" && -f "$FIXTURE/rel-dist/SceneCaptureBridge-0.0.1.zip" && ! -e "$CWD_PROBE/rel-dist" ]] && echo 0 || echo 1)" \
  "pack.sh 不依賴也不改變呼叫端的 cwd（相對 --output-dir 以 repo root 為基準）"

DIST="$FIXTURE/test-dist"
"$FIXTURE/scripts/pack.sh" --output-dir "$DIST" >/dev/null

ZIP="$DIST/SceneCaptureBridge-0.0.1.zip"
check "$([[ -f "$ZIP" ]] && echo 0 || echo 1)" "產生了 SceneCaptureBridge-0.0.1.zip"
if [[ -f "$ZIP" ]]; then
  entries="$(unzip -Z1 "$ZIP")"
  check "$(grep -qx 'Data/SKSE/Plugins/SceneCaptureBridge.dll' <<<"$entries" && echo 0 || echo 1)" \
    "DLL 在 Data/SKSE/Plugins/ 底下"
  check "$(grep -qx 'Data/SKSE/Plugins/SceneCaptureBridge/SceneCaptureBridge.ini' <<<"$entries" && echo 0 || echo 1)" \
    "設定檔在 runtime 的 SceneCaptureBridge/ 子目錄底下"
  # 注意：這裡要用 `grep -q ... && echo 1 || echo 0`，不是 `grep -qv`——後者只要有任一行
  # 不符就回 0，是恆真斷言。
  check "$(grep -q '^pack/' <<<"$entries" && echo 1 || echo 0)" \
    "壓縮檔裡沒有 staging 目錄本身"
fi

# 拒絕把輸出指到 pack/ 內（會在重建 staging 時被刪、或把 zip 自己遞迴打包）
set +e
"$FIXTURE/scripts/pack.sh" --output-dir "$FIXTURE/pack/nested" >/dev/null 2>&1
rc=$?
set -e
# 要驗特定的 exit 2，不能只驗「非零」——把防護整段拿掉之後它還是會因為別的原因失敗
# （staging 重建會把輸出目錄一起刪掉，zip 找不到路徑）。只驗非零等於沒測到防護本身。
check "$([[ "$rc" -eq 2 ]] && echo 0 || echo 1)" "--output-dir 指向 pack/ 內時明確拒絕（要 exit 2，實得 $rc）"

# 沒有 pre-rename 的 Template_Plugin 殘留：本專案的建置骨架抄自 my_skyrim_plugin_1，
# 而 runtime 子目錄名在 pack.sh 是寫死的常數，漏改就會打出錯的路徑。驗成品不驗原始碼。
if [[ -f "$ZIP" ]]; then
  check "$(grep -q '^Data/SKSE/Plugins/Template_Plugin/' <<<"$(unzip -Z1 "$ZIP")" && echo 1 || echo 0)" \
    "壓縮檔裡沒有 pre-rename 的 Template_Plugin 目錄"
fi

# CLI 契約：--help 要成功，參數錯誤要 exit 2。
set +e
"$FIXTURE/scripts/pack.sh" --help >/dev/null 2>&1; help_rc=$?
"$FIXTURE/scripts/pack.sh" --config >/dev/null 2>&1; missing_rc=$?
"$FIXTURE/scripts/pack.sh" --bogus >/dev/null 2>&1; bogus_rc=$?
set -e
check "$([[ "$help_rc" -eq 0 ]] && echo 0 || echo 1)" "--help 回報成功（要 exit 0，實得 $help_rc）"
check "$([[ "$missing_rc" -eq 2 ]] && echo 0 || echo 1)" "--config 缺值時 exit 2（實得 $missing_rc）"
check "$([[ "$bogus_rc" -eq 2 ]] && echo 0 || echo 1)" "未知參數時 exit 2（實得 $bogus_rc）"

if [[ "$failures" -ne 0 ]]; then echo "$failures failure(s)" >&2; exit 1; fi
echo "all packaging contract tests passed"
