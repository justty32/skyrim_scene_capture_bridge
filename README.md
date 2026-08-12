# scene-capture-bridge — 遊戲內採集橋 SKSE DLL（Idea #24 元件③）

← 契約權威：[ingame-scene-export-design.md](../ModForge/workflows/specs/ingame-scene-export-design.md)｜idea：[#24 遊戲內編輯器](../ModForge/workflows/idea/tools/24-ingame-editor.md)

> **獨立 repo**（2026-08-02 自 ModForge `sub_projs/scene-capture-bridge` 抽出，未帶舊 commit 歷史）。文中 `../ModForge/…` 這類連結，前提是兩個 repo **同層 clone 在同一個父目錄下**（本機為 `~/repo/moddings/skyrim/projects/`）。

**唯一 net-new 的重工程**：一支 SKSE C++ DLL，在遊戲內走訪目標 cell 的 placed refs、讀每個 base + world transform + enable state、把 runtime FormID 反解成耐久 `<plugin>:0xLOCALID`，序列化成 **scene.json** → 餵 ModForge（`dotnet run -- build scene.json`）生成 patch esp。

- **類型**：基石聯動（它的 output 契約 = ModForge 的 input；兩者靠 scene.json 協議接，不整合）
- **契約權威**：scene.json 的每個欄位對映**既有 ModForge spec 型別**，本 repo **只擁有 output 形狀**，生成端全在 ModForge。契約定義見 [spec §契約](../ModForge/workflows/specs/ingame-scene-export-design.md)。
- **建置**：[BUILD.md](BUILD.md)（C++23 + CommonLibSSE-NG + vcpkg + CMake presets；靜態 CRT standalone DLL）
- **離線 catalog consumer**：把 ModForge `scene-catalog.json` v1 放到 SKSE 資料目錄（與 `scene-capture-palette.json`、匯出檔同層），下次啟動時 Browser 會按 durable FormKey 補 EditorID 與 runtime 缺少的名稱，納入搜尋/顯示。catalog 必須由**完整 resolved load order** 建立：來源集合與 engine `TESDataHandler::files` 過濾出的 loaded global sequence（full + light）需完全一致，且 provenance/record cross-reference 合法；否則整份忽略並安全退回 runtime-only。離線資料只是 metadata，不會增加遊戲未載入的 form，也不取代 runtime name/model。SHA-256 仍未 runtime 核對，Browser 會明示。
- **狀態**：✅ **P1–P3 主線實機全過**（2026-07-11；前情 M4 spike＋P1 marker 閉環 2026-07-10）；**P5 模式制已實作待實機**（同日）——`sc` console 指令＋每模式鍵位＋SKSE co-save，**F 直達鍵全數移除**。面板：F1 → `Scene Capture Bridge`。驗收明細見 [landed/world.md](../ModForge/workflows/feature-dev/landed/world.md)；殘項見 [wait_todo/ingame-tests.md](../ModForge/wait_todo/ingame-tests.md)。

## 建置架構來源

改編自 [justty32/my_skyrim_plugin_1](https://github.com/justty32/my_skyrim_plugin_1) 的**建置骨架**（CMake/vcpkg/presets/CI/triplet overlay/靜態 CRT/clang-cl 跨編譯），**只借建置架構，plugin 邏輯全自寫**（依契約寫，不照抄內部程式碼）。關鍵沿用：
- `commonlibsse-ng-fork`（Monitor221hz vcpkg registry）+ **`nlohmann-json`**（scene.json 序列化）。
- `build-release-clang-cl-linux` preset → **唯一受支援的建置與出貨路徑**（Manjaro clang-cl + xwin，產物已實機驗證）。Windows/MSVC CI 於 2026-08-07 放棄。

## 操作模型（P5 模式制，2026-07-11 全數拍板）

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

**物理開關（`py`，2026-07-12）——重點是「哪一半會進 esp」**：

| | `sc pl py0/py1`（擺放） | `sc ed py0/py1`（編輯，含 `ax`） |
|---|---|---|
| 預設 | **`py1`** ＝保留完整物理 | **`py0`** ＝控制期間凍結（＝現行 P3 行為） |
| `py0` 做什麼 | ① **當下凍結**（`SetMotionType(kKeyframed)`，延後到 3D 載入才凍）② **匯出帶 `noHavokSettle`** | 選中即凍結，commit/cancel 時放開 → 物件沉降 |
| `py1` 做什麼 | 什麼都不做（物件正常受物理） | 控制期間**不凍**，havok 照跑 |

⚠️ **只有 (②) 那一半會 ship**：DLL 的 `SetMotionType` 是 runtime 狀態，隨存檔死掉、進不了 esp。真正讓「蓋好的房間裡杯子不亂飛」的是 **`PlacementSpec.noHavokSettle` → REFR 記錄旗標 `DontHavokSettle`（0x20000000）**——它叫引擎**跳過 cell 載入時的 havok settle pass**（那一下就是把手擺的杯子彈飛的元兇，物件跟桌面稍微交疊時尤其慘）。**這是 Bethesda 自己的做法，不是偏方**：Skyrim.esm 的 693,333 個 REFR 裡 **3,791 個**帶這個旗標，型別分佈正是雜物（MoveableStatic 995／MiscItem 724／Activator 564／Weapon 321／**Static 247**／藥水 245／Armor 159／Book 87…）。因為連 STAT 都在名單上，**匯出旗標不按型別過濾**（runtime 凍結才過濾——keyframe 一個 STAT 沒有意義）。ACHR 不寫（actor 無此語意）。

**Extra data 開關（`ed`，2026-07-12）**：現況 `sc pk` 只取 `GetBaseObject()`，所以**玩家自己附魔的劍吸進來是一把白鐵劍**——附魔活在 **ref 的 `ExtraEnchantment`**，不在 base 上。

- **`sc pk ed1`** ＝吸取時連實例附魔一起記進插槽（durable ENCH → 直接引用；玩家自製的 runtime ENCH → 記下 MGEF effects 待鑄造）。
- **`sc pl ed1`** ＝該插槽擺出去時，**匯出走「鑄造＋引用」**：同一份 scene 檔裡吐一筆 `capturedItems[]`（`editorId: MFPal_<插槽名>_<seq>`，`base` ＝實體模板、`enchantment` ＝吸到的附魔），而該 placement 的 **`base` 指那個 editorId**——**檔內相依**，跟 referrer 的 `references[]` 同一招（所以**必須同一個檔**：capturedItems 落到另一份 json，build 會解不到 base 而丟掉 placement）。ModForge 端 `ExpandCapturedItems` 把它展成 WEAP/ARMO ＋ 一顆新 ENCH，**C# 零改動**。
- 擺放當下若附魔是 **durable ENCH**，世界裡那顆也會真的帶上（`ExtraEnchantment`）；**runtime ENCH 不套**（它是存檔綁定的 form，而 palette 插槽是**落盤跨存檔**的，快取那個指標＝懸空指標）——匯出照樣鑄造，ship 出去的東西不受影響。

**`sc capp` ＝直接吸玩家（2026-07-12，去 PROTEUS 化）**：引擎把玩家的 chargen 寫在 base TESNPC（`Skyrim.esm:0x000007`）上，DLL 直讀同一處即可，**不必經 PROTEUS clone**（clone 自報 level 1／50-50-50、不寫 tintLayers、outfit 是空殼）。順帶所有 actor 的擷取都改帶**顯式數值**：H/M/S ＋ 18 技能（引擎 AV 6..23，＝Mutagen `Skill` 序）→ ModForge 直接寫 DNAM、**不開 autoCalcStats**（autocalc 只是拿 class+level 估算，載入時還會覆蓋掉）。玩家 perk 讀 `PlayerCharacter::addedPerks`（玩家 base 的 perk array 是空的）。

**`[Label]` ＝身份標籤，大小寫保留**（`sc` 的參數解析會全轉小寫——label 走未 `Lower()` 的 raw 參數）。匯出成 `editorId: "MFCap_<label>"`，ModForge 的「顯式 editorId 優先」規則讓同一個 label 永遠對應同一筆記錄（再吸一次＝更新同一個人，不會多生一個）。計畫全文：[plans/player-capture-capp.md](../ModForge/workflows/plans/player-capture-capp.md)。

**`sc ref` ＝ referrer：命名一個既有 ref（2026-07-12，DLL 端補齊；ModForge 消費端 `adc419b` 已在）**。三兄弟並列——`removals[]` **擦掉**既有、`overrides[]` **移動**既有、`references[]` **命名**既有。referrer **什麼都不動**（不新建、不改 transform、不 disable），只記「這個 ref 的身份 ＋ 一個自由 label」，匯出成頂層 `references[]`；ModForge 把 **label 註冊成可解析的名字**，於是 spec 裡**任何 ref 欄位**都能寫它（package 的 `sandbox.location`／`travel.place`、quest alias `forced:`、`linkedRefs`、`enableParent`、objective target、script Form prop）。典型用法：指一張椅子標「sofia's chair」→ Sofia 的 sandbox package 就錨在那張椅子。

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

另有 **clear all slots**（2026-07-12）＝**清空整個 palette**。插槽是**落盤跨存檔**的（不像擦除/override 那樣是可 revert 的存檔狀態）——清掉＝丟掉別的 playthrough 攢的東西，所以**兩道防呆**：① 按下去先變成 `really clear all N slot(s)?` ＋ `yes, clear` / `cancel`，要**再按一次**才真的清；② 清完出現 **`undo clear`**（本次 session 有效，按了整批回來、並重寫回磁碟）。想留備份就先按 `save to file`。

**匯出檔名（2026-07-12 起，每次一個新檔、永不覆蓋）**：

| 鈕 | 檔名 | 內容 |
|---|---|---|
| Export player cell | `scene-export_<cell EditorID 或 <ws>_x<X>y<Y>>_<YYYYMMDD-HHMM>.json` | placements（**物件，不含 NPC**）＋ removals ＋ overrides ＋ annotations ＋ **references** |
| Export all (loaded cells) | `scene-export_all-<玩家所在>_<YYYYMMDD-HHMM>.json` | 同上，掃全部已載入 cell |
| Export captures（Captures 頁也有一顆） | `captures_<YYYYMMDD-HHMM>.json` | **只有** capturedItems[] ＋ capturedNpcs[] |
| **Export requires** | `requires_<YYYYMMDD-HHMM>.txt` | **純文字報告**：這份匯出會讓生出來的 esp **依賴哪些非 vanilla mod**，逐筆講「是哪個欄位把它拉進來的」 |

同分鐘同場景再匯出＝加 `-2`/`-3` 後綴（`requires` 也一樣，`-2.txt`）。**⚠️ 下游 agent 別再寫死 `scene-export.json`**——去 SKSE 資料夾取最新一份（或使用者指名的那份）。

### Export requires — 「這個 esp 會需要哪些 mod？」在**遊戲內**就問得到

`sc capp` 吸一個玩家分身，就會把「給過你法術/perk/裝備的每一個 mod」變成 esp 的 **master**；缺 master 時 Skyrim **靜默不載**這個 esp（不報錯、log 也沒有，記錄就是不在）。ModForge C# 端 build 完會印同一份分析（＋`<plugin>.requires.txt` 旁檔，[Generator.Dependencies.cs](../ModForge/src/ModForge.Core/Generator.Dependencies.cs)）——但**那時你已經退出遊戲了**。這顆鈕把同一個問題**提前到匯出當下**：你人還站在那間房，覺得那顆 PROTEUS 法術不值得讓整個 mod 變成硬相依，重吸一次就好。

**不過濾任何東西**（完全複製優先＝使用者拍板），只是讓代價**可見**。掃描範圍＝DLL 手上會進匯出的全部：`placements[]`（base／cell／worldspace）、`removals[]`、`overrides[]`、`references[]`、`annotations[]`、整本 Captures 登記簿。判定規則與 C# 端**同一套**：vanilla ＝ Skyrim/Update/Dawnguard/HearthFires/Dragonborn **五個**；**CC（`ccXxxSSE###` / `_ResourcePack`）不算 vanilla**（按帳號綁定，沒買的人一樣靜默不載，只是報告會標原因）。

**⚠️ 只列「真的會變成依賴」的東西。** 有些欄位**寫著某個 mod 的 FormID，但 build 根本不會 link 它**——列進去是**說謊**（刪掉它並不會拿掉那個依賴）。這批一律排除，並在 SKSE log 逐欄位交代排除了幾筆：

| 排除的欄位 | 為什麼不是依賴 |
|---|---|
| `capturedNpcs[].activeEffects[].magicEffect` / `.source` | 當下 buff 快照，ModForge 從不消費（**兩個** ref 欄位，不只 magicEffect 那個）|
| `capturedNpcs[].base` | 一律**鑄新** NPC_，從不 override 來源 |
| `capturedNpcs[].defaultOutfit`（**當有 `worn` 裝備時**）| 穿在身上的甲會**鑄一件 OTFT 蓋掉它**，被捨棄的那件不會被 link |
| `capturedItems[].base`（**當 `kind: ingredient`**）| `IngredientSpec` 根本沒有 Template 欄位，這筆是死資料 |
| `annotations[].cell` / `.worldspace` | marker 是 inert 的，「不生任何記錄」|
| `references[].base` / `.cell` / `.worldspace` | DLL **刻意不寫 `anchor`** ⇒ 預設 `anchor:"none"` ⇒ build 連讀都不讀這三個 |

兩種**限定條件**的相依會加註記：`[template clone]`（`capturedItems[].base`、附魔的 `inventory[].item`——ModForge **deep-copy** 那筆記錄，form 本身不是 master link，但複本會拖著來源的 sub-link＋要用它的 mesh/材質，實務上仍需要那個 mod）與 `[named only]`（`references[].ref`——只有當 spec 真的有東西指向那個 label 時才成為 master）。

**跟 C# 版的差異**：C# 用「建好的 mod」當 master 清單的權威（掃 record FormKey＋`EnumerateFormLinks`），所以連 deep-copy 拖進來的 master 都抓得到，但**歸因會掉**（只剩 `record Weapon:MFCap_…`，講不出要刪哪一行）；DLL 這邊沒有建好的 mod 可掃，改用**規則表**（上面那兩張表，是逐一讀 C# 消費端讀出來的，不是猜的），代價是遇到全新欄位可能誤判，好處是 **template clone 也講得出是哪一行**。路徑前綴 `scene.` / `captures.` 指出該去改**哪一個檔**。兩邊格式對齊，可以互相比對。

**Scope（使用者 2026-07-12 拍板）**：cell 匯出＝**純場景/物件**，掃描時 **actor 一律排除**（面板/log 顯示 `N actor(s) excluded`）——NPC 交給 ModForge **按 marker（`annotations[]`）去擺**；真要複製某個 NPC 走 `sc cap` → 獨立的 `capturedNpcs[]` 檔。captures 是**跨 cell 的定義資料庫**，所以拆檔：一把附魔劍不屬於你剛好站的那間房。兩種檔都是合法 ModSpec，`build` 各吃各的（ModForge C# 端零改動）。

模式內操作不算佔鍵：numpad 編輯（8/2/4/6/1/3 位移、7/9 yaw、+/− 縮放、0 commit、`.` cancel、**5＝復原到編輯前姿態並續留編輯**）與 **numpad \*＝射線選取**照舊；位移/yaw/縮放**步長在 Settings 頁可調**（存 co-save）。當前模式/dp 狀態＋三個步長＋三本登記簿全部**存進存檔**（SKSE co-save）。`sc` 指令的實作＝劫持一個 retail 無作用的 vanilla console 指令（候選鏈首個命中者，2026-07-11 實機 donor＝`ClearAchievement`；全滅時面板 Settings 頁照樣能切模式）。

### 改鍵＝改 `SceneCaptureBridge.ini`（2026-07-12 拍板，**遊戲內 rebind 已移除**）

**檔案位置**：SKSE 資料夾（`…/Documents/My Games/Skyrim Special Edition/SKSE/SceneCaptureBridge.ini`）——跟 palette 存檔、所有匯出檔**同一個資料夾**。首次啟動**自動生成**（帶完整鍵名表註解），不用手動建。**刻意不放 `Data/SKSE/Plugins/`**：那裡在 MO2 mod 資料夾內，重裝 zip 會把它默默還原掉。

```ini
[Keys]
marker   = F11
delete   = F4
pick     = numpad 5
place    = F11
edit     = G
capture  = F11
referrer = F11
```

- **一行一模式**（`marker`/`delete`/`pick`/`place`/`edit`/`capture`/`referrer`，也吃 console 縮寫 `mk`/`del`/`pk`/`pl`/`ed`/`cap`/`ref`）。
- **值寫鍵名不寫 scan code**：`F1`–`F12`、`numpad 0`–`numpad 9`／`numpad . * - + /`／`numpad Enter`、字母數字、`Home`/`End`/`PageUp`/`PageDown`/`Insert`/`Delete`、方向鍵、`LAlt`/`RAlt`… 大小寫與空白不計（`NumPad 5` ＝ `numpad5` ＝ `num5`）；真要寫原始 DIK code 也行（`0x57` 或 `87`）。**生成的 ini 註解裡就有完整清單**。
- **保留鍵一律拒收**（WASD／Space／Shift／Ctrl／Esc／Tab／Enter／console 反引號）——寫了會被拒、該模式維持原鍵，SKSE log 講原因。
- **改完不必重開遊戲**：F1 → Settings 頁 → **`reload keys from ini`**。
- 鍵位每模式獨立、允許重複（預設全 F11）。

**ini vs co-save 的優先序：ini 贏。** 鍵位仍照樣**寫進 co-save**（SETT v7，跟著存檔走），但**讀回來時 ini 有指定的模式一律以 ini 為準**（co-save 那個值只寫進 log 說「被 ini 蓋掉」）；**ini 沒提到的模式**才吃存檔裡的值（沒有就 F11）。理由：ini 是**使用者的設定**，co-save 只是**這個存檔的狀態**——「我改了 ini 卻沒生效」是不可接受的失敗模式，而舊存檔裡可能還躺著 rebind 時代綁壞的鍵（載入時照樣過保留鍵驗證，壞的丟掉）。

**為什麼放棄遊戲內 rebind**：面板**不暫停遊戲**，抓鍵就等於在玩家手還按在 WASD 上的時候去搶輸入串流——**兩次嘗試都在實機失敗**（P5 2026-07-11 綁成 W；2026-07-12 的「保留鍵黑名單＋按下再放開才 commit」重作，使用者實機回報仍失敗）。ini 沒有這條賽道可輸：沒有 armed 狀態、沒有 input sink、沒有時序、沒有已按住的鍵。

## 現在有什麼（`src/`）

| 檔 | 內容 |
|---|---|
| `plugin.cpp` | SKSE 入口 + message handler；input sink **兩層**：編輯模式內部鍵 → 當前模式動作鍵（sink 形狀抄 my_skyrim_plugin_1 的 `FollowLight::HotkeySink`）。**沒有抓鍵層**了——改鍵走 ini。**numpad 重複按住**：`IsDown` → `Editor::HandleKey` / `Modes::HandleKey`（單按一步；動作鍵與 commit **必須單發**）；`IsHeld` → `Editor::HandleHold(code, HeldDuration())` **僅編輯模式** |
| `Modes.{h,cpp}` | P5 模式管理：一次一模式、每模式鍵位（預設 F11、允許重複）、動作分派；鍵位來源仲裁（`ApplyCoSaveBind`＝**ini > co-save > F11**）＋ DIK 鍵名表（`KeyName`/`KeyCode` 雙向，ini 靠它讀寫人類看得懂的鍵名）|
| `KeyIni.{h,cpp}` | **`SceneCaptureBridge.ini`（SKSE 資料夾）＝動作鍵設定檔**：缺檔就寫一份帶完整鍵名表註解的預設檔；`kDataLoaded` 讀一次，面板 `reload keys from ini` 可隨時重讀；保留鍵（WASD/Space/Shift/Ctrl/Esc/Tab/Enter/console）拒收。**遊戲內 rebind 已移除**（面板不暫停遊戲＝抓鍵永遠在跟玩家還按著的移動鍵搶，兩次實機都失敗）|
| `Console.{h,cpp}` | `sc` console 指令（ObScript 劫持：`LocateConsoleCommand` 改寫 inert donor 的 name/params/executeFunction）|
| `CoSave.{h,cpp}` | SKSE SerializationInterface（UID `'SCBR'`）：設定＋Markers/Eraser/Overrides/Captures 四本登記簿隨存檔走；revert 只清登記不碰世界；FormID 經 `ResolveFormID` 重解析（Captures 只存耐久 id，無 handle）。**record 版本**：`'ERSR'` v2→**v3**（+label+note）、`'OVRD'` v1→**v2**（+label+note）、`'SCCP'` v9→**v10**（+note，label 已在 v8）；舊存檔讀不到新欄位就是空字串，跟原本一樣|
| `UI.Settings.cpp` | Settings 頁：模式切換鈕、**每模式鍵位（唯讀顯示＋標來源 `(ini)`／`(save / default)`）＋ `reload keys from ini` 鈕**、編輯步長、marker 光球開關；`UI::ModeLine()` 各頁頂部常駐當前模式 |
| `UI.Fields.{h,cpp}` | **一個 bound text field，各頁的可編輯列共用**：修 2026-07-13 實機發現的「面板 buffer 與 registry 靜默分叉」（打了新名字沒按 Enter 就點開別的地方，registry 還是舊值，面板卻永遠畫著新值）。RULE 1——buffer 只有在那一格正被打字時才准跟 registry 不同，其餘每一幀都從 entry 重新種（靠 ImGui「同時只有一個 active item」的不變量）；RULE 2——Enter **或** deactivate-after-edit（點開別處）都會 commit。連帶讓 `g_rows.erase(seq)` / `g_slotBufs.clear()` 這類手動 buffer 失效呼叫全部消失（列會自癒）——唯一例外是 Palette（插槽用**索引**定位、沒有 seq），列表重排時要呼叫 `UI::ForgetEdits()` |
| `SceneExporter.{h,cpp}` | **核心**：`ExportCell` 走訪 cell → **vanilla diff**（ref 解得出耐久 id ⇒ 既有 ⇒ 跳過；解不出 ⇒ 玩家 `PlaceAtMe` 擺的 ⇒ emit）→ `placements[]`（**只有物件**：actor 掃描時排除，2026-07-12 拍板）；`ExportCaptures` 另出 captures 檔；`ResolveDurableId` FormID→`<plugin>:0xLOCALID`；`WriteSceneFile` 吐 json（檔名帶場景＋時間戳） |
| `CatalogFile.{h,cpp}` | **ModForge `scene-catalog.json` v1 的 portable consumer core**：驗 schema version、完整 source/record shape、SHA-256/FormKey 格式與未知欄位；以大小寫不敏感的 durable FormKey 建唯一索引，重複 key 明確拒絕；`AssessCompatibility` 擋 source 重複/斷序、FormKey/plugin 或 record/source cross-reference 錯誤、catalog/runtime plugin 集合或 global order 不一致；`Enrich` 保證 runtime 非空 name/model 優先。無 RE/SKSE 相依，`tests/CatalogFileTests.cpp` 可用 MinGW + CTest 驗證；`tests/RunModForgeCatalogContract.cmake` 另會呼叫 sibling ModForge 真實 CLI，以 synthetic full+light plugin 的 exporter bytes 串進 consumer，防跨 repo 契約漂移。`Catalog.cpp` 在 kDataLoaded 從 SKSE 資料目錄載入，以 `TESDataHandler::files` 保留 full/light 全域順序，Browser 首開掃 runtime form 時 merge；缺檔/壞檔/相容性不合 fail-soft。 |
| `UI.{h,cpp}` | 遊戲內面板（[SKSE Menu Framework 3](../ModForge/sub_projs/mod-survey/findings/skse-menu-framework-3.md) / Dear ImGui）：顯示所在 cell、Export 按鈕、上次匯出統計；Eraser/Palette/Editor/Markers 各頁帶 **this cell only 過濾**與逐列 undo/revert/del。**2026-07-12 清掉「按一下就執行世界動作」的按鈕**（place marker here／erase by ray／pick by ray／capture crosshair・by ray／select by ray／cancel (restore)）——這批動作全走 `sc` console 指令＋每模式動作鍵（P5 之後 UI 觸發是多餘的），面板只留設定/檢視/清單類。**軟相依**——`IsInstalled()` 是 `GetModuleHandleW` 探測，沒裝框架就只有 hotkey。Palette 頁的插槽改名欄改用 `UI.Fields` 的 bound field（Enter／點開別處都會 commit）。**Eraser／Captures／Editor-overrides／Palette 四頁現在每列都有 label＋note 的 bound field＋apply 鈕**（同樣走 `UI.Fields`）；Eraser／Overrides 兩頁的列改用**耐久 id 的 hash** 定位（不用 list 位置），上面一列 undo/revert 不會把下面列的欄位錯位到別筆 entry |
| `UI.Markers.cpp` | Markers 頁（this-cell 過濾、每列 `edit` 鈕）＋ **marker 編輯視窗**（E 按 marker 開啟：label／kind／**note 多行**／delete；`AddWindow` 獨立視窗，開著會暫停遊戲收輸入）。Markers 頁列上的 label／kind 欄改用 `UI.Fields` 的 bound field（Enter／點開別處都會 commit）；編輯視窗本身有明確 save／cancel 鈕，維持原樣不受影響 |
| `extern/SKSEMenuFramework/` | vendored 消費者 header（LGPL-2.1，`GetProcAddress` shim，不連結 DLL）|
| `Aim.{h,cpp}` | 共用視角射線＋**兩種選取入口**：`CrosshairRef()`（互動準星，老手感）與 `RayRef()`（物理射線→反查 ref，樹/純裝飾 static 用）。**射線絕不做自動 fallback**（使用者拍板 2026-07-11）——牆/地板都是 ref，自動 fallback 會把「按空」變誤抓；射線只走明示按鈕/專用鍵 |
| `Eraser.{h,cpp}` | 橡皮擦（`sc del` 模式動作，`sc del er1`＝射線瞄準）：authored→disable＋登記→`removals[]`；自己的 dynamic→真刪除無痕；entry 記 name/座標/cell（面板逐列顯示＋過濾）；undo 逐列/逐 cell/最近一筆。（`scan disabled` 跨存檔救援已移除——co-save 持久化耐久 id 後冗餘；面板的 `erase by ray` 觸發鈕 2026-07-12 移除，改走 `sc del er1`＋動作鍵）。entry 現帶 **label／note**（`SetLabel`/`SetNote`，鍵入耐久 id），note 講「為什麼收進去」（"cleared for the shelf"）|
| `Palette.{h,cpp}` | 滴管（`sc pk` 吸、`sc pl` 擺、**`sc pkc [Label]` console 選取版**、`sc pk er1`＝射線瞄準；runtime-only base 拒收）；**插槽落盤 `scene-capture-palette.json`（跨存檔跨 session）**，base 解析不回（plugin 移除）標 unavailable 不炸。**`sc pk ed1` 連實例 `ExtraEnchantment` 一起吸**（durable ENCH 引用／runtime ENCH 記 MGEF effects；runtime ENCH 的**指標絕不快取**——插槽會落盤，那是存檔綁定的 form）。面板的 `pick by ray` 觸發鈕 2026-07-12 移除，改走 `sc pk er1`＋動作鍵。插槽現帶 **note**（`Palette::SetNote`），**跟其他三本不同——落盤到 json 不是 co-save 狀態**，隨插槽存進 `scene-capture-palette.json`（`save to file` 會帶著走，比存檔活得久）；舊插槽沒有 note 欄就是空字串，不用遷移|
| `Palette.Placed.cpp` | **我們擺出去、匯出時要多講一句話的 ref 登記簿**（`sc pl py0` → `noHavokSettle`；`sc pl ed1` → 鑄造的 `capturedItems[]`）。一般擺放**不建列**（vanilla diff 本來就吐得完美）。identity ＝ handle，dead handle 用 **base+座標**在匯出掃 cell 時就地撿回（不必 kPostLoadGame hook）。co-save `'PLEX'` |
| `Physics.{h,cpp}` | havok 凍結/放開的共用原語（`HavokMovable` 判定 ＋ `FreezeDeferred` 延後到 3D 載入才 `SetMotionType`）。Markers/Editor/Palette 三處共用（原本各抄一份）。**⚠️ 這一層是 runtime、進不了 esp**——真正 ship 的是 `noHavokSettle` 記錄旗標 |
| `Captures.{h,cpp}` | 定義擷取器（Palette 的姊妹：吸「沒有耐久 base 可引用」的內容）。`sc cap`／面板讀 live form 的語意內容 → ModForge **鑄新記錄**。**①物品**：附魔武防（實例 ExtraEnchantment 優先，否則 base formEnchanting）＋藥水/材料效果 → `capturedItems[]`（效果 shape = EffectSpec）。**②NPC**：`TESNPC` 外貌（race/sex/weight/height＋headParts/tintLayers/faceMorphs+parts/hair/skin/FTST/outfit）＋**base perks（id+rank）**＋**當前 buff（active-effect 快照：source spell＋MGEF＋mag/dur/elapsed）**＋**旗標（unique/dead/essential/protected）**＋**顯式數值（H/M/S＋18 技能，讀 base actor values；所有 actor 都收）**＋擺位 → `capturedNpcs[]`。**③玩家**：`sc capp` 直讀玩家 base TESNPC（chargen 就在那），perk 走 `PlayerCharacter::addedPerks`。**唯一 NPC 也收**（2026-07-11 使用者反轉，帶 `unique` 旗標給 ModForge 判斷）。登記簿隨 co-save（record `'SCCP'` **v10**：v8 +label +H/M/S +skills、v10 +note，只存耐久 id）。entry 的 `label` 原本只能靠 console `sc capp <label>` 設，現在面板也開了欄（`Captures::SetLabel`/`SetNote`，鍵入 seq），打錯字不用重擷取。**⚠️ NPC 待驗/未涵蓋**：(a) PROTEUS 若用 NiNode live override 不寫 TESNPC，擷到的臉是 base 的非套用後的；(b) **身形/臉部「mesh」本身不收**——只收「定義」（headParts+morphs+race+weight，臉/身是由這些＋facegen 烘焙生成的），baked FaceGeom nif 與 RaceMenu/NiOverride 雕塑不在 TESNPC，需 facegen 烘焙＝ModForge 下游活 |
| `Editor.{h,cpp}` | 編輯模式（`sc ed` 動作鍵選中準星目標；**numpad \* ＝射線選取**）→ numpad 微調（8/2/4/6/1/3 位移、7/9 yaw、+/− 縮放、0 commit、. cancel、**5＝復原續編**）；步長 runtime 可調（Settings/co-save）；havok-movable 類型編輯期物理凍結；自己的 ref＝live pose 直接匯出（不進 overrides 列，正常），**authored ref＝commit 時登記進 Overrides**（2026-07-11 契約拍板）。**新 `HandleHold(code, dur)`**：按住連續位移，nudge 鍵（8/2/4/6/1/3 位移、7/9 yaw）重複 `step×frameDelta×rate`（8→40 steps/sec、1.5s 內滾升、0.35s dead zone、gap>0.25s 捨防傳送）；0 commit、. cancel、5 選、per-axis revert **不重複**；rotate 子模式 8/2→per-axis revert（只移動模式重複）；scale ∈[0.05,10] |
| `Referrer.{h,cpp}` | referrer（`sc ref` / `sc refc`）：**命名**一個既有 ref——只記身份＋label，**世界完全不動**（與 Eraser 的唯一差別）→ 匯出頂層 `references[]`。(乙) 檔內目標（我們 `sc pl` 擺的 dynamic ref）identity ＝ **handle**，匯出時給該 placement 蓋 `EditorIdOf()` ＝ `MFRef_<label>_<seq>`；(甲) 外部 authored ref 記耐久 id。拒收 marker proxy／自家 actor／重複 label。co-save `'RFRR'`；`ReacquireOrphans()` 讀檔按 base+座標撿回檔內目標 |
| `UI.References.cpp` | References 頁（最新在前、label/note 改名＋擋撞名、顯示檔內 editorId／耐久 id／base／cell／座標、逐列刪除）。label／note 欄改用 `UI.Fields` 的 bound field（Enter／點開別處都會 commit）；撞名被拒的改名現在會**視覺上彈回**已存的 label，而不是繼續顯示被拒的文字 |
| `Overrides.{h,cpp}` | authored ref 被編輯 commit 後的登記簿（比照 Eraser：明示、不 diff——havok 噪音）→ 匯出頂層 `overrides[]`（ref/position/rotation°/scale；actor 不帶 scale）；Editor 面板頁逐筆/全部 revert 回 baseline。entry 現帶 **label／note**（`SetLabel`/`SetNote`，鍵入耐久 id），同一 ref 重編會保留（跟保留 baseline 一樣）|
| `Requires.{h,cpp}` | **「這個 esp 會需要哪些 mod？」＝ Export 頁 `Export requires` 鈕**（→ `requires_<stamp>.txt`）。走訪**匯出後的 json**（`SceneExporter::ScanAll`/`ScanCaptures`＝匯出減掉寫檔與統計副作用），收集每個 `<plugin>:0xLOCALID` 及其 JSON 路徑 → 依規則表分成「真的會 link」／`[template clone]`／`[named only]`／**排除**（`activeEffects`、`capturedNpcs[].base`、被蓋掉的 `defaultOutfit`、ingredient 的 `base`、`annotations[]` 的 cell、`references[]` 的 base/cell/ws）。vanilla/CC 判定與 [Generator.Dependencies.cs](../ModForge/src/ModForge.Core/Generator.Dependencies.cs) **同一套，改一邊要改兩邊**。`Analyze(scene, captures)` 是**純函式**（不碰遊戲狀態）——刻意拆出來，這樣才驗得到 |
| `PCH.h` / `log.h` | CommonLibSSE PCH（含 nlohmann）＋ spdlog file logger |

## 尚未做（依 spec 里程碑）

- **PROTEUS clone 的 ref 是 dynamic**：`npcRoles[].actorRef` 需要耐久 ref id，dynamic ref 沒有。PROTEUS 已降為**可選**（預設走 ModForge 直接生的「大眾臉」NPC，ref 耐久），故不阻塞；見 [spec](../ModForge/workflows/specs/ingame-scene-export-design.md)「NPC 來源」。
- **§B 語意標記 / §D role tag / §E 滴管·範圍吸取·橡皮擦**：UI 骨架（`src/UI.cpp`）已接上 SKSE Menu Framework，剩下的是把這些工具畫進面板。

## 使用流程：marker → agent → 世界改變（P1，實機閉環 2026-07-10）

玩家側：console `sc mk` 進標記模式 → **動作鍵（預設 F11）**在準星處放 marker（無命中落腳下，一鍵兩用）→ 對著 marker **按 E 開編輯視窗**（改 label/kind、寫 **note** 給 agent 的補充指示、刪除）或 **F1 → Markers** → **F1 → Export 鈕**匯出。登記簿隨存檔走（co-save）；跨存檔撿孤兒才用 `adopt this cell`。（面板的 `place marker here` 觸發鈕 2026-07-12 移除——`sc mk` 動作鍵本來就落腳下 fallback，兩者是同一顆函式的兩個呼叫點，備援已無必要）
marker 的樣子＝**鐵匕首**（`Weapons\Iron\IronDagger.nif`，houseCARL 對 WEAP 01397E 驗過）——換掉靈魂石是因為 marker 現在會**記錄＋可編輯完整朝向與大小**，匕首的**劍尖方向**剛好把朝向視覺化。有碰撞才能被 E/準星選到；weapon clutter havok 會掉 → 放置當下 `SetMotionType(kKeyframed)` 凍住。marker 匯出的 `annotations[]` 現在帶 `rotation{x,y,z}`＋`scale`（`angleZ` 仍在＝`rotation.z`，向後相容）。

**agent 對接配方**（拿到需求如「在 goat 放一隻山羊」時照做）：

1. 讀 `.../compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE/` 裡**最新一份** `scene-export_*.json`（檔名帶場景＋時間戳，2026-07-12 起不再是固定 `scene-export.json`）的 `annotations[]`——每筆有 seq/label/kind/position/rotation/scale/note/cell 或 worldspace。**NPC 一定走這條**（cell 匯出不含 actor）。
2. 查 base：houseCARL `cross_plugin_query`（如 `editorid_contains=EncGoat`）。
3. author spec：`placements[]` 帶 marker 的 position/angleZ（rotation.z）＋歸屬欄位。**⚠️ 外部 NPC base 必須 `"kind": "npc"`**——isNpc 自動判定只認 in-spec base，漏了會生成 REFR（不生怪、無報錯，dump 看到 `PlacedObject` 而非 `PlacedNpc` 即中招）。
4. `build` → `dump` 驗座標與記錄型別 → 產物放 `<MO2>/mods/<新資料夾>/`。
5. 提醒使用者：**MO2 F5 refresh 後新 mod 預設不勾**，要手動勾 mod＋plugin。

先例：`mods/SCB Goat Demo/`（本 README 同日的實機驗收產物）。

## 持久化與 adopt 語意（P5 co-save 後全面升級）

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

## 建置踩坑（2026-07-10 首編）

- **`ports/` overlay 必須存在**。`CMakePresets.json` 的 `vcpkg-clang-linux` 指向 `${sourceDir}/ports`；`commonlibsse-ng-fork/fix-clang-delete.patch` 是 clang-cl 編 CommonLibSSE-NG 的**必要**修補，`directxtk` 也得走 overlay（registry 版在 `x64-windows-skse-clang` 下編不過）。從 `my_skyrim_plugin_1/ports/` 整包搬。
- `CMakeLists.txt` 需 `find_package(directxtk CONFIG REQUIRED)`——CommonLibSSE 的 export target 在 link interface 裡具名 `Microsoft::DirectXTK`。
- **改過 preset / vcpkg.json 後必須 `rm -rf build/release-clang-cl-linux`**。stale `CMakeCache.txt` 會讓 vcpkg.cmake 跳過 chainload toolchain，clang-cl 就不帶 `/winsysroot`，錯誤訊息長得像「編譯器壞了」。
- `ForEachReference` 的 callback 收 `TESObjectREFR*`（指標），不收 reference。

## 里程碑對位（spec §最小垂直切片）

本 repo負責 **M4（採集橋 spike）→ M6**。M0–M2（ModForge 側 `SceneImport` + `SceneNpcRoleSpec`，手寫 scene.json 即可驗）是 ModForge 本命工作、離線可測，**不依賴本 DLL**——兩線並行。
