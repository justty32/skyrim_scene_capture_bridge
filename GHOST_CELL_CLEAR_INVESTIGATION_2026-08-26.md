# Placement ghost 跨 exterior cell 消失調查（2026-08-26）

目標基線：`5273576`。症狀是把準星指向遠處再移回來後，placement ghost 消失且不再出現。

## 成因

`Aim::RenderedCameraHit()` 從 rendered camera 發出最遠 4096 units 的 world ray，回傳命中點
（`src/Aim.cpp:116-129`）。舊版 `Preview::Update()` 每幀把 ghost 直接移到該點
（`5273576:src/Preview.cpp:327-330`）；exterior 遠處命中可以位於玩家相鄰 cell，因此
`SetPosition()` 會讓 ghost 的 parent cell 改變。下一幀舊版以
`ref->GetParentCell() != player->GetParentCell()` 判斷「玩家離開 cell」
（`5273576:src/Preview.cpp:315-324`），其實比較到的是程式自己剛移動的 ghost，於是誤呼叫
`Clear()`。這完整解釋「看遠處」後才消失：ray hit 跨 cell → ghost parent cell 改變 → 下一幀
cell mismatch 清除。

## 為什麼不復原

交接線索認為可能是 `g_fromCatalog` 殘留；實際相反，`Clear()` 會把它清成 `false`
（`5273576:src/Preview.cpp:278-280`）。永久不復原發生在 Browser 原本替 `gh0` 使用者暫時開啟
ghost 的路徑：`UI.Browser.cpp:72-75` 呼叫 `ForceGhostOn()`，後者記下
`g_ghostForced = true`。cell mismatch 誤呼叫 `Clear()` 時，`Clear()` 又把 place ghost 設定還原成
`gh0`（`5273576:src/Preview.cpp:281-286`）；之後 `Update()` 的 `want` 永遠是 false
（`5273576:src/Preview.cpp:289-298`），所以不可能進入重建分支。若使用者原本就是 `gh1`，舊版
下一幀會重建 palette slot，而不是永久消失；但它仍會丟掉 Browser pinned source。

舊版預期 log 因而是：遠處 hit 後出現 `Preview: left the cell — ghost cleared`，接著
`Preview: ghost cleared`；Browser forced 路徑還會出現
`Modes: place ghost preview -> off (gh0)`，之後沒有新的 `Preview: ghost ...` spawn 訊息。這是
程式碼狀態轉移必然產生的序列，不需要把缺少 spawn 猜成 raycast 失敗。

## 修法與取捨

`src/Preview.cpp:35-38,220-224` 記錄 ghost 建立時玩家所在的 cell；`Update()` 現在只在玩家
自己的 parent cell 改變時重建（`src/Preview.cpp:337-345`），不再用可能被 aim follow 移到鄰格的
ghost parent cell 判斷。這保留原本「玩家換 cell 時不可留下孤兒 ref」的意圖，又不需要限制
4096-unit ray、夾座標或改動 rendered-camera aim。

內部重建使用 `Respawn()`（`src/Preview.cpp:238-249`），先保存 catalog base/label，再刪除舊 ref
並從原 source 建立新 ghost；它不呼叫具使用者語意的 `Clear()`，所以不會歸還 Browser 暫借的
`gh1`。`Update()` 的 inactive 分支也不再以 `!g_fromCatalog` 阻擋重建
（`src/Preview.cpp:320-330`），因此 live handle 因其他原因失效時，catalog 與 palette source 都有
恢復路徑。實際玩家換 cell 時，新版 log 應是
`Preview: player changed cells — ghost rebuilt` → `Preview: ghost cleared` → 新的
`Preview: ghost ...`；只把準星指到相鄰 cell 則不會產生清除訊息。

修正沒有碰 `Aim.cpp` 的 ghost self-hit filter、`Physics.cpp` 的 deferred keyframe retry，或
follow frame 的 `SetPosition()`／`Update3DPosition(true)`；因此 `5273576` 修好的不自轉、不逐幀
靠近玩家契約維持不變。

取捨是玩家真正跨 cell 時會重生 preview，而不是把舊 ref 留在已離開的 cell；fresh ghost 依目前
aim 重新計算 auto-scale/pose，優先保證沒有 orphan 且 place-mode invariant 立即恢復。

## 尚需實機驗收

本線依限制不啟動或操控遊戲，也不部署 DLL。使用者部署後只需在 exterior 的 cell 邊界附近確認：
準星先指向遠處相鄰 cell、再移回近處，ghost 始終存在且仍可 commit；接著讓玩家本人實際跨過
cell 邊界，確認舊 ghost 不殘留、新 ghost 會重建並仍可用。實機結果才是 runtime 最終通過依據。
