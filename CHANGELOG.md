# Changelog

## v1.0.0

A rewrite of the plugin, tested against every archive version and in eight games.
To upgrade, replace the DLL in 7-Zip's `Formats` folder.

### Breaking changes
- On/off options take `on`/`off` (or `+`/`-`) like 7-Zip's own; `true`/`false` no longer work.
- Writing an archive to standard output (`-so`) is no longer supported.
- The plugin is now licensed under the GNU GPL v3 or later.

### Added
- The userdata of `xdb` archives appears as `[userdata].ltx`: edit it like any file, replace
  it, or delete it.
- With the [Tosox/7zip](https://github.com/Tosox/7zip) fork, **Format options...** in the Add
  dialog shows the plugin's options as dropdowns, and settings that don't apply to game
  archives are greyed out.
- Large archives show progress, can be cancelled and use little memory.
- The **Method** column shows how each file is stored: `LZO`, `LZHUF` or `Store`.
- Empty folders are kept, as folders get their own entries like in the game's archives.
- The DLL shows its version in its file properties.

### Changed
- Renaming or copying a file onto a name that's already taken is refused, instead of one of
  the files being dropped.
- Errors say what's wrong, for example a name already in use or a character the game can't
  store, instead of "The parameter is incorrect".
- New `xdb` archives get `level_name = single` and an empty `link` in their userdata.

### Fixed
- Damaged archives are reported as errors and can no longer crash 7-Zip.
- Cyrillic file names are stored correctly on every Windows, not only on Russian ones.
- Names with accented Latin letters such as `ü` or `é` are refused, instead of being stored
  as `u` or `e` without a word.
- The plugin no longer adds a second "Copy" codec and "CRC32" hasher to 7-Zip.
- Converting between versions no longer produces broken archives, for example from `11xx`
  to `xdb`.
- `11xx` archives whose files are all compressed open as `11xx`, not `2215`, and show the
  real file sizes.
- `dbver=2945` and `dbver=2215` are accepted.
- Settings in the Add dialog, such as the number of CPU threads, no longer stop archiving.
- Adding to and editing archives works in 7-Zip 19.00 to 21.07.

## v0.1.0

First release: open, extract and create S.T.A.L.K.E.R. DB archives in 7-Zip, in all six
versions, and edit them in place.
