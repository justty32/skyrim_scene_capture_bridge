# Ghost 持續旋轉／靠近玩家調查（2026-08-25）

目標版本：`scene-capture-bridge@a17e460`。Runtime 補充事實：只有半透明 ghost 受影響；
可拾取／可動、有 Havok rigid body 的物品會持續旋轉並靠近玩家，STAT 正常；手離開輸入、
camera 靜止仍持續；第一人稱、vanilla 第三人稱、SmoothCam 都可重現。

## 1. 旋轉資料路徑完整追蹤

### Slot／Browser 到 ghost

- Palette 從 JSON 載入 slot angle：`src/Palette.cpp:194-197`；從 world ref 滴管擷取
  `ref->data.angle`：`src/Palette.cpp:231-278`（實際賦值 `:257-258`）。這些都是一次性來源。
- Browser 點 catalog entry：`src/UI.Browser.cpp:68-76` 切 place mode、開 ghost、呼叫
  `Preview::ShowBase()`；catalog 沒有 captured pose，所以 `src/Preview.cpp:268-270` 傳零 angle。
  Browser 的 `add to palette` 只在按鈕觸發時把目前 ghost yaw 寫入 slot：
  `src/UI.Browser.cpp:174-181`。
- `Preview::Spawn()` 在 `src/Preview.cpp:163-229`：先 `PlaceObjectAtMe`，加 sentinel
  （`:170-177`），`SetCollision(false)`（`:179-197`），取 aim point（`:199-200`），再把
  `slotAngle` 一次寫進 `g_angle`（`:202`），由 `ApplyPose()` 寫進 ref（`:112-116`,
  `:207-208`）。`g_spawnAngle`（`:204`）只供 numpad revert。
- 可動 base 會呼叫 `Physics::FreezeDeferred()`：`src/Preview.cpp:210-213`。
  `HavokMovable()` 的類型表在 `src/Physics.cpp:7-22`；STAT 不在表內，Misc/Weapon/Armor/
  AlchemyItem 等在內。

### 每幀 follow

- HUD hook 每 frame 呼叫 `Preview::Update()`：`src/UI.cpp:74-79`；Browser 開著時另有一個
  idempotent 呼叫：`src/UI.Browser.cpp:79-82`。
- `src/Preview.cpp:289-331` 只有 palette selection 真正改變時才 `ShowSlot()`／重生 ghost
  （`:300-308`）。一般 follow frame 走 rendered-camera ray，然後只做
  `SetPosition(pos)` + `Update3DPosition(true)`（`:326-330`）；**不讀 camera yaw、不寫
  `g_angle`、不呼叫 `SetAngle`**。
- `Aim::RenderedCameraHit()` 在 `src/Aim.cpp:120-132`，只由 final `NiCamera` 取 ray origin／
  direction。ray hit 只產生 position（`src/Aim.cpp:45-90`），沒有 orientation output。

### 所有 `g_angle` writer／reader

- 一次性 spawn：`src/Preview.cpp:202-205`。
- Browser yaw slider：`src/UI.Browser.cpp:195-200` → `Preview::SetYaw()`
  `src/Preview.cpp:248-250`；只有 widget value 改變才寫。
- numpad tap／hold：input sink `src/plugin.cpp:77-98` → `Preview::HandleKey()`
  `src/Preview.cpp:333-371` 或 `Preview::HandleHold()` `:373-377` → `Nudge()`
  `:120-142`。真正連續 angle writers 只有 `:125-130`；2/5/8 revert writers 在
  `:341-354`。hold 的 dead-zone／frame delta 在 `src/Numpad.cpp:6-44`。
- reader／world apply：`ApplyPose()` `src/Preview.cpp:112-116`；UI reader `Yaw()` `:237`。
- commit：place action 在 `src/Modes.cpp:106-118` 分流到 `Preview::Commit()`；commit 只在按鍵／
  按鈕當下把 ghost live `ref->data.angle` 複製進 temporary slot（`src/Preview.cpp:379-406`，
  angle 在 `:398`），再由 `Palette::PlaceSlot()` 一次 `SetAngle(s.angle)`
  （`src/Palette.cpp:308-374`，`:325-332`）。真實 ref 只登記在 placed registry，沒有
  per-frame updater。
- real ref 之後只有使用者明確進 Editor 才會被寫 transform：selection／原姿態
  `src/Editor.cpp:148-208`，tap／hold writer `:216-325`。Havok 本身仍可改 visual/body pose，
  但那不是 `g_angle` writer。

結論：camera ray 沒有 angle 資料路徑。「持續旋轉」不是 camera yaw 改寫 `g_angle`；它是
movable ghost 沒成功 keyframe 後仍受 Havok 模擬的 visual/body rotation。

## 2. 可能成因排序

