<#
.SYNOPSIS
  Checks that installed S.T.A.L.K.E.R. games load archives written by the xdb plugin.

.DESCRIPTION
  Each case writes an archive with a marked copy of the game's default_controls.ltx: an
  unknown console command (the engine logs it) followed by "quit". If the engine executes
  the file, the marker appears in its log and the game exits by itself.

    new             a new archive in the folder the game mounts its patches from
    store           the same with compress=off, so the file is stored uncompressed
    edit            one of the game's own archives, edited in place
    order           two healthy archives, A and B; B is mounted after A, so its file
                    must win. This is what the damaged cases rely on.
    damaged-header  a healthy archive (marker A) plus one mounted after it (marker B)
    truncated       whose file table is damaged, or which is cut off in the middle of its
                    file data. Marker B must never be executed, and the game must load A,
                    stop on an error or crash, which the summary tells apart.

  There is no case for damaged file data: X-Ray doesn't check CRCs when it loads a file,
  so it uses the garbled contents, and the result says nothing about the archive.

  Every file a case changes is backed up first and restored afterwards, verified by
  SHA-256. See README.md before running it.
#>
param(
  [Parameter(Mandatory)] [string]$Game,   # a key of the config, or "all"
  [ValidateSet("all", "new", "store", "edit", "order", "damaged-header", "truncated")] [string]$Case = "all",
  [string]$Config,     # default: games.psd1 next to this script
  [string]$Plugin,     # default: the Release build of XDB_x64.dll
  [string]$SevenZip = (Join-Path $env:ProgramFiles "7-Zip"),   # folder with 7z.exe and 7z.dll
  [string]$WorkRoot = (Join-Path $env:TEMP "xdb-game-tests"),
  [int]$TimeoutSec = 45,
  [switch]$Restore   # restore the files of an interrupted run, then exit
)

$ErrorActionPreference = "Stop"
# Windows PowerShell leaves $PSScriptRoot empty in parameter defaults under -File.
if (-not $Config) { $Config = Join-Path $PSScriptRoot "games.psd1" }
if (-not $Plugin) { $Plugin = Join-Path $PSScriptRoot "..\..\_build\x64\bin\Release\XDB_x64.dll" }
$AllCases = @("new", "store", "edit", "order", "damaged-header", "truncated")
$Cp1251 = [Text.Encoding]::GetEncoding(1251)
# the game logs of this run, so they're never mixed up with an earlier one
$LogRoot = Join-Path $WorkRoot ("logs\" + (Get-Date -Format "yyyyMMdd-HHmmss"))

Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class GameWindows {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  // "class|title" of each visible top-level window of a process
  public static List<string> Of(int pid) {
    var r = new List<string>();
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h)) {
        var t = new StringBuilder(256); var c = new StringBuilder(256);
        GetWindowText(h, t, 256); GetClassName(h, c, 256);
        r.Add(c + "|" + t);
      }
      return true; }, IntPtr.Zero);
    return r;
  }
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, StringBuilder l, uint f, uint t, out IntPtr r);
  // The texts inside the titled dialog boxes of a process, one per line. WM_GETTEXT, as
  // GetWindowText can't read the controls of another process.
  public static string DialogText(int pid) {
    var r = new StringBuilder();
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (p == pid && IsWindowVisible(h) && c.ToString() == "#32770" && t.Length > 0) {
        EnumChildWindows(h, (ch, l2) => {
          var s = new StringBuilder(4096); IntPtr res;
          SendMessageTimeout(ch, 0x000D, (IntPtr)4096, s, 2, 1000, out res);
          if (s.Length > 0) r.AppendLine(s.ToString());
          return true; }, IntPtr.Zero);
      }
      return true; }, IntPtr.Zero);
    return r.ToString();
  }
}
"@

# ---- setup ---------------------------------------------------------------------------

