# Games for Test-GameArchives.ps1. Copy this file to games.psd1 and set each Root; remove the
# games you don't have. The other paths are relative to Root and describe the game's layout:
#
#   Exe      the engine                       Cwd      its working directory ('' = Root)
#   AppData  where it writes logs and user.ltx
#   Config   default_controls.ltx inside the game's archives, the file the test marks
#   Version  dbver for the test archives
#   Edit     one of the game's own archives that holds Config (the "edit" case changes it)
#   New      a new archive the game mounts after its own; NewB is mounted after New
#   UserLtx  optional: written to AppData\user.ltx for the run
#   RunAsInvoker  optional: start the game without the administrator rights its exe asks for
@{
  soc = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Shadow of Chernobyl'
    Exe = 'bin\XR_3DA.exe'; Cwd = 'bin'; AppData = 'bin\appdata'
    Config = 'config\default_controls.ltx'; Version = '2947ww'
    Edit = 'gamedata.dbb'; New = 'gamedata.dbe'; NewB = 'gamedata.dbf'
  }
  cs = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Clear Sky'
    Exe = 'bin\xrEngine.exe'; Cwd = ''; AppData = 'appdata'
    Config = 'configs\default_controls.ltx'; Version = 'xdb'
    Edit = 'resources\configs.db'; New = 'patches\xpatch_zz_xdbtest_a.db'; NewB = 'patches\xpatch_zz_xdbtest_b.db'
    # Clear Sky only runs default_controls.ltx at startup when user.ltx says so
    UserLtx = 'default_controls'
  }
  cop = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Call of Pripyat'
    Exe = 'bin\xrEngine.exe'; Cwd = ''; AppData = 'appdata'
    Config = 'configs\default_controls.ltx'; Version = 'xdb'
    Edit = 'resources\configs.db'; New = 'patches\xpatch_zz_xdbtest_a.db'; NewB = 'patches\xpatch_zz_xdbtest_b.db'
  }
  # Lost Alpha's exe asks for administrator rights; started elevated, the script couldn't stop it
  la = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Lost Alpha DC'
    Exe = 'bins\XR_3DA.exe'; Cwd = ''; AppData = 'appdata'
    Config = 'config\default_controls.ltx'; Version = '2947ww'
    Edit = 'gamedata.db0'; New = 'gamedata.db3'; NewB = 'gamedata.db4'
    RunAsInvoker = $true
  }
  gamma = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Gamma'
    Exe = 'bin\AnomalyDX11.exe'; Cwd = ''; AppData = 'appdata'
    Config = 'configs\default_controls.ltx'; Version = 'xdb'
    Edit = 'db\configs\configs.db0'; New = 'db\mods\zzz_xdbtest_a.db0'; NewB = 'db\mods\zzz_xdbtest_b.db0'
  }
  rttn = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Road To The North'
    Exe = 'bin\xrEngine.exe'; Cwd = ''; AppData = 'appdata'
    Config = 'configs\default_controls.ltx'; Version = 'xdb'
    Edit = 'database\config\configs.db'; New = 'database\patch\zz_xdbtest_a.db'; NewB = 'database\patch\zz_xdbtest_b.db'
  }
  # default_controls.ltx is in configs.xdb0 and xtra.xdb0; the engine uses the later one
  deadair = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Dead Air'
    Exe = 'xrEngine.exe'; Cwd = ''; AppData = 'appdata'
    Config = 'configs\default_controls.ltx'; Version = 'xdb'
    Edit = 'database\xtra.xdb0'; New = 'database\zz_xdbtest_a.xdb0'; NewB = 'database\zz_xdbtest_b.xdb0'
  }
  cotz = @{
    Root = 'C:\Games\S.T.A.L.K.E.R. Call of The Zone'
    Exe = 'bin\xrEngine.exe'; Cwd = ''; AppData = 'appdata'
    Config = 'configs\default_controls.ltx'; Version = 'xdb'
    Edit = 'database\configs\configs.db'; New = 'database\patchs\zz_xdbtest_a.db'; NewB = 'database\patchs\zz_xdbtest_b.db'
  }
}
