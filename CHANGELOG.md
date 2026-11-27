# Changelog

## v1.0.0

A rewrite of the plugin, with tests against every archive version and checks in eight games.

### Breaking changes
- Switches take 7-Zip's spellings: `on`/`off`, `+`/`-`, or the name alone. `true`/`false`
  are no longer accepted.
- Writing to standard output (`-so`) is no longer supported.
- The plugin is now licensed under the GNU GPL v3 or later.

### Additions
- The userdata block of `xdb` archives appears as the file `[userdata].ltx`: edit it with F4,
  replace it by adding or renaming a file to that name, or delete it.
- Progress and Cancel for large archives. Files are processed one at a time, so memory use
  stays low.
- The **Method** column shows `LZO`, `LZHUF` or `Store`, and damaged archives are reported
  as errors.
- Folders get their own records, as the original packer writes them.
- With the [Tosox/7zip](https://github.com/Tosox/7zip) fork, the Add dialog's
  **Format options...** button shows the parameters as regular controls, and the level,
  CPU threads and memory settings, which the plugin ignores, are greyed out. So is
  *Split to volumes*, as the games can't read split archives. For a new archive it offers
  the real choices (`xdb`, Compress) instead of "Keep existing".
- The DLL carries its version and a description in its file properties.

### Changes
- Renaming or copying a file onto a name already in use, or onto a folder's name, is refused
  instead of silently dropping one of the files.
- Errors say what is wrong (a name already in use, a character CP1251 lacks, a file too
  large) instead of "The parameter is incorrect" or "Unspecified error".
- New `xdb` archives get `level_name = single` and an empty `link` in their generated userdata.

### Fixes
- Cyrillic file names are stored in CP1251 on every Windows, not only on Russian ones.
- Converting between versions re-encodes the files; it used to produce broken archives,
  for example from `11xx` to `xdb`.
- `11xx` archives whose files are all compressed are no longer taken for `2215`, and their
  files show their real size.
- `dbver=2945` and `dbver=2215` are accepted.
- Settings from the Add dialog, such as the number of CPU threads, no longer abort archiving.
- Updating archives works in 7-Zip 19.00 to 21.07.
- Damaged archives can no longer crash 7-Zip.

### Known issues
- The xray-xr-7z-plugin registers the same format ID. With both installed, 7-Zip may use
  the wrong one.

## v0.1.0

First release: open, extract and create S.T.A.L.K.E.R. DB archives in 7-Zip, in all six
versions, and edit them in place.
