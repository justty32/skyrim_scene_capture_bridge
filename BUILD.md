# Build — SceneCaptureBridge

SKSE plugin (C++23, CommonLibSSE-NG). Build architecture adapted from
[justty32/my_skyrim_plugin_1](https://github.com/justty32/my_skyrim_plugin_1)
(build stack only; plugin logic is our own).

## 先決條件

- **受支援的建置路徑只有 Linux (Manjaro)**：clang-cl + lld-link + [xwin](https://github.com/Jake-Shadle/xwin)（`xwin --accept-license splat --output ~/.xwin-cache`）+ vcpkg（`VCPKG_ROOT`）。此路徑同時用於編譯驗證與實際出貨；產物須在 `skse64.log` 顯示 `plugin SceneCaptureBridge.dll (...) loaded correctly`，並通過下方 standalone 檢查。
- **不維護 Windows/MSVC CI**：家用與實機環境均為 Linux + Proton，不再為 Windows-only vcpkg/MSVC 相容性升級依賴或維護 workflow。
- **必要 overlay**：`ports/`（`commonlibsse-ng-fork` 的 `fix-clang-delete.patch` 是 clang-cl 編 CommonLibSSE-NG 的必要修補；`directxtk` 的 registry 版在 `x64-windows-skse-clang` 下編不過）。`CMakeLists.txt` 需 `find_package(directxtk CONFIG REQUIRED)`。
- **改過 preset / `vcpkg.json` 後必須 `rm -rf build/release-clang-cl-linux`**：stale `CMakeCache.txt` 會讓 `vcpkg.cmake` 跳過 chainload toolchain，clang-cl 就不帶 `/winsysroot`，錯誤訊息長得像「編譯器壞了」。
- deps 由 vcpkg manifest 拉：`commonlibsse-ng-fork`（Monitor221hz registry）+ `nlohmann-json`。

## Windows（MSVC，不再支援）

Windows/MSVC 不屬於受支援的 DLL 建置與出貨路徑；請用下方 Manjaro 指令。

## Portable tests（MinGW／Linux，非 DLL build）

只驗不依賴 CommonLib/SKSE 的離線 catalog parser 與最愛邏輯；Windows MinGW 與 Linux（原生／MinGW+wine）指令見 [docs/portable-tests.md](docs/portable-tests.md)。

## 安裝離線 catalog

用 ModForge `catalog export-json <db> <out.json>` 產生檔案，命名為
`scene-catalog.json`，放在 Skyrim 的 SKSE 資料目錄（與 bridge 的
`scene-capture-palette.json`、`SceneCaptureBridge.ini` 及匯出檔同層）。bridge 只在
`kDataLoaded` 載入一次；替換後需完整重啟遊戲。Browser 會顯示載入或拒絕原因。
`catalog build` 的 plugin 參數必須包含引擎最後載入的**完整 plugin 集合**（依
low→high 順序），不是只列想搜尋的幾個 mod；少一個或多一個，bridge 都會拒絕
metadata，避免用不完整 override winner 污染顯示。順序另需與 engine
`TESDataHandler::files` 過濾出的 loaded global sequence 精確相符；它保留 full/light
交錯順序，不使用兩個 compiled array 拼接。MO2 VFS 內檔案的 SHA-256 尚未 runtime
核對；UI 會保留限制提示。

`catalog_compatibility_probe` 是需要真實 catalog 與 load order 的手動診斷工具，因此不註冊為 CTest：
```bash
build/tests-native/catalog_compatibility_probe /path/to/scene-catalog.json /path/to/plugins.txt
```
第二個參數可直接使用 MO2 `plugins.txt`（支援 CRLF、`#` 註解行與開頭的 `*` 啟用前綴）。

## 部署到 MO2（開發迭代）

部署守門、DLL 替換與 MO2 目標目錄見 [docs/deployment.md](docs/deployment.md)。

## Manjaro（clang-cl 跨編譯，唯一支援的建置與出貨路徑）

```bash
cmake --fresh --preset build-release-clang-cl-linux && cmake --build build/release-clang-cl-linux
```

`--fresh` 不可省略：preset 以裸名 `clang-cl` 指定編譯器，第二次 configure 時 cache 中的絕對路徑與裸名不等，CMake 會中途重跑 configure 並失去 xwin toolchain 的 include／libpath，報 `Check for working CXX compiler: /usr/bin/clang-cl - broken`。

## 驗證 standalone

```bash
llvm-readobj --coff-imports build/release-clang-cl-linux/SceneCaptureBridge.dll
```
不該出現 `MSVCP140.dll` / `VCRUNTIME140*.dll`（出現 → 砍 `build/` 重來）。已驗證產物的 import 表只有 `KERNEL32.dll`／`ole32.dll`／`SHELL32.dll`／`USER32.dll`／`VERSION.dll`，`MSVCP`／`VCRUNTIME` 命中數為 0。

## 打包（MO2 zip）

```bash
scripts/pack.sh                       # → dist/SceneCaptureBridge-0.0.1.zip
scripts/test_packaging.sh             # 打包契約測試（fixture，不需要真的 build）
scripts/test_game_detection.sh        # 部署守門的偵測契約測試（fixture，不需要遊戲）
```

`pack.sh` 預設 `--config release-clang-cl-linux`，也就是本機唯一會產出的那個。
**`pack.ps1` 在本機打不出東西**——它的 `[ValidateSet('release-msvc','debug-msvc')]`
不接受 clang-cl 的 config；留著只為 Windows 主機，本機一律用 `.sh` 那支。

## 本機狀態

無 clang-cl/xwin/vcpkg 的機器**不能編譯**——只能寫碼，實際 build-verify 留給具備 Linux 交叉編譯環境的機器。

## 建置踩坑

- **`ports/` overlay 必須存在**。`CMakePresets.json` 的 `vcpkg-clang-linux` 指向 `${sourceDir}/ports`；`commonlibsse-ng-fork/fix-clang-delete.patch` 是 clang-cl 編 CommonLibSSE-NG 的**必要**修補，`directxtk` 也得走 overlay（registry 版在 `x64-windows-skse-clang` 下編不過）。從 `my_skyrim_plugin_1/ports/` 整包搬。
- `CMakeLists.txt` 需 `find_package(directxtk CONFIG REQUIRED)`——CommonLibSSE 的 export target 在 link interface 裡具名 `Microsoft::DirectXTK`。
- **改過 preset / vcpkg.json 後必須 `rm -rf build/release-clang-cl-linux`**。stale `CMakeCache.txt` 會讓 vcpkg.cmake 跳過 chainload toolchain，clang-cl 就不帶 `/winsysroot`，錯誤訊息長得像「編譯器壞了」。
- `ForEachReference` 的 callback 收 `TESObjectREFR*`（指標），不收 reference。
