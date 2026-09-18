# 最愛 / favorite — 規格（README §A3）

← [README](../README.md)｜契約權威（場景匯出）：[ingame-scene-export-design.md](../../ModForge/workflows/specs/ingame-scene-export-design.md)

**需求原文只有一行**：「A3 favorite UI（加入最愛語意、星號／置頂）也尚無實體」（[README.md:193](../README.md)）。
外部 spec **沒有提到 favorite**（2026-09-18 對 387 行全文查證，零命中），所以作用對象、持久化、取消與排序
全是本規格第一次定下來的——底下每一條都標了理由，好翻案。

## 作用對象：Browser 與 Palette **共用一份**，鍵是耐久 base id

兩個清單都痛，但痛法不同：

- **Browser（catalog）**：幾萬筆，且顯示有 `kMaxRows` 硬上限，超過只叫人「narrow the search」
  （[UI.Browser.cpp:127](../src/UI.Browser.cpp)）。要的是「我常用的那幾個，別再搜了」。
- **Palette（插槽）**：磁碟持久、跨存檔，面板固定「最新在最上面」的插入序反轉
  （[UI.cpp:403](../src/UI.cpp)），沒有任何排序。用久了常用的會沉到下面。

兩者的共同身份都是 **`<plugin>:0xLOCALID`**（`Catalog::Entry::id`、`Palette::Slot::baseId`），
所以**一份以耐久 base id 為鍵的集合就能同時服務兩張表**，不必各做一套。

```
scene-capture-favorites.json  ──┬──► Browser 列：星號欄 + 「favorites only」+ 最愛排前
   { ids: ["Skyrim.esm:0x10C0E3", …] }   └──► Palette 列：星號欄 + 最愛排前（baseId 命中）
```

**已知且刻意接受的後果**：palette 裡兩個 baseId 相同、只有姿態不同的插槽會**一起**變成最愛。
這是「我喜歡這個桶子」而不是「我喜歡這個插槽」的語意——對一個挑選輔助功能，這是對的那一邊。
要改成 per-slot 就得給插槽一個穩定 id（現在插槽的身份是 `index`，會因刪除而位移），成本高出一個量級。

## 存哪：自己的 JSON，不進 co-save

| | co-save（`'SCCP'` 等） | JSON（`scene-capture-palette.json`）|
|---|---|---|
| 綁定 | 存檔 | 磁碟，跨存檔跨 playthrough |
| 加欄位 | **要動版本號** + `if (version >= N)` 讀分支 | `item.value(key, default)` 天生向後相容 |

最愛跟 palette 同性質——**它是「我這個人的偏好」，不是「這個 playthrough 的狀態」**，
換存檔、開新檔都應該還在。所以走 JSON，落在 `SKSE::log::log_directory()`，
與 `scene-capture-palette.json` 及匯出檔同層（比照 [Palette.cpp:31](../src/Palette.cpp) 的 `StorePath()`）。

**不動任何 co-save record，不動任何版本號**——這也是本功能風險最低的原因。

```json
{ "contract": "scene-capture-favorites/1",
  "ids": ["Skyrim.esm:0x10C0E3", "Skyrim.esm:0x000F026C"] }
```

- `ids` 是集合語意：**無序、去重**；寫出時排序，讓檔案 diff 穩定。
- **讀取 fail-open**：檔案不存在／壞 JSON／`contract` 不認得 → 視為空集合並記一行 log，
  **絕不阻擋面板**。最愛是輔助，壞掉最多是「星號不見了」，不該讓人開不了 Browser。
  （`Palette` 的 json 沒有 `contract` 欄位，是既有的小債；新檔不重蹈。）
- **寫入時機＝每次切換立刻落盤**，比照 `Palette::SetNote`（[Palette.cpp:494](../src/Palette.cpp)）。
  UI 那邊原話「the note is disk state — it is only real once it is written」，最愛同理。

## SCB 與 Godot 誰讀誰寫

**SCB 獨佔讀寫，Godot 完全不參與。** 最愛是遊戲內面板的挑選輔助，godot-worldspace-editor
既不讀 catalog、也不讀 palette，兩邊沒有交集。

