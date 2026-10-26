# Export requires — 「這個 esp 會需要哪些 mod？」在**遊戲內**就問得到

← [README](../README.md)

`sc capp` 吸一個玩家分身，就會把「給過你法術/perk/裝備的每一個 mod」變成 esp 的 **master**；缺 master 時 Skyrim **靜默不載**這個 esp（不報錯、log 也沒有，記錄就是不在）。ModForge C# 端 build 完會印同一份分析（＋`<plugin>.requires.txt` 旁檔，[Generator.Dependencies.cs](../../ModForge/src/ModForge.Core/Generator.Dependencies.cs)）——但**那時你已經退出遊戲了**。這顆鈕把同一個問題**提前到匯出當下**：你人還站在那間房，覺得那顆 PROTEUS 法術不值得讓整個 mod 變成硬相依，重吸一次就好。

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
