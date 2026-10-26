# 操作模型（P5 模式制，2026-07-11 全數拍板）

← [README](../README.md)｜續：[referrer、編輯子模式、匯出](referrer-editor-export.md)

**一次一個模式，console 切換；每個模式一格動作鍵（面板可改綁、允許重複、預設全 F11）。** F6/F7/F8/F10 直達鍵**不存在**（使用者拍板：整個移除，非預設關）；export 只走面板 Export 鈕。

```
sc mk    打標記模式（動作鍵＝準星處放 marker）
sc del   刪除模式（動作鍵＝擦準星目標）
sc pk    滴管吸取模式（動作鍵＝吸準星目標進 palette）
sc pl    擺放模式（動作鍵＝把選中插槽擺在準星處）
sc ed    編輯模式（動作鍵＝選中準星目標進 numpad 微調）
sc ref   referrer 模式（動作鍵＝「命名」準星指的既有 ref，世界不動）
sc off   啥都不做
sc       印當前模式＋用法
sc mk dp0 / dp1            隱藏／顯示所有 marker 光球（純視覺，登記簿與匯出不受影響）
sc del|pk|ed|cap|ref er0/er1  該模式動作鍵用準星／物理射線（樹、純裝飾 static）
sc pl py1 / py0            擺出的物件保留／關閉物理（py1 預設；見下「物理開關」）
sc ed py0 / py1            控制期間凍結／保留物理（py0 預設＝現行行為）
sc pk ed0 / ed1            滴管只吸耐久 base／連實例 extra data（附魔）一起吸（ed0 預設）
sc pl ed0 / ed1            擺放只帶 base／帶上插槽的 extra data（ed0 預設）
sc ed ax                   進編輯「純旋轉子模式」（見下）；回普通模式打 sc ed
sc delc                    擦除 console 滑鼠點選的 ref（先只做物件，非 actor）
sc pkc [Label]             滴管吸 console 滑鼠點選的 ref 進 palette（aim-free；Label ＝當場改插槽名）
sc cap / sc cap r          擷取準星／射線目標的附魔＋效果（附魔武防、藥水、材料）進 capturedItems[]
sc capc [Label]            擷取 console 滑鼠點選的 ref（物品或 NPC 皆可）
sc capp [Label]            擷取「玩家自己」（臉/數值/perk/裝備全帶）→ capturedNpcs[]
sc ref <Label>             命名當前指的既有 ref（一次到位：標下＋打標籤）→ references[]
sc refc [Label]            命名 console 滑鼠點選的 ref（aim-free）
```

**物理開關（`py`）——重點是「哪一半會進 esp」**：

| | `sc pl py0/py1`（擺放） | `sc ed py0/py1`（編輯，含 `ax`） |
|---|---|---|
| 預設 | **`py1`** ＝保留完整物理 | **`py0`** ＝控制期間凍結（＝現行 P3 行為） |
| `py0` 做什麼 | ① **當下凍結**（`SetMotionType(kKeyframed)`，延後到 3D 載入才凍）② **匯出帶 `noHavokSettle`** | 選中即凍結，commit/cancel 時放開 → 物件沉降 |
| `py1` 做什麼 | 什麼都不做（物件正常受物理） | 控制期間**不凍**，havok 照跑 |

⚠️ **只有 (②) 那一半會 ship**：DLL 的 `SetMotionType` 是 runtime 狀態，隨存檔死掉、進不了 esp。真正讓「蓋好的房間裡杯子不亂飛」的是 **`PlacementSpec.noHavokSettle` → REFR 記錄旗標 `DontHavokSettle`（0x20000000）**——它叫引擎**跳過 cell 載入時的 havok settle pass**（那一下就是把手擺的杯子彈飛的元兇，物件跟桌面稍微交疊時尤其慘）。**這是 Bethesda 自己的做法，不是偏方**：Skyrim.esm 的 693,333 個 REFR 裡 **3,791 個**帶這個旗標，型別分佈正是雜物（MoveableStatic 995／MiscItem 724／Activator 564／Weapon 321／**Static 247**／藥水 245／Armor 159／Book 87…）。因為連 STAT 都在名單上，**匯出旗標不按型別過濾**（runtime 凍結才過濾——keyframe 一個 STAT 沒有意義）。ACHR 不寫（actor 無此語意）。

**Extra data 開關（`ed`）**：現況 `sc pk` 只取 `GetBaseObject()`，所以**玩家自己附魔的劍吸進來是一把白鐵劍**——附魔活在 **ref 的 `ExtraEnchantment`**，不在 base 上。

- **`sc pk ed1`** ＝吸取時連實例附魔一起記進插槽（durable ENCH → 直接引用；玩家自製的 runtime ENCH → 記下 MGEF effects 待鑄造）。
- **`sc pl ed1`** ＝該插槽擺出去時，**匯出走「鑄造＋引用」**：同一份 scene 檔裡吐一筆 `capturedItems[]`（`editorId: MFPal_<插槽名>_<seq>`，`base` ＝實體模板、`enchantment` ＝吸到的附魔），而該 placement 的 **`base` 指那個 editorId**——**檔內相依**，跟 referrer 的 `references[]` 同一招（所以**必須同一個檔**：capturedItems 落到另一份 json，build 會解不到 base 而丟掉 placement）。ModForge 端 `ExpandCapturedItems` 把它展成 WEAP/ARMO ＋ 一顆新 ENCH，**C# 零改動**。
- 擺放當下若附魔是 **durable ENCH**，世界裡那顆也會真的帶上（`ExtraEnchantment`）；**runtime ENCH 不套**（它是存檔綁定的 form，而 palette 插槽是**落盤跨存檔**的，快取那個指標＝懸空指標）——匯出照樣鑄造，ship 出去的東西不受影響。

**`sc capp` ＝直接吸玩家**：引擎把玩家的 chargen 寫在 base TESNPC（`Skyrim.esm:0x000007`）上，DLL 直讀同一處即可，**不必經 PROTEUS clone**（clone 自報 level 1／50-50-50、不寫 tintLayers、outfit 是空殼）。順帶所有 actor 的擷取都改帶**顯式數值**：H/M/S ＋ 18 技能（引擎 AV 6..23，＝Mutagen `Skill` 序）→ ModForge 直接寫 DNAM、**不開 autoCalcStats**（autocalc 只是拿 class+level 估算，載入時還會覆蓋掉）。玩家 perk 讀 `PlayerCharacter::addedPerks`（玩家 base 的 perk array 是空的）。

**`[Label]` ＝身份標籤，大小寫保留**（`sc` 的參數解析會全轉小寫——label 走未 `Lower()` 的 raw 參數）。匯出成 `editorId: "MFCap_<label>"`，ModForge 的「顯式 editorId 優先」規則讓同一個 label 永遠對應同一筆記錄（再吸一次＝更新同一個人，不會多生一個）。計畫全文：[plans/player-capture-capp.md](../../ModForge/workflows/plans/player-capture-capp.md)。