function Read-Games {
  if (-not (Test-Path -LiteralPath $Config)) { throw "no $Config; copy games.example.psd1 to games.psd1 and set each Root" }
  $games = Import-PowerShellDataFile -LiteralPath $Config
  foreach ($key in $games.Keys) {
    foreach ($field in "Root", "Exe", "AppData", "Config", "Version", "Edit", "New", "NewB") {
      if (-not $games[$key][$field]) { throw "games.psd1: '$key' has no $field" }
    }
  }
  $games
}

# A private portable 7-Zip with the plugin under test, so an installed plugin is never used.
function New-SevenZip {
  $dir = Join-Path $WorkRoot "7zip"
  foreach ($f in (Join-Path $SevenZip "7z.exe"), (Join-Path $SevenZip "7z.dll"), $Plugin) {
    if (-not (Test-Path -LiteralPath $f)) { throw "not found: $f" }
  }
  New-Item -ItemType Directory -Force (Join-Path $dir "Formats") | Out-Null
  Copy-Item -LiteralPath (Join-Path $SevenZip "7z.exe"), (Join-Path $SevenZip "7z.dll") $dir -Force
  Copy-Item -LiteralPath $Plugin (Join-Path $dir "Formats") -Force
  Join-Path $dir "7z.exe"
}

# ---- backup and restore --------------------------------------------------------------

# Copies everything a case may change into (work) and writes manifest.json there.
function Backup-Files($g, $work) {
  $root = $g.Root
  $files = foreach ($rel in $g.Edit, $g.New, $g.NewB, "$($g.AppData)\user.ltx") {
    $path = Join-Path $root $rel
    $entry = [ordered]@{ Path = $path; Existed = (Test-Path -LiteralPath $path); Hash = $null; Backup = $null }
    if ($entry.Existed) {
      $entry.Hash = (Get-FileHash -LiteralPath $path).Hash
      $entry.Backup = Join-Path $work ("backup_" + [IO.Path]::GetFileName($path))
      Copy-Item -LiteralPath $path $entry.Backup
    }
    [pscustomobject]$entry
  }
  $logDir = Join-Path $root "$($g.AppData)\logs"
  $manifest = [pscustomobject]@{
    Files = @($files); LogDir = $logDir; LogsExisted = (Test-Path -LiteralPath $logDir); LogsBackup = (Join-Path $work "logs")
  }
  if ($manifest.LogsExisted) { Copy-Item -LiteralPath $logDir $manifest.LogsBackup -Recurse }
  $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $work "manifest.json")
  $manifest
}

# Puts every file back as it was; returns $true if all of them are verified.
function Restore-Files($manifest) {
  foreach ($e in $manifest.Files) {
    if ($e.Existed) { Copy-Item -LiteralPath $e.Backup $e.Path -Force }
    elseif (Test-Path -LiteralPath $e.Path) { Remove-Item -LiteralPath $e.Path -Force }
  }
  if (Test-Path -LiteralPath $manifest.LogDir) { Remove-Item -LiteralPath $manifest.LogDir -Recurse -Force }
  if ($manifest.LogsExisted) { Copy-Item -LiteralPath $manifest.LogsBackup $manifest.LogDir -Recurse }

  $ok = $true
  foreach ($e in $manifest.Files) {
    if ($e.Existed -and (Get-FileHash -LiteralPath $e.Path).Hash -ne $e.Hash) { $ok = $false; Write-Warning "restore mismatch: $($e.Path)" }
    if (-not $e.Existed -and (Test-Path -LiteralPath $e.Path)) { $ok = $false; Write-Warning "restore leftover: $($e.Path)" }
  }
  $ok
}

