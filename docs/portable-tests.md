# Portable tests（MinGW／Linux，非 DLL build）

← [BUILD.md](../BUILD.md)

## Windows（MinGW）

`CatalogFile` 是不依賴 CommonLib/SKSE 的離線 catalog consumer，可在只有 CMake、
vcpkg 與 MinGW 的 Windows 主機編譯測試。這條路徑**只驗 portable parser**，不代表
MinGW 是受支援的 plugin toolchain：

```powershell
$env:PATH='C:\dev\mingw64\bin;' + $env:PATH
$env:VCPKG_ROOT='C:\dev\vcpkg'
cmake -S tests -B build/portable-tests-mingw -G 'MinGW Makefiles' `
  -DCMAKE_MAKE_PROGRAM=C:/dev/mingw64/bin/mingw32-make.exe `
  -DCMAKE_CXX_COMPILER=C:/dev/mingw64/bin/g++.exe `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static `
  -DVCPKG_HOST_TRIPLET=x64-mingw-static
cmake --build build/portable-tests-mingw --parallel
ctest --test-dir build/portable-tests-mingw --output-on-failure
```

測試有自己的 `tests/vcpkg.json`，只安裝 `nlohmann-json`，不會為 portable test 拉
CommonLibSSE/DirectXTK。

## Linux 上跑 portable tests（家用 Manjaro 實際用的兩條）

原生 g++（最快，`docs/favorites.md` 也用這個目錄）：

```bash
: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout}"
cmake -S tests -B build/tests-native -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
```

MinGW 交叉編譯＋wine 執行 `.exe`（驗 Windows ABI 下的 parser）。三個旗標缺一不可：
`CMAKE_SYSTEM_NAME=Windows`；`VCPKG_APPLOCAL_DEPS=OFF`（否則 link 後去叫
`powershell.exe`，exit 127）；靜態連結 libgcc/libstdc++（否則 wine 下找不到
`libstdc++-6.dll`，三條測試全 FAIL）：

```bash
cmake -S tests -B build/portable-tests-mingw -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static -DVCPKG_APPLOCAL_DEPS=OFF \
  "-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++"
cmake --build build/portable-tests-mingw
ctest --test-dir build/portable-tests-mingw --output-on-failure
```

測試 target 為 `catalog_file_tests`、`favorites_tests`。

