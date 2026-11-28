# Building and testing

## Build

1. **Fetch submodules**
   ```pwsh
   git submodule update --init --recursive
   ```
2. **Configure, build and test**
   ```pwsh
   cmake -S . -B _build/x64 -G "Visual Studio 17 2022" -A x64
   cmake --build _build/x64 --config Release
   ctest --test-dir _build/x64 -C Release
   ```
   * Win32 build: replace `-A x64` with `-A Win32` and use `_build/Win32`.
   * Tested generators: `Visual Studio 17 2022`, `Visual Studio 18 2026`.
   * The tests download GoogleTest during configuration. Pass `-DXDB_BUILD_TESTS=OFF` to skip them.

3. **Result**
   - `_build/x64/bin/Release/XDB_x64.dll`
   - `_build/Win32/bin/Release/XDB_x86.dll`

## Tests

- `ctest` runs the unit tests: every archive version is written, read back and compared with the
  reference implementation in `third_party/xray_re-tools`, along with damaged archives and the
  7-Zip update paths.
- To also run them against real game archives, set `XDB_TEST_ARCHIVES` to a semicolon-separated
  list of archive paths.
- To check that installed games load the archives the plugin writes, see
  [`tests/game`](../tests/game/README.md).

## Layout

- `CPP/7zip/Archive/Xdb` - plugin sources
  - `XdbHandler.*`, `XdbHandlerOut.cpp` - the 7-Zip handler: listing, extraction, properties, updating
  - `XdbIn.*` - reading archives and detecting their version
  - `XdbUpdate.*`, `XdbOut.*` - writing archives
  - `XdbHeader.*`, `XdbFormat.*` - the file table and on-disk constants
  - `XdbLzhuf.*`, `XdbScrambler.*`, `XdbCodec.*` - compression and the header cipher
  - `XdbRegister.cpp` - format registration and the options for the fork's dialog
- `docs/db-format.md` - description of the archive format
- `tests` - unit tests (GoogleTest); `tests/game` - tests against installed games
- `third_party/7zip` - upstream 7‑Zip sources
- `third_party/lzokay` - LZO1X compression
- `third_party/xray_re-tools` - reference implementation, used only by the tests
