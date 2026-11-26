<h1 align="center">Xray DB 7z Plugin</h1>

<p align="center">
  Open, edit and create S.T.A.L.K.E.R. game archives directly in 7-Zip.
</p>

<p align="center">
  <a href="https://github.com/Tosox/xray-db-7z-plugin/actions/workflows/build.yml"><img src="https://github.com/Tosox/xray-db-7z-plugin/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="https://github.com/Tosox/xray-db-7z-plugin/blob/master/LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue.svg" alt="License: GPL v3"></a>
  <a href="https://github.com/Tosox/xray-db-7z-plugin/releases"><img src="https://img.shields.io/github/downloads/Tosox/xray-db-7z-plugin/total.svg?label=downloads&color=green" alt="Downloads"></a>
  <img src="https://img.shields.io/badge/platform-Windows-0078D6.svg" alt="Platform: Windows">
</p>

S.T.A.L.K.E.R. stores its game data in `.db` archives, in six different versions across the X-Ray
engine's history. Working with them usually means separate unpacker tools and command lines for
each version. This plugin teaches 7-Zip the format instead: browse and extract any game archive,
edit it in place, convert it between versions, and pack your mod into a new one.

## 📋 Features

- Open, extract, test and create S.T.A.L.K.E.R. DB archives directly in 7-Zip
- All archive versions: `xdb` (Clear Sky, Call of Pripyat, mods), `2947ww` and `2947ru` (Shadow of Chernobyl), `2945`, `2215` and `11xx` (builds 1114–1472)
- Edit archives in place: add, replace, rename and delete files and folders
- Convert archives between versions
- The version is detected from the archive's contents, not its extension
- The userdata block of `xdb` archives appears as the file `[userdata].ltx`
- Progress and cancel for large archives; files are processed one at a time, so memory use stays low

## 📦 Installation

1) Download the latest release zip from GitHub
2) Pick the DLL that matches your 7‑Zip install:  
   - `XDB_x64.dll` for 64‑bit 7‑Zip (most installs)
   - `XDB_x86.dll` only if you explicitly run 32‑bit 7‑Zip
3) Copy the DLL into your 7‑Zip `Formats` directory (e.g., `C:\Program Files\7-Zip\Formats`). Create the `Formats` folder if it isn’t there.  

## 🧭 Usage in 7‑Zip

### Parameters

Type them into the **Parameters** field of the *Add to Archive* dialog, or pass them with `-m` on the command line.
With the [Tosox/7zip](https://github.com/Tosox/7zip) fork, the **Format options...** button next to the field shows them as regular controls.

| Parameter | Values | Default | Meaning |
|-----------|--------|---------|---------|
| `dbver` | `auto`, `xdb`, `2947ww`, `2947ru`, `2945`, `2215`, `11xx` | `auto` | Version to write. `auto` keeps the version of an existing archive and uses `xdb` for new ones. Reading always detects the version. |
| `compress` | `auto`, `on`, `off` | `auto` | Compress new files. `auto` follows the archive being updated (compress if most of its files are compressed); new archives are compressed. |
| `xdb_ud` | path to a file | – | Use the file's contents as the userdata block (`xdb` only). Quote paths with spaces: `xdb_ud="D:\My Mods\userdata.ltx"`. |
| `gen_xdb_ud` | `on`, `off` | `on` | Give new `xdb` archives (and archives converted to `xdb`) a default `[header]` userdata block. |
| `keepdups` | `on`, `off` | `off` | Keep all entries of damaged archives that list the same path more than once. By default only the last one is kept. |

Examples:
- **Parameters** field: `compress=off xdb_ud="D:\Mods\my mod\userdata.ltx"`
- `7z a mymod.db0 .\gamedata\* -txdb`
- `7z a mymod.db0 .\gamedata\* -txdb -mdbver=2947ww -mcompress=off`
- `7z u old_build.db -txdb -mdbver=xdb` converts an archive to `xdb`

### Userdata

`xdb` archives can carry a userdata block, usually an LTX `[header]` section that describes a mod. It appears as `[userdata].ltx` at the top of the archive:
edit it with F4, replace it by adding or renaming a file to that name, rename it to keep its contents as an ordinary file, or delete it to remove the block. An `xdb_ud` parameter always takes precedence.

### What 7-Zip shows

- Archive properties: **Userdata** (the userdata text) and **DB-Format** (the detected version)
- File columns: **Method** is `LZO`, `LZHUF` or `Store`; **CRC** only for versions that store one (`2945` and later)

### Limitations

- Writing to standard output (`-so`) is not supported: the archive's size fields are filled in after the files are written.
- Archives are limited to 4 GiB by the format.
- File names must be representable in the Windows Cyrillic code page (CP1251), like the game expects.
- A file can't share its name with another file or a folder; a rename or copy that would cause this is refused.

## 🛠 Build

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
   * To also run the tests against real game archives, set `XDB_TEST_ARCHIVES` to a semicolon-separated list of archive paths.

3. **Result**
   - `_build/x64/bin/Release/XDB_x64.dll`
   - `_build/Win32/bin/Release/XDB_x86.dll`

## 📂 Layout

- `CPP/7zip/Archive/Xdb` - plugin sources
  - `XdbHandler.*`, `XdbHandlerOut.cpp` - the 7-Zip handler: listing, extraction, properties, updating
  - `XdbIn.*` - reading archives and detecting their version
  - `XdbUpdate.*`, `XdbOut.*` - writing archives
  - `XdbHeader.*`, `XdbFormat.*` - the file table and on-disk constants
  - `XdbLzhuf.*`, `XdbScrambler.*`, `XdbCodec.*` - compression and the header cipher
  - `XdbRegister.cpp` - format registration
- `docs/db-format.md` - description of the archive format
- `tests` - unit tests (GoogleTest)
- `third_party/7zip` - upstream 7‑Zip sources
- `third_party/lzokay` - LZO1X compression
- `third_party/xray_re-tools` - reference implementation, used only by the tests

## 📄 License

* Plugin-specific code: GNU GPL v3 or later (see `LICENSE`)
* Third-party code keeps its own licenses (see `NOTICE`)

---

<p align="center">
  <em>🤖 Built with substantial help from Claude Code · verified by the <a href="tests">unit tests</a> and against real game archives</em>
</p>
