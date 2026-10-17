# 改鍵＝改 `SceneCaptureBridge.ini`（2026-07-12 拍板，**遊戲內 rebind 已移除**）

← [README](../README.md)（本頁 2026-10-17 自 README 拆出，內容未改寫）

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
