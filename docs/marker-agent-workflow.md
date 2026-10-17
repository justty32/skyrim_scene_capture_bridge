# 使用流程：marker → agent → 世界改變（P1，實機閉環 2026-07-10）

← [README](../README.md)（本頁 2026-10-17 自 README 拆出，內容未改寫）

玩家側：console `sc mk` 進標記模式 → **動作鍵（預設 F11）**在準星處放 marker（無命中落腳下，一鍵兩用）→ 對著 marker **按 E 開編輯視窗**（改 label/kind、寫 **note** 給 agent 的補充指示、刪除）或 **F1 → Markers** → **F1 → Export 鈕**匯出。登記簿隨存檔走（co-save）；跨存檔撿孤兒才用 `adopt this cell`。（面板的 `place marker here` 觸發鈕 2026-07-12 移除——`sc mk` 動作鍵本來就落腳下 fallback，兩者是同一顆函式的兩個呼叫點，備援已無必要）
marker 的樣子＝**鐵匕首**（`Weapons\Iron\IronDagger.nif`，houseCARL 對 WEAP 01397E 驗過）——換掉靈魂石是因為 marker 現在會**記錄＋可編輯完整朝向與大小**，匕首的**劍尖方向**剛好把朝向視覺化。有碰撞才能被 E/準星選到；weapon clutter havok 會掉 → 放置當下 `SetMotionType(kKeyframed)` 凍住。marker 匯出的 `annotations[]` 現在帶 `rotation{x,y,z}`＋`scale`（`angleZ` 仍在＝`rotation.z`，向後相容）。

**agent 對接配方**（拿到需求如「在 goat 放一隻山羊」時照做）：

1. 讀 `.../compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE/` 裡**最新一份** `scene-export_*.json`（檔名帶場景＋時間戳，2026-07-12 起不再是固定 `scene-export.json`）的 `annotations[]`——每筆有 seq/label/kind/position/rotation/scale/note/cell 或 worldspace。**NPC 一定走這條**（cell 匯出不含 actor）。
2. 查 base：houseCARL `cross_plugin_query`（如 `editorid_contains=EncGoat`）。
3. author spec：`placements[]` 帶 marker 的 position/angleZ（rotation.z）＋歸屬欄位。**⚠️ 外部 NPC base 必須 `"kind": "npc"`**——isNpc 自動判定只認 in-spec base，漏了會生成 REFR（不生怪、無報錯，dump 看到 `PlacedObject` 而非 `PlacedNpc` 即中招）。
4. `build` → `dump` 驗座標與記錄型別 → 產物放 `<MO2>/mods/<新資料夾>/`。
5. 提醒使用者：**MO2 F5 refresh 後新 mod 預設不勾**，要手動勾 mod＋plugin。

先例：`mods/SCB Goat Demo/`（本 README 同日的實機驗收產物）。
