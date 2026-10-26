# INDEX — scene-capture-bridge 專案地圖

scene-capture-bridge = **Skyrim SE 遊戲內採集橋 SKSE DLL（C++23，CommonLibSSE-NG）：走訪 cell 的 placed refs，輸出 `scene.json` 餵 ModForge 生成 patch esp；含 F1 面板的編輯／放置工具與離線 catalog consumer。是由人操作、隨內容交付的 authoring tool**。本檔只描述**頂層**：每列一句話＋連結；目錄內部複雜就放它自己的 README / INDEX。

## Repo 佈局

| 路徑 | 內容 |
|------|------|
| `src/` | DLL 原始碼；模組表見 [docs/source-map.md](../docs/source-map.md)、[docs/source-map-tools.md](../docs/source-map-tools.md) |
| `tests/` | 不依賴 CommonLib／SKSE 的 portable C++ tests（catalog parser、最愛、ModForge catalog 契約） |
| `scripts/` | `deploy.sh`（守門部署）、`pack.sh`（MO2 zip）、`check_game_running.sh` 與兩支契約測試 |
| `docs/` | 操作模型、持久化、最愛、ghost 調查等分頁；導航見 [README](../README.md)「文件地圖」 |
| `cmake/、ports/、extern/、pack/` | toolchain、vcpkg overlay、vendored SKSE Menu Framework、zip 打包素材 |
| `BUILD.md、README.md` | 建置／部署／打包；人類向概覽 |
| `workflows/` | 工作流（派發見 [WORKFLOWS.md](WORKFLOWS.md)；共享區 [workflows/common/](workflows/common/README.md)）|
| `.claude/commands/` | slash 指令適配層（可選）。Claude Code 為例：只讀專案根的這層，非侵入式佈局也留在根；沒有 slash 機制的工具忽略本目錄，直接跑 `tools/wf-lint.sh` |
| `tools/` | kernel 工具：`wf-lint.sh`（檢查）、`tabledb.py`（資料檔 CRUD／連結）、`find_big_lists.py`、`fix_moved_links.py`；非侵入式佈局在 `wf/tools/`；資料檔契約見 [workflows/common/data-files.md](workflows/common/data-files.md) |
<!-- wf-insert:INDEX -->

## 頂層文件

| 檔案 | 角色 |
|------|------|
| [WORKFLOWS.md](WORKFLOWS.md) | 派發器：意圖 → 工作流入口 |
| [STRUCTURE.md](STRUCTURE.md) | 結構整理參考（被動）：分層、膨脹即拆、四級成長、archive、工作流形式 |
| [SESSION-LOG.md](SESSION-LOG.md) | 我的 open 進度 |
| [WAIT_USER.md](../../../WAIT_USER.md) | 母 repo 的等使用者清單（本 repo 不另開）|