# The engine runs under the name of its exe. An elevated engine can't be killed from a normal
# user process (access denied), so wait for the user to close it: restoring needs the files
# unlocked. RunAsInvoker in games.psd1 avoids this for games whose exe asks for elevation.
function Stop-Engine($g) {
  $name = [IO.Path]::GetFileNameWithoutExtension($g.Exe)
  Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  for ($i = 0; $i -lt 10 -and (Get-Process -Name $name -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }
  if (Get-Process -Name $name -ErrorAction SilentlyContinue) {
    Write-Host "  could not stop $name; close the game to continue"
    while (Get-Process -Name $name -ErrorAction SilentlyContinue) { Start-Sleep 1 }
    Start-Sleep 1
  }
}

# ---- test archives -------------------------------------------------------------------

# The game's default_controls.ltx with (marker) and "quit" appended, staged for 7-Zip.
function New-MarkedConfig($g, $stage, $marker, $original) {
  $file = Join-Path $stage $g.Config
  New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
  [IO.File]::WriteAllText($file, $original.TrimEnd() + "`r`n" + $marker + "`r`nquit`r`n", $Cp1251)
}

function Invoke-SevenZip($stage, [string[]]$arguments) {
  Push-Location -LiteralPath $stage
  try { $out = & $script:Zip @arguments 2>&1 } finally { Pop-Location }
  if (($out -join "`n") -notmatch "Everything is Ok") { throw "7-Zip failed: $out" }
}

function New-TestArchive($g, $target, $marker, $stage, $original, [string[]]$options = @()) {
  New-MarkedConfig $g $stage $marker $original
  New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
  Invoke-SevenZip $stage (@("a", "-txdb", $target, $g.Config, "-mdbver=$($g.Version)") + $options)
}

# Throws unless every file in the archive is stored uncompressed.
function Assert-Stored($target) {
  # folders list an empty method
  $methods = @(& $script:Zip l -slt -txdb $target | Select-String "^Method = ." | ForEach-Object { "$_".Substring(9) })
  if (-not $methods -or @($methods | Where-Object { $_ -ne "Store" })) { throw "not stored: $($methods -join ', ')" }
}

# Replaces the config in one of the game's own archives and adds a file, as two updates.
function Edit-TestArchive($g, $target, $marker, $stage, $original) {
  New-MarkedConfig $g $stage $marker $original
  New-Item -ItemType Directory -Force "$stage\xdb_test" | Out-Null
  Set-Content -LiteralPath "$stage\xdb_test\added.txt" "added by the xdb plugin test" -Encoding ascii
  Invoke-SevenZip $stage @("a", "-txdb", $target, $g.Config)
  Invoke-SevenZip $stage @("a", "-txdb", $target, "xdb_test\added.txt")
}

# @{ Id; Pos; Size } of each chunk. Read here rather than with the plugin, which is under test.
function Get-Chunks($path) {
  $bytes = [IO.File]::ReadAllBytes($path); $pos = 0
  while ($pos + 8 -le $bytes.Length) {
    $size = [BitConverter]::ToUInt32($bytes, $pos + 4)
    [pscustomobject]@{ Id = [BitConverter]::ToUInt32($bytes, $pos); Pos = $pos + 8; Size = $size }
    $pos += 8 + $size
  }
}

# Flips (count) bytes in [start, start + length) of a file, the same ones on every run.
function Edit-Bytes($path, [long]$start, [long]$length, [int]$count) {
  $bytes = [IO.File]::ReadAllBytes($path)
  $rand = New-Object Random 7
  for ($i = 0; $i -lt $count; $i++) { $p = $start + $rand.Next([int]$length); $bytes[$p] = $bytes[$p] -bxor 0x5A }
  [IO.File]::WriteAllBytes($path, $bytes)
}

function Set-Damage($path, $case) {
  $chunks = @(Get-Chunks $path)
  $header = $chunks | Where-Object { ($_.Id -band 0x7FFFFFFF) -eq 1 } | Select-Object -First 1
  $data = $chunks | Where-Object { $_.Id -eq 0 } | Select-Object -First 1
  switch ($case) {
    "damaged-header" { Edit-Bytes $path ($header.Pos + 4) ([Math]::Max(1, $header.Size - 4)) 6 }
    # cut off in the middle of the file data, like an interrupted download: the file table,
    # which comes last, is gone. (A table cut in half can still decode, so that isn't a stable test.)
    "truncated"      { $b = [IO.File]::ReadAllBytes($path); [IO.File]::WriteAllBytes($path, $b[0..([int]($data.Pos + $data.Size / 2) - 1)]) }
  }
}

# ---- the game ------------------------------------------------------------------------

# Starts the game and waits until it exits, logs a fatal error or shows an error window.
function Invoke-Game($g, $logDir) {
  if (Test-Path -LiteralPath $logDir) { Get-ChildItem -LiteralPath $logDir -Filter "xray_*.log" | Remove-Item -Force }
  $cwd = if ($g.Cwd) { Join-Path $g.Root $g.Cwd } else { $g.Root }
  # Windows skips the exe's request for elevation when this is set at launch
  if ($g.RunAsInvoker) { $env:__COMPAT_LAYER = "RunAsInvoker" }
  try { $proc = Start-Process -FilePath (Join-Path $g.Root $g.Exe) -WorkingDirectory $cwd -ArgumentList "-nointro" -PassThru }
  finally { if ($g.RunAsInvoker) { Remove-Item Env:__COMPAT_LAYER -ErrorAction SilentlyContinue } }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $started = Get-Date
  $run = [pscustomobject]@{ StopReason = "timeout"; Seconds = 0; Windows = @{}; Log = ""; ExitCode = $null; DialogText = "" }
  while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
    Start-Sleep -Milliseconds 500
    if ($proc.HasExited) { $run.ExitCode = $proc.ExitCode; $run.StopReason = "exited (code $($proc.ExitCode))"; break }
    $windows = [GameWindows]::Of($proc.Id)
    foreach ($w in $windows) { $run.Windows[$w] = $true }
    # a titled dialog box (the untitled #32770 is the splash screen) or a crash reporter
    $dialog = $windows | Where-Object { $_ -match "^#32770\|.+|error|crash|bugtrap|fatal" } | Select-Object -First 1
    if ($dialog) {
      # BugTrap fills in the error details a moment after its window appears
      for ($i = 0; $i -lt 10; $i++) {
        Start-Sleep -Milliseconds 300
        $run.DialogText = [GameWindows]::DialogText($proc.Id)
        if ($run.DialogText -match "Expression|Description") { break }
      }
      $run.StopReason = "dialog: $dialog"; break
    }
    # Windows' own crash report runs in a process of its own
    $report = @(Get-Process -Name WerFault -ErrorAction SilentlyContinue | Where-Object { try { $_.StartTime -gt $started -and $_.MainWindowTitle } catch { $false } })
    if ($report) {
      $run.StopReason = "Windows crash report"
      $report | Stop-Process -Force -ErrorAction SilentlyContinue
      break
    }
    $log = Get-ChildItem -LiteralPath $logDir -Filter "xray_*.log" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($log -and $log.Length -gt 0 -and (Get-Content -LiteralPath $log.FullName -Raw -ErrorAction SilentlyContinue) -match "FATAL ERROR") {
      $run.StopReason = "fatal error in the log"; break
    }
  }
  $run.Seconds = [Math]::Round($sw.Elapsed.TotalSeconds, 1)
  Stop-Engine $g
  $log = Get-ChildItem -LiteralPath $logDir -Filter "xray_*.log" -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($log) { $run.Log = Get-Content -LiteralPath $log.FullName -Raw }
  $run
}

