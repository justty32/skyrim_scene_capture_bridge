# 操作模型（續）：referrer、編輯子模式、匯出

← [README](../README.md)｜前半：[操作模型](operation-model.md)
**`sc ref` ＝ referrer：命名一個既有 ref**。三兄弟並列——`removals[]` **擦掉**既有、`overrides[]` **移動**既有、`references[]` **命名**既有。referrer **什麼都不動**（不新建、不改 transform、不 disable），只記「這個 ref 的身份 ＋ 一個自由 label」，匯出成頂層 `references[]`；ModForge 把 **label 註冊成可解析的名字**，於是 spec 裡**任何 ref 欄位**都能寫它（package 的 `sandbox.location`／`travel.place`、quest alias `forced:`、`linkedRefs`、`enableParent`、objective target、script Form prop）。典型用法：指一張椅子標「sofia's chair」→ Sofia 的 sandbox package 就錨在那張椅子。

目標分兩類，**語意完全不同**：

| | **(乙) 檔內相依**（我們自己 `sc pl` 擺的） | **(甲) 外部既有 ref**（vanilla／他 mod） |
|---|---|---|
| 那是什麼 | dynamic ref，**沒有耐久 FormID** | authored ref，解得出 `<plugin>:0xLOCALID` |
| `references[].ref` 寫什麼 | 匯出時**發給該 placement 的穩定 editorId**（`MFRef_<label>_<seq>`）——**檔內**指向 `placements[]` 的那一筆 | 耐久 id 本身 |
| 為何不能寫 FormID | dynamic FormID **不可攜**，build 後對不上（這是整條路最容易做錯的地方） | — |
| persistent | build **強制** persistent（0x400 ＋ cell 的 Persistent group）——「引用需 persistent」天然滿足 | 看它自己；vanilla 場景物件多為 temporary → **build 會警告**，並提供 `anchor` 逃生門（`marker`／`replace`） |

**DLL 一律不填 `anchor`**（留白＝`none`）：persistent 逃生門要不要開、開哪種，是 ModForge／authoring agent 的判斷，不是採集端的。

**label 大小寫保留**（同 `sc capp`：走未 `Lower()` 的 raw 參數）。⚠️ console 參數以空白分隔——**含空格的 label 要加引號**（`sc ref "sofia's chair"`），或在 References 頁改名（面板不受此限）。

**拒收**：① **marker 光球**（editor chrome，本來就被 `ExportCell` 排除 → 檔內 reference 永遠解不到；而且 marker 自己就有 label/note，走 `annotations[]`）；② **我們自己生的 actor**（cell 匯出不含 actor ⇒ 沒有 placement 可指；要複製那個 NPC 走 `sc cap`）；③ **重複 label**（label 在 ModForge 是**全域名字空間**——它就是可解析的 id，撞名會讓 validate 炸整份 spec，所以面板改名/打標當場擋掉）。**vanilla NPC 的 ACHR 可以指**（它有耐久 id，走 (甲)）。

面板 **References 頁**：最新在前、label／note 就地改名、顯示 ref id（檔內目標顯示**將寫進 json 的 editorId**）／base／cell／座標、逐列刪除（**只刪登記列，世界不動**）。登記簿隨 co-save（record `'RFRR'` v1）；檔內目標的身份是 **handle**，完整重啟後 dynamic FormID 未必重解析 → 讀檔自動 `ReacquireOrphans`（按 base＋座標在當前 cell 重新綁回，同 marker 的 adopt 救援），撿不回的列**保留**但標 `TARGET LOST`、匯出時跳過（不吐一個 build 對不上的 editorId）。

**編輯純旋轉子模式**：`sc ed ax` 進入，**`sc ed` 退回**普通移動模式。ON 時 numpad 方向鍵改成旋轉——**4/6＝yaw、1/3＝pitch、7/9＝roll**（位置/縮放不動），**每組的中間鍵＝只還原自己那一軸**（2026-07-12 使用者拍板）：**2＝還原 pitch、5＝還原 yaw、8＝還原 roll**——**還原＝回到進編輯前的該軸原值，不是設成 0**（物件本來就可能有角度）。OFF（預設）時 8/2 前後、4/6 左右、1/3 升降、7/9 yaw，numpad 5＝復原到編輯前姿態（整個編輯）。

編輯模式的目標若是 **marker 光球**：numpad 微調＋0 commit＝**移動該 marker**（更新登記簿座標，不進 overrides）。`er` 切換、旋轉子模式、編輯步長全部**存進存檔**（co-save SETT v3）。

Export 頁有 **Export player cell**、**Export all (loaded cells)**、**Export captures** 與 **Export requires** 四鈕：registries（marker/擦除/override）本就全 cell 全域，`all` 只多掃**已載入的其他 cell** 的 placements（未載入 cell 的 placements 撈不到，log 會講）。Palette 頁的具名檔有三鈕：**load from file (append)**（載入的插槽**疊在列表最上面**，保留檔內順序）／**replace from file**（**清掉現有插槽**再載入；檔案不存在或無可用插槽＝什麼都不動，不會誤清）／**save to file**。**檔內順序＝面板順序**（最上面那筆排第一），所以 json 讀起來就是你在面板看到的樣子。

另有 **clear all slots**＝**清空整個 palette**。插槽是**落盤跨存檔**的（不像擦除/override 那樣是可 revert 的存檔狀態）——清掉＝丟掉別的 playthrough 攢的東西，所以**兩道防呆**：① 按下去先變成 `really clear all N slot(s)?` ＋ `yes, clear` / `cancel`，要**再按一次**才真的清；② 清完出現 **`undo clear`**（本次 session 有效，按了整批回來、並重寫回磁碟）。想留備份就先按 `save to file`。

**匯出檔名（每次一個新檔、永不覆蓋）**：

| 鈕 | 檔名 | 內容 |
|---|---|---|
| Export player cell | `scene-export_<cell EditorID 或 <ws>_x<X>y<Y>>_<YYYYMMDD-HHMM>.json` | placements（**物件，不含 NPC**）＋ removals ＋ overrides ＋ annotations ＋ **references** |
| Export all (loaded cells) | `scene-export_all-<玩家所在>_<YYYYMMDD-HHMM>.json` | 同上，掃全部已載入 cell |
| Export captures（Captures 頁也有一顆） | `captures_<YYYYMMDD-HHMM>.json` | **只有** capturedItems[] ＋ capturedNpcs[] |
| **Export requires** | `requires_<YYYYMMDD-HHMM>.txt` | **純文字報告**：這份匯出會讓生出來的 esp **依賴哪些非 vanilla mod**，逐筆講「是哪個欄位把它拉進來的」 |

同分鐘同場景再匯出＝加 `-2`/`-3` 後綴（`requires` 也一樣，`-2.txt`）。**⚠️ 下游 agent 別再寫死 `scene-export.json`**——去 SKSE 資料夾取最新一份（或使用者指名的那份）。