1. **最高：ghost 的 deferred freeze 在 rigid body ready 前就錯誤停止重試。**
   觸發條件：base 通過 `HavokMovable()`，`PlaceObjectAtMe` 後 3D root 已出現，但 child rigid
   body 還沒準備好。舊 `src/Physics.cpp:31-39` 只以 `Get3D()` 判定完成，呼叫
   `SetMotionType(kKeyframed)` 後忽略其 bool 回傳並立即 return。於是一次失敗便永久保持
   dynamic。為什麼連續：Havok 每 tick 持續積分 angular/linear velocity、重力與接觸，物品會
   滾／轉／被 player 接觸推動；STAT 沒 dynamic body、也不進 freeze，與實測分界完全吻合。
2. **次高、與第 1 條形成靠近 feedback：a17e460 的 A8 broad collector 沒排除 ghost。**
   觸發條件：第 1 條留下可被 broad ray 看見的 movable rigid body。a17e460 collector 只拒絕
   `IsPlayerRef()`，沒有拒絕 live ghost 或 `[SCB preview ghost]` sentinel。為什麼連續：
   `Preview::Update()` 每 frame raycast，再把 ghost 移到本 frame 的 hit point；若 hit 是 ghost
   自己，下一 frame 的命中面跟著移動，形成逐 frame 向 camera 靠近的回饋。三種 camera 都走
   同一函式，所以不會只限第三人稱。
3. **movable body 與每幀 `Update3DPosition(true)` 互相競爭。**
   觸發條件：freeze 失敗且 follow on。為什麼連續：程式每 frame 推 ref visual transform，Havok
   同時維護 dynamic body transform；這會放大轉動／位置抖動。它是第 1 條的後果，不是獨立
   angle writer。
4. **hold-repeat 輸入。** 路徑確實能連續改 angle（`plugin.cpp:95-98` →
   `Preview.cpp:373-377`），但觸發條件必須有 `ButtonEvent::IsHeld()`；使用者完全離開鍵鼠仍重現，
   且此路徑不改 position，不能解釋靠近，故降到低可能。
5. **Browser slider。** 只在 Browser 開啟、ImGui slider value 變動時觸發，且不改 position；
   無輸入仍重現，排除。
6. **每幀 respawn／palette selection churn。** 觸發時每次都會 `Vanish()`＋`Spawn()`，log 會反覆
   出現 `ghost cleared`／`Preview: ghost`；本次 window 每個 ghost 只有一條 spawn，沒有 churn。
7. **camera yaw 連動或 commit 後 real ref 仍被 updater 改寫。** 程式碼排除：camera ray 只回
   position；follow handle 只指 ghost；real ref 在 `PlaceSlot()` 後沒有 per-frame transform writer。
   使用者也確認 real ref 正常。已提交 real ref 目前不被 placement ray 排除，但它是 stationary
   world surface，命中只會得到穩定點，不構成「自己移動 → 下一 frame 再命中自己」的迴圈。

## 3. Runtime log 查核

實際 log 不在 handoff 所寫的 `~/games/mod-organizer-2-skyrimspecialedition/` 樹內；本次 fresh
window 位於 Proton Documents：

`/home/lorkhan/.local/share/Steam/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/`

- `SKSE/SceneCaptureBridge.log:1-17`：21:44:50 load、21:45:06 data loaded；無 bridge error。
- 同檔 `:24`：co-save 還原 `place physics -> OFF (py0)`；`:28-29` 當時 gh0/off。
- 同檔 `:38-40`：21:48:00 切 place、gh1，只 spawn 一次 movable `Homecooked Meal`；
  `:41-43` 於 21:48:30 因 cell mismatch 清除；沒有 action key／commit／Editor 訊息。
- 同檔 `:44-46`：21:48:38 再切 place、gh1，只 spawn 一次 movable `Honey`；仍沒有反覆
  spawn。Nudge 本身不逐步寫 log，所以「無 input」以使用者現場觀測為準，不從缺 log 外推。
- 同檔 `:47-49`：21:54:34 換成 movable `Honey Nut Treat` ghost；`:50-69` 在約 1.6 秒內
  commit 五個 real refs，每次 log 都是當下 ghost exact pose，五組座標持續改變。這證明 commit
  沒有回頭重新 aim；它如實複製正在漂移的 ghost。使用者現場確認五個 real refs 放下後正常。
- 同檔 `:73-79`：之後換過另一個 movable entry，再換成 STAT
  `BYOHBYOHWRlonghsRINT01`；`:80-83` STAT ghost pose commit 一次。使用者現場確認 STAT ghost
  穩定，與 `HavokMovable()` type gate 分界相符。
- 同檔 `:84-88`：正常離開 mode 並存 co-save；到 window 結束仍無 bridge error。
- `SKSE/skse64.log:329-331`：`SceneCaptureBridge.dll` loaded correctly。
- `Logs/Script/Papyrus.0.log`：沒有 `SceneCaptureBridge`／`SCB` 訊息；有其他 mods 的 property／
  native binding warnings，與本案無直接關聯。
