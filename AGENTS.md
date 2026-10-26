# scene-capture-bridge — AI agent 專案備忘

scene-capture-bridge = **Skyrim SE 遊戲內採集橋 SKSE DLL（C++23，CommonLibSSE-NG）：走訪 cell 的 placed refs，輸出 `scene.json` 餵 ModForge 生成 patch esp；含 F1 面板的編輯／放置工具與離線 catalog consumer。是由人操作、隨內容交付的 authoring tool**。本檔是最頂層路由器，只指向下一層；細節不寫這裡（分層原則見 [STRUCTURE.md](wf/STRUCTURE.md)）。

## 開場與入口

- 每個 session 先跑 `grep -c '^- \[' wf/SESSION-LOG.md`：非 0 才開 [SESSION-LOG.md](wf/SESSION-LOG.md)（進度）。等使用者的事一律記母 repo [WAIT_USER.md](../../WAIT_USER.md)，本 repo 不另開一份。
- **碰原始碼前**：慣例與 code map → [workflows/common/conventions.md](wf/workflows/common/conventions.md)、[workflows/common/code-map.md](wf/workflows/common/code-map.md)；環境與指令 → [workflows/dev-env.md](wf/workflows/dev-env.md)。
<!-- wf-insert:AGENTS -->
- **要你動手做事** → [WORKFLOWS.md](wf/WORKFLOWS.md) 依意圖派發，再讀該工作流入口檔。
- **想看專案結構** → [INDEX.md](wf/INDEX.md)；人類向的完整說明 → [README.md](README.md)。
- 使用者偏好與邊界 → [workflows/common/user.md](wf/workflows/common/user.md)。

## 鐵律（always-on，隨時適用）

1. 重構 / 整理**不改原意**：開發＝行為不變且驗證綠燈（`bash scripts/test_game_detection.sh && bash scripts/test_packaging.sh`）；非開發＝原意不變，照 `Done when:` 驗收。
2. **不可逆或對外的動作**（push、刪除、對外送出、動 DB）要有**授權來源**：使用者當場確認，或他親自登記在清單裡（routines / schedule）。都沒有就先問。
3. **條列走資料檔、導航留 md**：給 AI 消化的表／清單 >1 KB 存 `.json`／`.csv`（契約 `wf-table/1`，見 [data-files](wf/workflows/common/data-files.md)），用 `wf/tools/tabledb.py` 讀寫、不整份讀進 context；給人點的導航連結留 md。
4. **具體流程**在各工作流入口檔，不在頂層。

## 專案摘要

C++23 SKSE DLL，唯一受支援的建置與出貨路徑是 Linux 上 clang-cl＋xwin＋vcpkg 交叉編譯（見 [BUILD.md](BUILD.md)）；`scene.json` 的契約權威在 ModForge spec，本 repo 只擁有 output 形狀。離線可跑的是 `scripts/` 兩支 fixture 契約測試與 `tests/` 的 portable C++ tests（見 [testing.md](wf/workflows/testing.md)）。文中 `../ModForge/…` 連結要求兩 repo 同層 clone。

<!-- wf-kernel v0.6+368468a (2026-10-26 導入) -->
