# Tests against installed games

`Test-GameArchives.ps1` checks that the real X-Ray engines load archives written by the
plugin. The unit tests can't show that: they only compare the plugin with itself and with
the reference tools. Run it before a release, or after a change to how archives are written.

It needs the games installed and a desktop session, so it is not part of `ctest` or CI.

## How it works

Each case puts a marked copy of the game's `default_controls.ltx` into an archive. The copy
ends with an unknown console command, which the engine logs, and `quit`. If the engine
executes the file, the marker shows up in its log and the game closes by itself.

The damaged cases put the damaged archive after a healthy one, where a healthy archive would
replace the first one's config; `order` checks that this is so. They show how each engine
reacts to a broken archive, not how the plugin writes one: the plugin writes a correct
archive and the script damages it. The summary says how each one ended:

- the damaged archive ignored and the healthy one loaded;
- an engine error, with the expression and description from its dialog or log;
- an engine crash or an exit with an error code.

All of these pass. The damaged contents running, or the game not reacting at all within the
timeout, fail. There is no case for damaged file data: X-Ray doesn't check CRCs when it loads
a file, so it runs whatever the damaged data decompresses to.

| Case | What the plugin writes | Passes when |
|---|---|---|
| `new` | a new archive in the folder the game mounts patches from | the marker is logged and the game quits |
| `store` | the same with `compress=off`, so the file is stored uncompressed | the same |
| `edit` | the config replaced and a file added in one of the game's own archives | the marker is logged and the game quits |
| `order` | two healthy archives, the second mounted after the first | only the second one's marker is logged, and the game quits |
| `damaged-header` | a healthy archive, plus one mounted after it whose file table is damaged | the damaged archive's marker is never logged, and the game reacts |
| `truncated` | the same, cut off in the middle of its file data, so the file table at its end is missing | the same |

## What it changes in a game folder

While a case runs, it:
- adds up to two test archives in the game's patch folder;
- rewrites one of the game's own archives (`edit` only);
- creates `user.ltx` in the game's appdata folder (Clear Sky only);
- replaces the game's logs.

Everything is backed up into the work folder first. Afterwards it is restored and checked
against the original SHA-256 hashes. The script refuses to start if a test archive or the
`user.ltx` it would create already exists.

## Running it

1. Build the plugin (`_build/x64/bin/Release/XDB_x64.dll`) and install 64-bit 7-Zip. The
   script makes its own copy of 7-Zip with the built plugin, so an installed plugin is never
   tested by mistake.
2. Copy `games.example.psd1` to `games.psd1` (ignored by git), set each `Root`, and remove the
   games you don't have.
3. Close the games, then run:
   ```pwsh
   .\tests\game\Test-GameArchives.ps1 -Game soc -Case new
   .\tests\game\Test-GameArchives.ps1 -Game all              # every game, every case
   ```
   Each case takes up to 45 s (`-TimeoutSec`). The script exits with 0 only if every case
   passes. The game logs of each run are kept in `%TEMP%\xdb-game-tests\logs\<date-time>`.

Lost Alpha's exe asks for administrator rights. With `RunAsInvoker = $true` in its entry, the
script starts it without them, so it can stop the game and close its error windows. A game
that still runs elevated can't be stopped by the script; it then waits until you close it.

Other parameters: `-Plugin` (another DLL), `-SevenZip` (the folder with `7z.exe` and
`7z.dll`), `-Config`, `-WorkRoot` (default `%TEMP%\xdb-game-tests`).

## If a run was interrupted

When the console is closed or the PC loses power during a case, the game folder may still
hold the test files. The backups and `manifest.json` stay in `%TEMP%\xdb-game-tests\<game>`,
and the script won't start another case on that game until they are restored:

```pwsh
.\tests\game\Test-GameArchives.ps1 -Game soc -Restore
```
