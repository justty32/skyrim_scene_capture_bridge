# SKSEMenuFramework.h — vendored consumer header

來源：[QTR-Modding/SKSE-Menu-Framework-3](https://github.com/QTR-Modding/SKSE-Menu-Framework-3)
`resources/SKSEMenuFramework.h` @ `71a712dbf8d36bd938b15b4019067c5f4c6c1029`（2026-07-09）

**授權：LGPL-2.1**（見 [LICENSE](LICENSE)）。本 header 是一層 `GetProcAddress` shim——
消費者**不連結**框架 DLL，執行期用 `GetModuleHandleW(L"SKSEMenuFramework")` 動態取
export，所以是動態連結，符合 LGPL。

**為何 vendored 而非 build 時下載**：本 repo 的離線機必須能 build（見
[dev-env.md](../../../ModForge/workflows/dev-env.md)），`file(DOWNLOAD)` 會破壞這一點。

**更新方式**：從 upstream 重抓同一個檔，更新上面的 commit 雜湊。header 自足
（只要 `windows.h` + std，不依賴 CommonLibSSE），沒有其他同步負擔。
