# 持久化與 adopt 語意（P5 co-save 後全面升級）

← [README](../README.md)（本頁 2026-10-17 自 README 拆出，內容未改寫）

DLL 有兩層狀態，**P5 起兩層都隨存檔走**：

1. **存檔（savegame）**：所有實際操作本來就持久——擺的動態 ref（`0xFF......`）連同 transform、擦除的 `Disable()` 狀態、marker proxy 連同顯示名。2026-07-11 實機確認。
2. **登記簿 → SKSE co-save（`CoSave.cpp`）**：Markers（**含 note**）/Eraser/Overrides 三本登記簿＋設定（模式/鍵位/dp 狀態）以 UID `'SCBR'` 掛在**每個存檔**旁，讀檔自動跟回來——**關遊戲重開不再歸零**。讀一個沒有我們記錄的存檔＝乾淨預設（revert 先清）。

匯出的 vanilla diff 判別依舊無狀態（ref 解得出來源檔 ⇒ authored 跳過；解不出 ⇒ 玩家放的）。co-save 後的持久化對照：

| 東西 | 誰記得 | 重開遊戲後匯出 |
|---|---|---|
| 新增物件（擺的、丟在地上的裝備） | 存檔（動態 ref） | **自動**——身份證在 ref 自己身上，從來不需要登記 |
| 真刪除的自家物件 | 存檔（disabled 動態 ref） | 自動跳過（無痕） |
| marker（位置/label/kind/**note**） | **co-save 登記簿**＋存檔裡的 proxy | **自動**——proxy 是動態 ref、FormID 過完整重啟未必重解析，故讀檔時 co-save 認不回的那筆改**自動 adopt**（`kPostLoadGame` 掃當前 cell），並用**座標配對**把 co-save 的 note/kind 貼回撿到的光球；別的 cell 走過去仍靠 `adopt this cell` |
| 擦除 vanilla/mod 物件 | **co-save 登記簿**＋存檔 disable 狀態 | **自動**進 `removals[]` |
| 移動 authored ref（overrides） | **co-save 登記簿**（baseline＋commit pose）＋存檔 live pose | **自動**進 `overrides[]`，revert 也還能回 baseline |
| Palette 插槽 | **磁碟**（`scene-capture-palette.json`） | 天生跨存檔；plugin 移出 load order 的槽標 unavailable；面板 `clear all slots` 清空（二次確認＋ `undo clear`） |
| Captures 擷取定義（物品附魔/效果、NPC 外貌/數值、玩家） | **co-save 登記簿**（record `'SCCP'` v8，純耐久 id） | **自動**進 `capturedItems[]`／`capturedNpcs[]`；無 handle，讀檔即回 |
| referrer（命名既有 ref） | **co-save 登記簿**（record `'RFRR'` v1） | **外部目標**＝耐久 id，自動進 `references[]`；**檔內目標**（我們自己擺的）身份是 dynamic handle → 讀檔 `ReacquireOrphans` 按 base+座標撿回；撿不回就標 `TARGET LOST` 並**跳過該筆**（不吐 build 對不上的 editorId） |
| **`sc pl py0/ed1` 擺出的物件**（旗標／附魔） | **co-save 登記簿**（record `'PLEX'` v1） | 匯出自動帶 `noHavokSettle` ／ 鑄造的 `capturedItems[]` ＋ placement 的 `base` 指它；handle 跨重啟死掉 → 匯出掃 cell 時按 **base＋座標**就地撿回 |
| 模式/dp 狀態 ＋ **物理/extra-data 開關** | **co-save**（SETT **v7**） | 隨存檔還原（v≤5 舊存檔＝預設：place py1、edit py0、extra data 全關） |
| **動作鍵** | **`SceneCaptureBridge.ini`**（磁碟，SKSE 資料夾）＋ co-save（SETT v7，仍照寫照讀） | **ini 贏**：ini 有寫的模式一律吃 ini，存檔裡的值只進 log；ini 沒寫的才吃存檔（再沒有就 F11）。ini 天生跨存檔跨 session |

**adopt 降級為救援機制**：marker 的 `adopt this cell` 現在讀檔會**自動跑一次**（掃當前 cell），只有跨到別的 cell 才需手動按。擦除的 `scan disabled refs` 已整個移除——co-save 存的是耐久 id，重解析穩定，跨存檔救援冗餘。真要**換一個存檔**撿另一條時間線的 marker，走 Markers 頁 `adopt this cell`。

**2026-07-11 實機驗收**：F11 準星放置（pitch 正確）、F8 擦除/undo、F6/F7 滴管（含姿態）、numpad 編輯（5 選/3 升/0 commit/. 取消還原；編輯中 log 出現的 unmapped `0x11`/`0x1F`/`0x20`/`0x38` 是 WASD/Alt，非 numpad 問題）、物理凍結→commit→沉降（匯出為沉降後姿態）、F10 匯出→ModForge build→esp 閉環（removals 深埋 Z-30000、Tamriel override 自動帶 TopCell、ESL）。