# The lines from the first FATAL ERROR on, joined, or $null.
function Get-FatalError($log) {
  $lines = $log -split "`n"
  $at = [array]::FindIndex($lines, [Predicate[string]]{ param($l) $l -match "FATAL ERROR" })
  if ($at -lt 0) { return $null }
  ($lines[$at..([Math]::Min($at + 8, $lines.Count - 1))] | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() }) -join " | "
}

# "Expression: ..., Description: ..." from an engine error, or its first line.
function Get-ErrorSummary($text) {
  $parts = foreach ($field in "Expression", "Description", "Arguments") {
    if ($text -match "(?m)^\s*(\[error\])?$field\s*:\s*(.+?)\s*$" -and $Matches[2] -ne "<no expression>") { "${field}: $($Matches[2])" }
  }
  if ($parts) { return $parts -join ", " }
  (($text -split "`n") | Where-Object { $_.Trim() } | Select-Object -First 1).Trim()
}

# How a damaged case ended. Every outcome but the first two means the engine refused the
# damaged archive, which is all these cases can show.
function Get-DamagedOutcome($run, $hasA, $hasB, $fatal) {
  if ($hasB) { return "B executed" }
  if ($run.StopReason -eq "timeout") { return "no reaction" }
  if ($hasA) { return "B ignored, A loaded" }
  $text = "$($run.DialogText)`n$fatal"
  if ($run.ExitCode -eq -1073741819 -or $text -match "access violation|unhandled exception") { return "engine crashed (access violation)" }
  if ($run.StopReason -eq "Windows crash report") { return "engine crashed (Windows crash report)" }
  if ($run.DialogText.Trim() -or $fatal) { return "engine error: " + (Get-ErrorSummary $text.Trim()) }
  if ($run.ExitCode) { return "engine stopped (exit code $($run.ExitCode))" }
  "no reaction"
}

