# 部署到 MO2（開發迭代）

← [BUILD.md](../BUILD.md)

```bash
scripts/deploy.sh --dry-run   # 先看會寫哪裡
scripts/deploy.sh             # 建完之後跑這個。不要手打 cp。
```

**🔴 絕不用 `cp` 就地覆寫 `mods/.../SKSE/Plugins/*.dll`**——遊戲跑著的時候這麼做會讓它**無聲暴斃、沒有 crash log**（Linux 不像 Windows 會鎖住載入中的 DLL；`cp` 寫穿同一個 inode，而 DLL 程式碼頁是從該檔 demand-page 進來的）。`deploy.sh` 做兩件事：① 呼叫 `scripts/check_game_running.sh`，**遊戲在跑就拒絕**；② `cp → .tmp` 再 `mv`（`rename(2)` 換 inode，執行中的 mapping 不受影響）。偵測不用 `pgrep -f`——它掃的是完整命令列，agent 驅動時會掃到呼叫端自己而誤判（`[S]kyrimSE\.exe` 括號寫法也擋不住）。**也不能靠 `ps -eo comm=`**：Proton 底下 MO2 的 comm 可為 `main`、SkyrimSE 可為 `Main`，比對 exe 名是保證偽陰性（守門會在該擋的時候放行）。主訊號是掃 `/proc/*/maps` 裡的**映射檔路徑**找 `SkyrimSE.exe`／`skse64_loader.exe`／`ModOrganizer.exe`，外加「目標 DLL 是不是正被 mmap」；映射路徑是核心從 inode 反查的檔名、不是命令列，所以同時免疫自我匹配與 wine 改名。契約測試 `scripts/test_game_detection.sh`（6 條，fixture，不需要真的遊戲）。成因全文見 [dev-env § 部署 SKSE DLL 到 MO2](../../ModForge/workflows/dev-env.md)。

**遊戲用的是帶 esp 的 `mods/SceneCaptureBridge/`**（modpack-main 的 modlist 裡是 `+SceneCaptureBridge`），`deploy.sh` 只寫這一個。目標 mod 資料夾不存在時 `deploy.sh` 報錯 exit 1，**不會**自己建（`mods/` 下多出空目錄會被 MO2 自動加進 modlist）。新 DLL 要**完全關遊戲重開**才吃得到。

`CMakeLists.txt` 也有個 post-build copy（configure 時 `OUTPUT_FOLDER` 有設才啟用；本機的 preset **沒設**，所以是關的）——要用的話它只會寫 `<mods>/SceneCaptureBridge <BuildType>/`，**不會**碰遊戲在用的那個夾。

**不要丟進 MO2 的 `overwrite/`**：那是傾倒區，plugin 擺在那裡幾乎不可見、會被「清空 overwrite」沖掉。用真的 mod 資料夾，開關權留給 MO2 的勾選框。改完 DLL 後 MO2 要 F5 refresh 才看得到新資料夾。
