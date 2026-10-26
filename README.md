# scene-capture-bridge — 遊戲內採集橋 SKSE DLL（Idea #24 元件③）

← 契約權威：[ingame-scene-export-design.md](../ModForge/workflows/specs/ingame-scene-export-design.md)｜idea：[#24 遊戲內編輯器](../ModForge/workflows/idea/tools/24-ingame-editor.md)

> **獨立 repo**。文中 `../ModForge/…` 這類連結，前提是兩個 repo **同層 clone 在同一個父目錄下**（本機為 `~/repo/moddings/skyrim/projects/`）。

**唯一 net-new 的重工程**：一支 SKSE C++ DLL，在遊戲內走訪目標 cell 的 placed refs、讀每個 base + world transform + enable state、把 runtime FormID 反解成耐久 `<plugin>:0xLOCALID`，序列化成 **scene.json** → 餵 ModForge（`dotnet run -- build scene.json`）生成 patch esp。

<!-- wf-nav -->
- **類型**：基石聯動（它的 output 契約 = ModForge 的 input；兩者靠 scene.json 協議接，不整合）
- **契約權威**：scene.json 的每個欄位對映**既有 ModForge spec 型別**，本 repo **只擁有 output 形狀**，生成端全在 ModForge。契約定義見 [spec §契約](../ModForge/workflows/specs/ingame-scene-export-design.md)。
- **建置**：[BUILD.md](BUILD.md)（C++23 + CommonLibSSE-NG + vcpkg + CMake presets；靜態 CRT standalone DLL）
- **離線 catalog consumer（已完成；portable suite 3/3 PASS，2026-10-17 含 `favorites_tests`）**：把 ModForge `scene-catalog.json` v1 放到 SKSE 資料目錄（與 `scene-capture-palette.json`、匯出檔同層），下次啟動時 Browser 會按 durable FormKey 補 EditorID 與 runtime 缺少的名稱，納入搜尋/顯示。`Catalog.cpp` 已接上載入、compatibility gate 與 metadata merge，`CatalogFile.cpp` 是不依賴 SKSE 的 portable parser；`catalog_file_tests` 與 `modforge_catalog_contract` 均通過。catalog 必須由**完整 resolved load order** 建立：來源集合與 engine `TESDataHandler::files` 過濾出的 loaded global sequence（full + light）需完全一致，且 provenance/record cross-reference 合法；否則整份忽略並安全退回 runtime-only。離線資料只是 metadata，不會增加遊戲未載入的 form，也不取代 runtime name/model。SHA-256 仍未 runtime 核對，Browser 會明示。
- **狀態**：✅ **P1–P6 核心功能已存在**；部分 post-P6／ghost 回歸仍 open，不能視為全部實機驗收完畢。面板：F1 → `Scene Capture Bridge`。驗收明細見 [landed/world.md](../ModForge/workflows/feature-dev/landed/world.md)；未驗項以本 repo 的 [ghost spin 調查](docs/investigations/GHOST_SPIN_INVESTIGATION_2026-08-25.md)、[ghost cell clear 調查](docs/investigations/GHOST_CELL_CLEAR_INVESTIGATION_2026-08-26.md)與[下方待實機清單](#尚無實機驗收記錄2026-09-05)為準。ModForge 側的 `wait_todo` 尚未同步，待該 repo 的線補。

## 建置架構來源

改編自 [justty32/my_skyrim_plugin_1](https://github.com/justty32/my_skyrim_plugin_1) 的**建置骨架**（CMake/vcpkg/presets/CI/triplet overlay/靜態 CRT/clang-cl 跨編譯），**只借建置架構，plugin 邏輯全自寫**（依契約寫，不照抄內部程式碼）。關鍵沿用：
- `commonlibsse-ng-fork`（Monitor221hz vcpkg registry）+ **`nlohmann-json`**（scene.json 序列化）。
- `build-release-clang-cl-linux` preset → **唯一受支援的建置與出貨路徑**（Manjaro clang-cl + xwin，產物已實機驗證）。不維護 Windows/MSVC CI。

## 文件地圖

<!-- wf-nav -->
| 主題 | 頁面 |
|---|---|
| 操作模型（模式、`sc` 指令、物理／extra data 開關、`sc capp`） | [docs/operation-model.md](docs/operation-model.md) |
| referrer、編輯純旋轉子模式、Export 鈕與匯出檔名 | [docs/referrer-editor-export.md](docs/referrer-editor-export.md) |
| Export requires（esp 依賴報告） | [docs/export-requires.md](docs/export-requires.md) |
| 改鍵（`SceneCaptureBridge.ini`） | [docs/keybindings-ini.md](docs/keybindings-ini.md) |
| `src/` 模組表 | [docs/source-map.md](docs/source-map.md)、[docs/source-map-tools.md](docs/source-map-tools.md) |
| 使用流程：marker → agent → 世界改變 | [docs/marker-agent-workflow.md](docs/marker-agent-workflow.md) |
| 持久化與 adopt 語意 | [docs/persistence.md](docs/persistence.md) |
| 最愛（A3）規格 | [docs/favorites.md](docs/favorites.md) |
| ghost 調查（`docs/investigations/`） | [spin 2026-08-25](docs/investigations/GHOST_SPIN_INVESTIGATION_2026-08-25.md)、[cell clear 2026-08-26](docs/investigations/GHOST_CELL_CLEAR_INVESTIGATION_2026-08-26.md) |
| 建置、測試、部署、建置踩坑 | [BUILD.md](BUILD.md)、[docs/deployment.md](docs/deployment.md)、[docs/portable-tests.md](docs/portable-tests.md) |

## 狀態

<a id="尚無實機驗收記錄2026-09-05"></a>

> **尚無實機驗收記錄（2026-09-05）**：`75308c9` 讓 `sc pl py0` 的 frozen palette placement 經 editor commit／cancel 後仍保持 frozen；`a17e460` 讓 ghost 與 gh0 direct placement 改走最終 rendered `NiCamera` ray，對齊第一／第三人稱與 SmoothCam，並略過玩家；`5273576` 讓 movable preview ghost 的 deferred freeze 重試到 motion change 真正成功，且 placement ray 略過 live／sentinel ghost，避免 follow self-hit；`21867c1` 在玩家跨 cell 時重建 preview。2026-09-05 的 `7a95a14` 修正 ghost commit 丟失 slot extra／actor 身分、`51e442d` 修正 Browser 借用的 `gh1` 洩漏、`61d969d` 修正 physics release 誤報成功、`bb13695` 修正 palette 刪除後選擇錯位、`a8512f0` 修正 editor cancel 未更新縮放、`f5efc7f` 修正失敗 slot 留著舊 ghost、`e6e927b` 修正失敗 slot 恢復後 ghost 不回來、`0e1d6aa` 與 `4c15f0c` 補齊 Eraser／marker dynamic ref 的刪除標記。以上皆已提交，但都**尚未有 Skyrim 實機 PASS 記錄**。

## 尚未做（依 spec 里程碑）

- **A 線仍有兩項工作**：A1 是唯一的**未知技術 spike**（`Inventory3DManager` 面板內真 3D 預覽）；A2「施法即編輯」尚無 `TESSpellCastEvent` 實體。A3 favorite UI 已於 `1ac59df`（2026-09-18）實作：Browser＋Palette 共用 `scene-capture-favorites.json`、`[*]` 切換、最愛置頂與 favorites only，規格見 [docs/favorites.md](docs/favorites.md)，`favorites_tests` 離線通過；**尚無實機驗收記錄**。
- **PROTEUS clone 的 ref 是 dynamic**：`npcRoles[].actorRef` 需要耐久 ref id，dynamic ref 沒有。PROTEUS 已降為**可選**（預設走 ModForge 直接生的「大眾臉」NPC，ref 耐久），故不阻塞；見 [spec](../ModForge/workflows/specs/ingame-scene-export-design.md)「NPC 來源」。
- **§B 語意標記 / §D role tag / §E 滴管·範圍吸取·橡皮擦**：UI 骨架（`src/UI.cpp`）已接上 SKSE Menu Framework，剩下的是把這些工具畫進面板。

## 里程碑對位（spec §最小垂直切片）

本 repo負責 **M4（採集橋 spike）→ M6**。M0–M2（ModForge 側 `SceneImport` + `SceneNpcRoleSpec`，手寫 scene.json 即可驗）是 ModForge 本命工作、離線可測，**不依賴本 DLL**——兩線並行。

## 第三方依賴

- [extern/SKSEMenuFramework/README.md](extern/SKSEMenuFramework/README.md)：vendored 的 SKSE Menu Framework（上游文件，不在本 repo 維護）。