## 與現有 capture / 匯出流程的關係：**沒有關係**（刻意）

`scene.json` 的頂層欄位（`placements[]`／`removals[]`／`overrides[]`／`annotations[]`／
`references[]`／`capturedItems[]`／`capturedNpcs[]`）**一個都不動**，`SceneExporter*` 一行都不碰。

理由：favorite 是**操作期的挑選狀態**，不是場景語意。把它寫進 `scene.json` 等於單方面擴充 ModForge 的
輸入契約，逼下游決定要不要吃——換來的價值是零（ModForge 不會因為你喜歡某個桶子而生出不同的 esp）。
若日後真要，照 `removals[]` 的 shorthand-collapse 慣例（[SceneExporter.Registries.cpp:24](../src/SceneExporter.Registries.cpp)）加，
但那必須先在外部 spec 登記。

## 實作落點

新增一支**不依賴 SKSE/CommonLib 的** `src/Favorites.{h,cpp}`——這是刻意的，比照 `CatalogFile.cpp`：

```
bool   Contains(id)          // 查
bool   Toggle(id)            // 切換，回傳切換後狀態
size_t Count()
void   LoadFrom(path) / SaveTo(path)          // fail-open 讀、排序寫
string Serialize() / bool Deserialize(text)   // 純字串進出，單元測試的著力點
vector<size_t> OrderFavoritesFirst(ids, order) // stable：最愛保序在前，其餘保序在後
```

`Favorites` 只認 `std::string` id，**沒有 `RE::` 型別**，所以進得了既有的 portable 測試套件。
`Palette`／`Catalog` 的 entry 都帶 `RE::TESBoundObject*`、`RE::NiPoint3`，本身永遠進不去——
把純資料形狀抽出來是唯一能離線驗的做法。

UI 接法：

- 星號用 **`ImGuiMCP` 按鈕/Checkbox 就地 commit**，**不要**套 `UI::BoundText`——
  那套雙緩衝是為文字輸入的「打完沒按 Enter 就切頁」問題設計的（[UI.Fields.h:1](../src/UI.Fields.h)），bool 沒這問題。
- Browser：星號欄 + `favorites only` checkbox；排序掛在 `Refilter()` 之後
  （[UI.Browser.cpp:49](../src/UI.Browser.cpp)），對 `g_hits` 做 stable partition，**不改 `Catalog::Filter`**。
- Palette：把面板現有的反向遍歷換成「先 `OrderFavoritesFirst` 再畫」，插槽本身的 json schema 不動。

## 驗證（不開遊戲）

新增 `tests/FavoritesTests.cpp`，target `favorites_tests`，掛進既有 CTest：

```
ctest --test-dir build/tests-native --output-on-failure
```

寫死 **8 條**，不自行加碼：

1. `Toggle` 兩次回到原狀；`Count` 隨之增減。
2. 重複 `Add` 同一 id 只留一份（集合語意）。
3. `Serialize` 的 `ids` 已排序（檔案 diff 穩定）。
4. round-trip：`Serialize` → `Deserialize` → 集合相等。
5. 壞 JSON → fail-open 成空集合，**不丟例外**。
6. `contract` 欄位不認得 → 同樣空集合、不丟例外。
7. `OrderFavoritesFirst`：最愛在前、非最愛在後，**兩組各自保持原順序**（stable）。
8. 沒有任何最愛時 `OrderFavoritesFirst` 是恆等變換。

## 我替使用者做的假設（可翻案）

1. **作用對象＝Browser ＋ Palette 共用一份**，鍵是耐久 base id。
   翻案代價：若只要 Browser，Palette 那一半拿掉即可；若要 per-slot，插槽得先有穩定 id（大改）。
2. **存 JSON、不進 co-save**——即「換存檔最愛還在」。若使用者要的是 per-playthrough，這條要反過來。
3. **不進 `scene.json`**，匯出與 ModForge 契約零改動。
4. **同 baseId 的多個 palette 插槽一起被標記**（見上）。
5. **v1 只有「最愛優先 + 只看最愛」，沒有自訂排序、沒有分組、沒有多組收藏夾。**
6. 讀檔壞掉**靜默降級**而不是報錯擋人。