# ---- one case ------------------------------------------------------------------------

function Invoke-Case($key, $g, $case) {
  $work = Join-Path $WorkRoot $key
  if (Test-Path -LiteralPath (Join-Path $work "manifest.json")) {
    throw "an earlier run on '$key' was interrupted; run with -Restore -Game $key first"
  }
  if (Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($g.Exe)) -ErrorAction SilentlyContinue) {
    throw "'$key' is running; close it first"
  }
  foreach ($rel in $g.New, $g.NewB) {
    if (Test-Path -LiteralPath (Join-Path $g.Root $rel)) { throw "$rel already exists in '$key'; refusing to overwrite it" }
  }
  $userLtx = Join-Path $g.Root "$($g.AppData)\user.ltx"
  if ($g.UserLtx -and (Test-Path -LiteralPath $userLtx)) { throw "$userLtx exists; refusing to replace it" }

  if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }   # no manifest: nothing to restore
  New-Item -ItemType Directory -Force $work | Out-Null
  $manifest = Backup-Files $g $work
  try {
    # the game's own config, from the archive the engine takes it from
    & $script:Zip e -txdb "-o$work\orig" (Join-Path $g.Root $g.Edit) $g.Config *> $null
    $origPath = Join-Path "$work\orig" ([IO.Path]::GetFileName($g.Config))
    if (-not (Test-Path -LiteralPath $origPath)) { throw "could not extract $($g.Config) from $($g.Edit)" }
    $original = [IO.File]::ReadAllText($origPath, $Cp1251)

    $id = Get-Random -Maximum 99999
    $markerA = "xdb_plugin_test_${key}_a_$id"   # the healthy archive
    $markerB = "xdb_plugin_test_${key}_b_$id"   # the edited or damaged one
    $detail = @()
    switch ($case) {
      "new"  { New-TestArchive $g (Join-Path $g.Root $g.New) $markerA "$work\stageA" $original }
      "store" {
        New-TestArchive $g (Join-Path $g.Root $g.New) $markerA "$work\stageA" $original @("-mcompress=off")
        Assert-Stored (Join-Path $g.Root $g.New)
      }
      "edit" { Edit-TestArchive $g (Join-Path $g.Root $g.Edit) $markerB "$work\stageB" $original }
      default {   # order and the damaged cases
        New-TestArchive $g (Join-Path $g.Root $g.New) $markerA "$work\stageA" $original
        $second = Join-Path $g.Root $g.NewB
        New-TestArchive $g $second $markerB "$work\stageB" $original
        if ($case -ne "order") { Set-Damage $second $case }
      }
    }
    if ($g.UserLtx) { Set-Content -LiteralPath $userLtx $g.UserLtx -Encoding ascii }

    $run = Invoke-Game $g $manifest.LogDir
    $hasA = $run.Log -match [regex]::Escape($markerA)
    $hasB = $run.Log -match [regex]::Escape($markerB)
    $exited = $run.StopReason -like "exited*"
    $fatal = Get-FatalError $run.Log
    $damaged = $case -eq "damaged-header" -or $case -eq "truncated"
    $outcome = if ($damaged) { Get-DamagedOutcome $run $hasA $hasB $fatal } else { "" }
    $pass = switch ($case) {
      "new"   { $hasA -and $exited }
      "store" { $hasA -and $exited }
      "edit"  { $hasB -and $exited }
      "order" { $hasB -and -not $hasA -and $exited }
      default { $outcome -ne "B executed" -and $outcome -ne "no reaction" }
    }
    $detail += "$($run.StopReason) after $($run.Seconds) s; marker A: $hasA, marker B: $hasB"
    if ($outcome) { $detail += "outcome: $outcome" }
    if ($fatal) { $detail += "log: $fatal" }
    if (-not $pass) { $detail += "windows: " + (($run.Windows.Keys | Sort-Object) -join "; ") }
    if ($run.Log) {
      New-Item -ItemType Directory -Force $LogRoot | Out-Null
      Set-Content -LiteralPath (Join-Path $LogRoot "$key-$case.log") $run.Log
    }
    [pscustomobject]@{ Game = $key; Case = $case; Result = $(if ($pass) { "PASS" } else { "FAIL" }); Outcome = $outcome; Detail = $detail -join "`n    " }
  }
  finally {
    Stop-Engine $g
    if (Restore-Files $manifest) { Remove-Item -LiteralPath $work -Recurse -Force }
    else { Write-Warning "'$key' was NOT fully restored; the backups are in $work" }
  }
}