- fresh window 沒有新增 `crash-*.log`；最新真正 crash report 是 2026-08-16。2026-08-25 的
  `SKSE/CrashLogger.log` 是 Crash Logger 自身的 startup log，不是 crash report。

## 4. `a17e460`／`75308c9` git 證據與迴歸判定

### `a17e460 fix: aim placements from rendered camera`

`git show a17e460 -- src/Aim.cpp src/Preview.cpp src/Palette.cpp` 證明：

- 新增 `PlayerIgnoringRayCollector`，唯一 object filter 是
  `if (ref && ref->IsPlayerRef()) return;`；沒有 `Preview::IsGhost()`／sentinel filter。
- ghost spawn 與 per-frame follow 由 `Aim::LookHit()` 改為 `Aim::RenderedCameraHit()`；direct
  placement 同步改。沒有碰 `g_angle`、numpad 或 `Physics::FreezeDeferred()`。
- 舊 `LookHit` 使用 `bhkPickData.rayInput.from/to`，由 engine `PickObject()` 填
  `pick.rayOutput`；新 camera path 改成 `from + delta ray + rayHitCollectorA8` 的 broad collector。
  兩者不是同一 collector/filter path。
- a17e460 註解說是 SmoothCam 同一 broad-ray 入口，但 SmoothCam 的參考實作在
  [`SmoothCam/source/raycast.cpp:6-37`](https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/master/SmoothCam/source/raycast.cpp#L7-L38)
  會先套 `collisionFilterInfo` mask，再套 object filter；`:108-127` 才把 collector 裝到 A8，
  並明確加入 player 3D filter。a17e460 只搬 A8 送法，漏了 collector 自己必須負責的 filtering。

判定：**a17e460 是 self-hit／靠近回饋路徑的引入點**，但不是 `g_angle` 或 freeze race 的引入點；
持續旋轉本體是既有 deferred freeze 提早停止，a17e460 讓同一個未凍 rigid body 又成為每幀 ray hit。

### `75308c9 fix: preserve frozen palette placements`

`git show 75308c9 -- src/Editor.cpp src/Palette.cpp src/Physics.cpp` 證明：

- `Editor::State` 新增 `keepFrozen`；選到 `Palette::PlacedInfoFor()` 且 row 是 `py0` 時，commit／
  cancel 不再 `Physics::Release()`。這只影響**已提交且後來進 Editor 的 real placement**。
- `Palette.cpp` 只在 placement log 加 ref FormID。
- `Physics::HavokMovable()` 只新增 Armor 類型；沒有改 `FreezeDeferred()` 的重試條件，也沒有
  新增／刪除 Preview 的 freeze 呼叫。
- `git blame src/Preview.cpp:170-213` 顯示 ghost 的 `SetCollision(false)` 與
  `Physics::FreezeDeferred(ghost->GetHandle())` 都來自初始匯入 `2cc87c5`，不是 75308c9。

判定：**75308c9 不是本案迴歸點**。它沒有把 freeze「只套在 real placement」；Preview 原本就
呼叫同一共用 freeze。真正缺陷是共用 freeze 以 `Get3D()!=nullptr` 誤當 `SetMotionType` 成功。

## 5. 最小修正方案與驗證

分支：`fix/ghost-ray-self-hit-2026-08-25`（本報告與修正同一交付 commit；exact SHA 見 inbox／
交付訊息）。

- `src/Physics.cpp`：`FreezeDeferred()` 只有在
  `SetMotionType(kKeyframed, false)` 回傳 true 時才停止；3D root 已有但 child body 尚未 ready
  時繼續使用既有 60 次 task retry。沒有改 motion type、movable type gate、retry cap 或 release
  semantics。
- `src/Aim.cpp`／`src/Aim.h`：placement-only A8 collector 除 player 外，明確拒絕
  `Preview::IsGhost(ref)`；這同時涵蓋 live handle 與 `[SCB preview ghost]` sentinel orphan。
  舊 `LookHit`／`RayRef`、marker／pick／editor ray 完全不改；已提交 real ref 保持普通 aim surface。
- `README.md`：同步 Aim／Physics 契約。

離線驗證：

```text
nice -n 19 taskset -c 0-9 cmake --build build/release-clang-cl-linux --parallel 2
=> pass, SceneCaptureBridge.dll linked

nice -n 19 taskset -c 0-9 cmake --build build/tests-native --parallel 2
nice -n 19 taskset -c 0-9 ctest --test-dir build/tests-native --output-on-failure
=> 2/2 pass (catalog_file_tests, modforge_catalog_contract)
```

依硬性限制未啟動／操控 Skyrim、未部署 DLL，所以 runtime 尚待使用者用 movable ghost 在第一人稱、
vanilla 第三人稱、SmoothCam 各靜置至少 10 秒確認「不轉、不向玩家靠近」；STAT、F11 real ref 與既有
15 條 rendered-camera suite 仍需照原清單驗收。
