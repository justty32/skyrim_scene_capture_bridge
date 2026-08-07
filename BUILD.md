# Build — SceneCaptureBridge

SKSE plugin (C++23, CommonLibSSE-NG). Build architecture adapted from
[justty32/my_skyrim_plugin_1](https://github.com/justty32/my_skyrim_plugin_1)
(build stack only; plugin logic is our own).

## 先決條件

- **受支援的建置路徑只有 Linux (Manjaro)**：clang-cl + lld-link + [xwin](https://github.com/Jake-Shadle/xwin)（`xwin --accept-license splat --output ~/.xwin-cache`）+ vcpkg（`VCPKG_ROOT`）。2026-07-10 實測此路徑的產物在遊戲內載入正常（`skse64.log`：`plugin SceneCaptureBridge.dll (...) loaded correctly`；import 表只有 KERNEL32/ole32/VERSION/USER32/SHELL32，靜態 CRT 無 vcredist 相依），因此同時作為編譯驗證與實際出貨路徑。
- **Windows/MSVC CI 已於 2026-08-07 放棄**：家用與實機環境均為 Linux + Proton，不再為 Windows-only vcpkg/MSVC 相容性升級依賴或維護 workflow。
- **必要 overlay**：`ports/`（`commonlibsse-ng-fork` 的 `fix-clang-delete.patch` 是 clang-cl 編 CommonLibSSE-NG 的必要修補；`directxtk` 的 registry 版在 `x64-windows-skse-clang` 下編不過）。`CMakeLists.txt` 需 `find_package(directxtk CONFIG REQUIRED)`。
- **改過 preset / `vcpkg.json` 後必須 `rm -rf build/release-clang-cl-linux`**：stale `CMakeCache.txt` 會讓 `vcpkg.cmake` 跳過 chainload toolchain，clang-cl 就不帶 `/winsysroot`，錯誤訊息長得像「編譯器壞了」。
- deps 由 vcpkg manifest 拉：`commonlibsse-ng-fork`（Monitor221hz registry）+ `nlohmann-json`。

## Windows（MSVC，不再支援）

```powershell
cmake --preset build-release-msvc; if ($?) { cmake --build build/release-msvc }
```

產出 `build/release-msvc/SceneCaptureBridge.dll`（靜態 CRT，不依賴 vcredist）。
Debug 把 `release-msvc` 換 `debug-msvc`。改過 `vcpkg.json` / triplet → 先 `Remove-Item -Recurse -Force build` 再 configure（避免舊 CRT cache 的 LNK2038）。

## 部署到 MO2（開發迭代）

```bash
scripts/deploy.sh          # 建完之後跑這個。不要手打 cp。
```

**🔴 絕不用 `cp` 就地覆寫 `mods/.../SKSE/Plugins/*.dll`**——遊戲跑著的時候這麼做會讓它**無聲暴斃、沒有 crash log**（Linux 不像 Windows 會鎖住載入中的 DLL；`cp` 寫穿同一個 inode，而 DLL 程式碼頁是從該檔 demand-page 進來的）。`deploy.sh` 做兩件事：① `pgrep -f SkyrimSE.exe`，**遊戲在跑就拒絕**；② `cp → .tmp` 再 `mv`（`rename(2)` 換 inode，執行中的 mapping 不受影響）。成因全文見 [dev-env § 部署 SKSE DLL 到 MO2](../ModForge/workflows/dev-env.md)。

**遊戲用的是帶 esp 的 `mods/SceneCaptureBridge/`**（`SceneCaptureBridge Release/` 是備份夾）；`deploy.sh` 兩個都更新。新 DLL 要**完全關遊戲重開**才吃得到。

`CMakeLists.txt` 也有個 post-build copy（configure 時 `OUTPUT_FOLDER` 有設才啟用；本機的 preset **沒設**，所以是關的）——要用的話它只會寫 `<mods>/SceneCaptureBridge <BuildType>/`，**不會**碰遊戲在用的那個夾。

**不要丟進 MO2 的 `overwrite/`**：那是傾倒區，plugin 擺在那裡幾乎不可見、會被「清空 overwrite」沖掉。用真的 mod 資料夾，開關權留給 MO2 的勾選框。改完 DLL 後 MO2 要 F5 refresh 才看得到新資料夾。

## Manjaro（clang-cl 跨編譯，僅驗證）

```bash
cmake --preset build-release-clang-cl-linux && cmake --build build/release-clang-cl-linux
```

## 驗證 standalone

```powershell
dumpbin /dependents build\release-msvc\SceneCaptureBridge.dll
```
不該出現 `MSVCP140.dll` / `VCRUNTIME140*.dll`（出現 → 砍 `build/` 重來）。CI 自動跑此檢查。

## 打包（MO2 zip）

```powershell
scripts\pack.ps1                      # → dist\SceneCaptureBridge-0.0.1.zip
```

## 本機狀態

無 clang-cl/xwin/vcpkg 的機器**不能編譯**——只能寫碼，實際 build-verify 留給具備 Linux 交叉編譯環境的機器。