# ---- main ----------------------------------------------------------------------------

$games = Read-Games
$keys = if ($Game -eq "all") { @($games.Keys | Sort-Object) } else { @($Game) }
foreach ($key in $keys) { if (-not $games.ContainsKey($key)) { throw "no game '$key' in $Config" } }
New-Item -ItemType Directory -Force $WorkRoot | Out-Null

if ($Restore) {
  foreach ($key in $keys) {
    $work = Join-Path $WorkRoot $key
    $file = Join-Path $work "manifest.json"
    if (-not (Test-Path -LiteralPath $file)) { "${key}: nothing to restore"; continue }
    Stop-Engine $games[$key]
    if (Restore-Files (Get-Content -LiteralPath $file -Raw | ConvertFrom-Json)) { Remove-Item -LiteralPath $work -Recurse -Force; "${key}: restored and verified" }
    else { "${key}: NOT fully restored; the backups are in $work" }
  }
  return
}

$script:Zip = New-SevenZip
$cases = if ($Case -eq "all") { $AllCases } else { @($Case) }
$results = @()
foreach ($key in $keys) {
  foreach ($c in $cases) {
    Write-Host "$key / $c"
    try { $r = Invoke-Case $key $games[$key] $c }
    catch {
      # go on with the next case only if this one left the game as it was
      if (Test-Path -LiteralPath (Join-Path $WorkRoot "$key\manifest.json")) { throw }
      $r = [pscustomobject]@{ Game = $key; Case = $c; Result = "ERROR"; Outcome = ""; Detail = "$_" }
    }
    Write-Host "  $($r.Result): $($r.Detail)"
    $results += $r
  }
}
$results | Format-Table Game, Case, Result, Outcome -AutoSize
if (Test-Path -LiteralPath $LogRoot) { "game logs: $LogRoot" }
$failed = @($results | Where-Object { $_.Result -ne "PASS" }).Count
"$($results.Count - $failed) of $($results.Count) passed"
exit $(if ($failed) { 1 } else { 0 })
