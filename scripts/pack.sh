#!/usr/bin/env bash
# pack.sh — 把編好的 SKSE plugin DLL 打包成 MO2 可安裝的 .zip。
#
# 為什麼需要這支：`pack.ps1` 的 `[ValidateSet('release-msvc','debug-msvc')]` 結構性地
# 拒絕 `release-clang-cl-linux`，而依 BUILD.md，這台機器**唯一受支援的建置路徑**就是
# Linux clang-cl。等於在本機 `pack.ps1` 打不出任何東西。這支是它的 POSIX 對等物，
# 語意對齊（讀 build/<config>/CMakeCache.txt 取名稱與版本、stage 出
# Data/SKSE/Plugins/ 佈局、zip 進 dist/），但預設 config 換成本機真的會產出的那個。
#
# 當腳本執行出錯時立即停止
set -e

# ==============================================================================
# 設定區域 (Settings)
# ==============================================================================

# 1. 預設的 CMake 編譯配置（本機唯一受支援的建置路徑，見 BUILD.md）
CONFIG="release-clang-cl-linux"

# 2. 【標註：在此設置 .zip 的輸出位置】
# 你可以修改這裡的 "dist" 成任何你想要的路徑 (例如 "/home/user/my_mods")
OUTPUT_DIR="dist"

# 3. runtime 的設定檔子目錄名稱。必須與 CMakeLists.txt 裡的 CONFIG_FOLDER 一致
#    （那邊是 if(DEFINED OUTPUT_FOLDER) 區塊內的普通變數，不會進 CMakeCache，
#    所以這裡只能寫死；改名要兩邊一起改）。
CONFIG_FOLDER_NAME="SceneCaptureBridge"

# ==============================================================================

# 幫助訊息功能
usage() {
  echo "用法: $0 [--config <release-clang-cl-linux|release-msvc|debug-msvc>] [--output-dir <路徑>]"
  echo "說明:"
  echo "  --config      指定要打包的編譯資料夾 (預設: $CONFIG)"
  echo "  --output-dir  指定 ZIP 輸出的目的地 (預設: $OUTPUT_DIR)"
  exit "${1:-1}"
}

# 解析命令列參數
while [[ "$#" -gt 0 ]]; do
  case $1 in
  --config)
    if [[ "$#" -lt 2 || -z "$2" ]]; then
      echo "錯誤: --config 需要一個值。" >&2
      exit 2
    fi
    CONFIG="$2"
    shift
    ;;
  --output-dir)
    if [[ "$#" -lt 2 || -z "$2" ]]; then
      echo "錯誤: --output-dir 需要一個值。" >&2
      exit 2
    fi
    OUTPUT_DIR="$2"
    shift
    ;; # 也可以透過執行時傳參來臨時改變輸出位置
  -h | --help) usage 0 ;;
  *)
    echo "未知參數: $1" >&2
    usage 2
    ;;
  esac
  shift
done

# 取得專案根目錄路徑（cd 只影響本子行程，不會改到呼叫端的 cwd）
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# 定義編譯路徑與 CMake 快取檔案路徑
BUILD_DIR="build/$CONFIG"
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"

# 檢查編譯快取是否存在
if [[ ! -f "$CACHE_FILE" ]]; then
  echo "錯誤: 找不到 '$CACHE_FILE'。請先執行 'cmake --preset build-$CONFIG'。" >&2
  exit 1
fi

# 內部函式：從 CMakeCache.txt 提取數值
get_cache_value() {
  local key=$1
  # 搜尋 Key，取得 '=' 後的內容，並移除 Windows 的 \r 換行符
  local value
  value=$(grep "^$key:[^=]*=" "$CACHE_FILE" | head -n 1 | cut -d'=' -f2- | tr -d '\r')
  if [[ -z "$value" ]]; then
    echo "錯誤: 快取中找不到鍵值 '$key' ($CACHE_FILE)。" >&2
    exit 1
  fi
  echo "$value"
}

# 讀取專案名稱與版本號
PLUGIN_NAME=$(get_cache_value "CMAKE_PROJECT_NAME")
PLUGIN_VERSION=$(get_cache_value "CMAKE_PROJECT_VERSION")

# 檢查 DLL 是否已生成
DLL_PATH="$BUILD_DIR/$PLUGIN_NAME.dll"
if [[ ! -f "$DLL_PATH" ]]; then
  echo "錯誤: 找不到 DLL '$DLL_PATH'。請先編譯專案 (cmake --build $BUILD_DIR)。" >&2
  exit 1
fi

# 先解析輸出路徑；輸出若位於 pack/ 內，會在重建 staging 時被刪除或把 zip
# 自己遞迴打包，因此明確拒絕。
PACK_DIR="$REPO_ROOT/pack"
mkdir -p "$OUTPUT_DIR"
ABS_OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
case "$ABS_OUTPUT_DIR/" in
"$PACK_DIR/"*)
  echo "錯誤: --output-dir 不可指向 '$PACK_DIR' 或其子目錄。" >&2
  exit 2
  ;;
esac

# 準備打包用的暫存資料夾結構 (pack/Data/SKSE/Plugins)
PLUGINS_DIR="$PACK_DIR/Data/SKSE/Plugins"

echo "正在準備暫存檔案至 $PACK_DIR..."
rm -rf "$PACK_DIR"
mkdir -p "$PLUGINS_DIR"

# 複製編譯好的 DLL
cp "$DLL_PATH" "$PLUGINS_DIR/"

# 如果專案目錄下有 config/ 資料夾，也一併複製進 runtime 的子目錄
# （對齊 CMakeLists.txt 的 post-build copy_directory_if_different）
CONFIG_SRC="$REPO_ROOT/config"
if [[ -d "$CONFIG_SRC" ]]; then
  CONFIG_DST="$PLUGINS_DIR/$CONFIG_FOLDER_NAME"
  cp -r "$CONFIG_SRC" "$CONFIG_DST"
  echo "已包含設定檔: config/ -> Data/SKSE/Plugins/$CONFIG_FOLDER_NAME/"
fi

# 決定 ZIP 檔名 (Debug 版加上 -Debug 後綴)
BUILD_TAG=""
case "$CONFIG" in
debug-*) BUILD_TAG="-Debug" ;;
esac
ZIP_NAME="$PLUGIN_NAME-$PLUGIN_VERSION$BUILD_TAG.zip"
ZIP_PATH="$ABS_OUTPUT_DIR/$ZIP_NAME"

# 檢查環境中是否有 zip 指令
if ! command -v zip &>/dev/null; then
  echo "錯誤: 找不到 'zip' 指令。請先安裝 (例如: sudo pacman -S zip)。" >&2
  exit 1
fi

# 執行壓縮
rm -f "$ZIP_PATH"
echo "正在壓縮檔案至 $ZIP_PATH..."
(cd "$PACK_DIR" && zip -r "$ZIP_PATH" .) >/dev/null

echo ""
echo "打包成功: $ZIP_PATH"
echo "  您可以將此 .zip 檔案直接拖入 MO2 的安裝界面。"
