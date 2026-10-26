# dev-env — 開發環境、指令、外部工具

[WORKFLOWS](../WORKFLOWS.md)｜[INDEX](../INDEX.md)

這台機器上要能開發需要什麼、怎麼裝、跑什麼指令；外部工具設定與 env var 也收在這裡。

**何時用**：fresh clone、換機器、裝不起來、忘了指令、要加一個外部工具或環境變數。
**何時不用**：驗證 / 測試怎麼跑 → [testing](testing.md)；程式碼慣例 → [common/conventions](common/conventions.md)。

## Done when

- 照「流程（fresh clone 後）」走完，`bash scripts/test_game_detection.sh && bash scripts/test_packaging.sh` 回傳 0。
- 下面三張表沒有空欄；要使用者親自做的（帳號、授權、金鑰）在 [WAIT_USER](../../../../WAIT_USER.md) 各佔一行。

## 流程（fresh clone 後）

1. 取得原始碼（`extern/SKSEMenuFramework` 是 vendored，無 submodule）；要讀跨 repo 連結與跑 ModForge catalog 契約測試，另在同一父目錄 clone `ModForge`。
2. 備好 toolchain：`clang-cl`、`cmake`、`ninja`、xwin splat 到 `~/.xwin-cache`、`VCPKG_ROOT`。只動文件或 shell 腳本可略過。
3. 跑一次冒煙指令：`bash scripts/test_game_detection.sh && bash scripts/test_packaging.sh`（回傳 0；fixture，不需遊戲、不需 build）。

## 指令表

| 做什麼 | 指令 | 備註 |
|--------|------|------|
| build DLL | `cmake --fresh --preset build-release-clang-cl-linux && cmake --build build/release-clang-cl-linux` | 唯一受支援路徑；產出 `build/release-clang-cl-linux/SceneCaptureBridge.dll` |
| portable C++ tests | `cmake -S tests -B build/tests-native -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake && cmake --build build/tests-native && ctest --test-dir build/tests-native --output-on-failure` | 全文與 MinGW＋wine 變體見 [docs/portable-tests.md](../../docs/portable-tests.md) |
| 部署到 MO2 | `scripts/deploy.sh --dry-run`，再 `scripts/deploy.sh` | 遊戲在跑會拒絕；不要手打 `cp`；新 DLL 需完全關遊戲重開 |
| 打包 | `scripts/pack.sh` | 產出 `dist/SceneCaptureBridge-0.0.1.zip` |

驗證與測試指令不列這裡——連同「誰跑」一起在 [testing](testing.md)。

## 跨機 / 離線差異

| 環境 | 能跑 | 跑不了的 → 怎麼辦 |
|------|------|------------------|
| 本機（Linux，Manjaro） | shell 契約測試；portable tests；DLL 交叉編譯（toolchain 已備） | 遊戲內行為要實機，記 WAIT_USER |
| 無 clang-cl／xwin／vcpkg 的機器 | 只能寫碼與跑 shell 契約測試 | 不能編譯；build-verify 留給有 Linux 交叉編譯環境的機器（BUILD.md「本機狀態」） |
| Windows／MSVC | — | 已於 2026-08-07 放棄，不維護 |


## 外部工具與 env var

| 名稱 | 用途 | 怎麼取得 / 設定 |
|------|------|----------------|
| `clang-cl`／`cmake`／`ninja` | 交叉編譯 DLL 與 portable tests | 系統套件 |
| xwin | Windows CRT／SDK splat | `xwin --accept-license splat --output ~/.xwin-cache` |
| `VCPKG_ROOT` | vcpkg 根目錄（deps：`commonlibsse-ng-fork`、`nlohmann-json`、`directxtk` overlay） | 本機 `~/dev/vcpkg` |
| `dotnet`＋同層 `ModForge` | `modforge_catalog_contract` 測試的 producer 端 | 缺則該測試跳過／不建（見 `tests/CMakeLists.txt`） |

需要帳號、付費、授權才能取得的：守鐵律 2（授權來源），並在 [WAIT_USER](../../../../WAIT_USER.md) 記一行。

## 交接

- 環境就緒要開工 → [feature-dev](feature-dev/README.md)；先確認驗證跑得動 → [testing](testing.md)。
- 同一個裝機坑第二次撞到 → [common/gotchas](common/gotchas.md)。
