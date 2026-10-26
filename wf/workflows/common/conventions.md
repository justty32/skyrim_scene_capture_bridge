# conventions — 程式碼慣例（碰原始碼的工作流共用）

[common/README](README.md)｜[INDEX](../../INDEX.md)

碰原始碼的工作流（feature-dev / refactor / investigation）共用這套規矩：**寫碼時要遵守什麼**。哪個檔負責什麼領域、測試在哪 → [code-map](code-map.md)；真相層優先序也在 [code-map](code-map.md)。結構整理原則 → [STRUCTURE](../../STRUCTURE.md)。


## 慣例

| 項目 | 規矩 |
|------|------|
| 契約權威 | scene.json 欄位對映 ModForge 既有 spec 型別，本 repo 只擁有 output 形狀；改欄位先對照 `../ModForge/workflows/specs/ingame-scene-export-design.md` |
| portable 分離 | `CatalogFile.*` 不得 include CommonLib／SKSE 標頭（要能在 `tests/` 獨立編）：`grep -n 'RE/\|SKSE' src/CatalogFile.*` 應無輸出 |
| 部署 | 不用 `cp` 就地覆寫 `mods/.../SKSE/Plugins/*.dll`（遊戲在跑會無聲暴斃）；一律 `scripts/deploy.sh` |
| 建置 | 改過 preset／`vcpkg.json` 後 `rm -rf build/release-clang-cl-linux`；configure 用 `cmake --fresh` |
| overlay | `ports/` overlay 必須存在（`fix-clang-delete.patch`、`directxtk`） |
| catalog | catalog 必須由完整 resolved load order 建立，來源集合與 engine loaded sequence 不符就整份忽略（安全退回 runtime-only） |
