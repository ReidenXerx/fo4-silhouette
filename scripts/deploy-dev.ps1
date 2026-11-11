<#
.SYNOPSIS
  Copies a complete Silhouette build into its Vortex staging folder.

.DESCRIPTION
  Vortex owns Data/, so the dev build is an ordinary Vortex mod, Silhouette-dev.
  Vortex deploys by HARDLINK: copying over a staged file changes the file the game
  reads at once, with no Deploy. So this refuses while the game runs -- it would
  change a running game's Data under it, and the DLL is held open anyway -- and it
  is ONE session's act: whoever holds the game says it is free first.

  What goes in, all of it built or generated, none of it hand-edited:
    build\<Config>\Silhouette.dll (+.pdb)  scripts\build-plugin.ps1
    build\papyrus\Silhouette\*.pex         scripts\build-papyrus.ps1
    data\...                               tools\silhouette_gen.py --write, tools\make_esp.py

  Refused before anything is copied: files the plugin's parser would refuse, a
  compiled player.pex of another generator run, a Silhouette.esp without the refit
  keyword (tools\make_esp.py --check: LooksMenu would move every refit value into
  her own body for good), files the verifier fails (tools\verify_bodygen.py -- it
  reads the built bodies in Data, so a body rebuilt since the generator ran fails
  here too), and a committed manifest edited or deleted (git; run from the
  fo4-silhouette checkout itself).

  Staged IN PLACE, file by file (Copy-Item -Force writes into the existing file and
  keeps its inode): every file Data already links goes live at once, all together.
  Removing and re-copying a folder instead made every restage half-deployed until
  the Deploy: a new esp and DLL beside the old scripts (wave 4). A file an older
  build shipped and this one does not is removed from staging (manifests excepted:
  they are only ever added), and the Deploy takes it out of Data. Every write is
  checked possible BEFORE the first one (a folder where a file goes, a file held
  open elsewhere), since a stop half-way leaves Data holding two builds.

  At the end, whether the Deploy is needed is read from -GameData: each staged file
  must be Data's very file (its hardlink), and nothing Silhouette no longer ships may
  be left there. What waits is listed by name.

  ASCII only. Windows PowerShell 5.1 reads a BOM-less UTF-8 script as ANSI.
#>
[CmdletBinding()]
param(
    [string] $Staging  = 'D:\Vortex\fallout4\mods\Silhouette-dev',
    [string] $Config   = 'Release',
    # Read only: whether each staged file is live there is read from it at the end.
    [string] $GameData = 'D:\SteamFreeGames\Fallout 4 AE\Data'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$game = Get-Process -Name 'Fallout4' -ErrorAction SilentlyContinue
if ($game) {
    throw "Fallout4 is running (PID $($game.Id -join ', ')). Close it before deploying: staged files are hardlinked into Data."
}

$dll  = Join-Path $root "build\$Config\Silhouette.dll"
$pex  = Join-Path $root 'build\papyrus\Silhouette'
$data = Join-Path $root 'data'
foreach ($need in @($dll, (Join-Path $pex 'Bridge.pex'), (Join-Path $pex 'DLL.pex'), (Join-Path $pex 'API.pex'),
                    (Join-Path $pex 'Adopter.pex'), (Join-Path $data 'F4SE\Plugins\Silhouette\catalog.json'),
                    (Join-Path $data 'Silhouette.esp'))) {
    if (-not (Test-Path $need)) { throw "Missing $need - build and generate first." }
}
if (-not (Get-ChildItem $pex -Filter 'player.pex' -ErrorAction SilentlyContinue)) {
    throw "Missing $pex\Player.pex - run scripts\build-papyrus.ps1."
}

# The generated files, read by the plugin's own parser (SilhouetteTests.exe --check): the catalog,
# every manifest, and both BodyGen headers against the catalog's build, stamp and rules hash. What
# the game would refuse at load is refused here.
$tests = Join-Path $root "build\$Config\SilhouetteTests.exe"
if (-not (Test-Path $tests)) { throw "Missing $tests - run scripts\build-plugin.ps1." }
& $tests --check $data
if ($LASTEXITCODE) { throw "the plugin would refuse these generated files - regenerate." }
$catalog = Get-Content (Join-Path $data 'F4SE\Plugins\Silhouette\catalog.json') -Raw | ConvertFrom-Json
# ...and the scripts must be from that run: Player.psc names its build, and so does what was COMPILED
# from it -- the .pex is what ships, and a stale one outlives a regenerated source.
$player = Get-Content (Join-Path $root 'papyrus\Silhouette\Player.psc') -Raw
if ($player -notmatch "Return `"$($catalog.build)`"") {
    throw "papyrus\Silhouette\Player.psc is not build $($catalog.build) - regenerate, then build-papyrus."
}
$pexFile = (Get-ChildItem $pex -Filter 'player.pex').FullName
$pexText = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes($pexFile))
if (-not $pexText.Contains($catalog.build)) {
    throw "$pexFile was not compiled from build $($catalog.build) - run scripts\build-papyrus.ps1."
}
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw "No python: the verifier (tools\verify_bodygen.py) must pass before a deploy." }
# The refit keyword (0x803) must be in the esp that lands, and this is the plain answer why not: first,
# before the verifier (which refuses the same esp among everything else). LooksMenu resolves a keyed
# morph by the plugin's NAME only: a Silhouette.esp without that form turns every refit value into her
# own body at the next load, for good (wave 3 L5-H1) -- an older build's esp restaged is exactly that.
& $python (Join-Path $root 'tools\make_esp.py') --check (Join-Path $data 'Silhouette.esp')
if ($LASTEXITCODE) { throw "data\Silhouette.esp must not be staged - see the line above." }
# The files do what they claim, against the bodies built in Data now. On a failure every line is shown:
# a cut-off tail once hid the one reason that mattered.
$verify = @(& $python (Join-Path $root 'tools\verify_bodygen.py') --dir $data --psc (Join-Path $root 'papyrus\Silhouette\Player.psc'))
if ($LASTEXITCODE) {
    $verify | ForEach-Object { Write-Host $_ }
    throw "tools\verify_bodygen.py fails these files - every line above; regenerate if a body was rebuilt."
}
$verify | Select-Object -Last 3 | ForEach-Object { Write-Host $_ }
# The manifests are history: each says what the bodies of one build are, and saves keep those bodies.
# A committed one edited or deleted misnames bodies no regeneration can repair; a new one is only added.
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $git) { throw "No git: the committed manifests cannot be checked before a deploy." }
# Only from this checkout itself: a copy without .git cannot be compared, and a copy inside another
# repository would be compared with THAT repository's commit and pass whatever it holds.
$ErrorActionPreference = 'Continue'    # git's stderr must not turn into a PowerShell error here
$top = & $git -C $root rev-parse --show-toplevel 2>$null
$topExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($topExit -or -not $top -or ([System.IO.Path]::GetFullPath(($top | Select-Object -First 1).Trim()).TrimEnd('\') -ine
                                  [System.IO.Path]::GetFullPath($root).TrimEnd('\'))) {
    throw "Run this from the fo4-silhouette git checkout: the manifest guard compares data\F4SE\Plugins\Silhouette\manifests with the last commit, and $root is not the top of a git work tree of its own."
}
# Modified or deleted only: a new manifest, staged or not yet, is how a new build is meant to arrive.
# --no-renames: a manifest renamed is one deleted, whatever git would call it.
& $git -C $root diff --quiet --no-renames --diff-filter=MD HEAD -- 'data/F4SE/Plugins/Silhouette/manifests'
switch ($LASTEXITCODE) {
    0 { }
    1 { throw "A committed manifest was edited or deleted (git diff HEAD -- data/F4SE/Plugins/Silhouette/manifests). Restore it: manifests are only ever added." }
    default { throw "git could not compare the manifests with the last commit (exit $LASTEXITCODE)." }
}

# Everything this build ships, staging path -> source. Keys compare in any case, as NTFS names do: a
# staged 'player.pex' IS Player.pex, and must never be taken for a file this build no longer ships.
$ship = New-Object 'System.Collections.Specialized.OrderedDictionary' ([System.StringComparer]::OrdinalIgnoreCase)
$dataFull = (Get-Item -LiteralPath $data).FullName.TrimEnd('\')
foreach ($f in Get-ChildItem -LiteralPath $data -Recurse -File) {
    $ship[$f.FullName.Substring($dataFull.Length).TrimStart('\')] = $f.FullName
}
$ship['F4SE\Plugins\Silhouette.dll'] = $dll
# Beside the DLL, a crash logger names Silhouette's functions instead of offsets.
$pdb = [System.IO.Path]::ChangeExtension($dll, '.pdb')
if (Test-Path $pdb) { $ship['F4SE\Plugins\Silhouette.pdb'] = $pdb }
foreach ($f in Get-ChildItem $pex -Filter *.pex) {
    # The compiler writes some names in lower case; the game does not care, a person reading the folder does.
    $name = if ($f.Name -ieq 'player.pex') { 'Player.pex' } else { $f.Name }
    $ship["Scripts\Silhouette\$name"] = $f.FullName
}
# S-79: the picker window, scripts\build-interface.ps1. F4SE opens it from Interface\ by its name. Missing or older
# than its sources, it is said out loud: this is the build tested in game.
$swf = Join-Path $root 'build\interface\SilhouetteMenu.swf'
if (Test-Path $swf) {
    $ship['Interface\SilhouetteMenu.swf'] = $swf
    $newest = Get-ChildItem (Join-Path $root 'interface\src') -Filter *.as | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($newest.LastWriteTime -gt (Get-Item $swf).LastWriteTime) { Write-Warning "$($newest.Name) is newer than SilhouetteMenu.swf - run scripts\build-interface.ps1." }
} else {
    Write-Warning "No build\interface\SilhouetteMenu.swf: the picker window is not staged - run scripts\build-interface.ps1."
}
# Its pictures, tools\thumbnails.py: one atlas a sex, named by the build, loose in Data\Textures (F4SE mounts only
# loose files). Pictures of another build are staged but the window never mounts them: said out loud.
$drawn = Get-ChildItem (Join-Path $root 'build\textures\Silhouette') -Filter *.dds -ErrorAction SilentlyContinue
foreach ($f in $drawn) {
    $ship["Textures\Silhouette\$($f.Name)"] = $f.FullName
}
if (-not ($drawn | Where-Object { $_.Name -like "*_$($catalog.build).dds" })) {
    Write-Warning "No thumbnails of build $($catalog.build): the window shows no pictures - run python tools\thumbnails.py."
}

# Silhouette's own files in a mod folder or in Data (relative paths) that this build does not ship: only
# Silhouette's own places are looked at, and never a manifest -- an NPC of that build keeps its stamp for the
# rest of the save.
$owned = @('F4SE\Plugins\Silhouette', 'F4SE\Plugins\F4EE\BodyGen\Loose', 'MCM\Config\Silhouette', 'Scripts\Silhouette', 'Textures\Silhouette')
function Get-NotShipped([string] $Base) {
    $out = @()
    if (-not (Test-Path -LiteralPath $Base -PathType Container)) { return $out }
    $baseFull = (Get-Item -LiteralPath $Base).FullName.TrimEnd('\')
    foreach ($dir in $owned) {
        $p = Join-Path $Base $dir
        if (-not (Test-Path -LiteralPath $p -PathType Container)) { continue }
        foreach ($f in Get-ChildItem -LiteralPath $p -Recurse -File) {
            $rel = $f.FullName.Substring($baseFull.Length).TrimStart('\')
            # Vortex marks every folder it deploys into with this file: it is Vortex's, never ours, and a Deploy
            # keeps it (the first real run of the Data check called three of them stale -- wave 6).
            if ($f.Name -ieq '__folder_managed_by_vortex') { continue }
            if ($rel -like 'F4SE\Plugins\Silhouette\manifests\*') { continue }
            if ($rel -like 'F4SE\Plugins\F4EE\BodyGen\Loose\*' -and $f.Name -notlike 'Silhouette_*') { continue }
            if (-not $ship.Contains($rel)) { $out += $rel }
        }
    }
    foreach ($rel in @('F4SE\Plugins\Silhouette.dll', 'F4SE\Plugins\Silhouette.pdb', 'Silhouette.esp')) {
        if (-not $ship.Contains($rel) -and (Test-Path -LiteralPath (Join-Path $Base $rel) -PathType Leaf)) { $out += $rel }
    }
    return $out
}
$stale = @(Get-NotShipped $Staging)

# Refused BEFORE the first write: in place, every write is live in Data at once, so a copy that stops half-way
# leaves the game a new esp and catalog beside an old DLL and scripts (wave 5, lens 2 M4). Every destination's
# folders must be folders, the destination must not be one, and a file there -- or one about to be removed --
# must be free: nothing else may hold it open.
$refused = @()
foreach ($rel in @($ship.Keys) + $stale) {
    $dest = Join-Path $Staging $rel
    $up = Split-Path -Parent $dest
    while ($up -and $up.Length -gt $Staging.TrimEnd('\').Length) {
        if (Test-Path -LiteralPath $up -PathType Leaf) { $refused += "$rel - $up is a file where a folder must be"; break }
        $up = Split-Path -Parent $up
    }
    if (Test-Path -LiteralPath $dest -PathType Container) { $refused += "$rel - a folder stands where the file goes"; continue }
    if (Test-Path -LiteralPath $dest -PathType Leaf) {
        $readOnly = (Get-Item -LiteralPath $dest -Force).IsReadOnly    # Copy-Item -Force writes through that
        try {
            $access = if ($readOnly) { [System.IO.FileAccess]::Read } else { [System.IO.FileAccess]::Write }
            [System.IO.File]::Open($dest, [System.IO.FileMode]::Open, $access, [System.IO.FileShare]::None).Close()
        } catch {
            $why = if ($_.Exception.InnerException) { $_.Exception.InnerException.Message } else { $_.Exception.Message }
            $refused += "$rel - in use or not writable: $why"
        }
    }
}
if ($refused.Count) {
    $refused | ForEach-Object { Write-Host "  $_" }
    throw "Nothing was staged: the files above could not all be written, and a deploy that stops half-way leaves Data holding two builds. Close what holds them (a debugger on the PDB, xEdit or the Creation Kit on the esp, a scanner), fix the folders, and run this again."
}

# In place, file by file: Copy-Item -Force writes INTO an existing file, so its hardlink in Data carries
# the new bytes at once. A file that did not exist has no link in Data until Vortex's Deploy.
New-Item -ItemType Directory -Force -Path $Staging | Out-Null
$new = @()
$written = @()
try {
    foreach ($rel in $ship.Keys) {
        $dest = Join-Path $Staging $rel
        if (-not (Test-Path -LiteralPath $dest)) { $new += $rel }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
        Copy-Item -LiteralPath $ship[$rel] -Destination $dest -Force
        $written += $rel
    }
    # What an older build shipped and this one does not: out of staging (Vortex's Deploy then takes it out of Data).
    foreach ($rel in $stale) {
        Remove-Item -LiteralPath (Join-Path $Staging $rel) -Force
        $written += "$rel (removed)"
    }
} catch {
    Write-Host "Written before the failure:"
    $written | ForEach-Object { Write-Host "  $_" }
    Write-Host "Data now mixes two builds: fix the cause and run this again before starting the game."
    throw
}

# What landed, from the disk: a deploy that silently did nothing looks exactly like one that worked.
foreach ($rel in @('F4SE\Plugins\Silhouette.dll', 'F4SE\Plugins\Silhouette\catalog.json',
                   'F4SE\Plugins\F4EE\BodyGen\Loose\Silhouette_templates.ini', 'MCM\Config\Silhouette\keybinds.json',
                   'Scripts\Silhouette\Bridge.pex', 'Scripts\Silhouette\DLL.pex', 'Scripts\Silhouette\Player.pex',
                   'Silhouette.esp')) {
    $path = Join-Path $Staging $rel
    if (Test-Path $path) {
        $item = Get-Item $path
        Write-Host ("  {0,-60} {1,9} bytes  {2:HH:mm:ss}" -f $rel, $item.Length, $item.LastWriteTime)
    } else {
        Write-Host "  MISSING: $rel"
    }
}
Write-Host "Staged build $($catalog.build) (stamp $($catalog.stamp)) in $Staging, file by file in place."
if ($new.Count) { Write-Host ("  new in staging: " + ($new -join ', ')) }
if ($stale.Count) { Write-Host ("  removed from staging: " + ($stale -join ', ')) }

# Whether Data holds the staged files is read from Data, never from what this run did: a run that listed new
# files, followed by no Deploy, would otherwise tell the next run that nothing waits (wave 5, lens 2 M3). A file
# is live when Data's entry IS the staged file -- the same volume and file id, the hardlink Vortex made.
if (-not ('SilhouetteDeploy.FileId' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace SilhouetteDeploy
{
    public static class FileId
    {
        // BY_HANDLE_FILE_INFORMATION. A FILETIME is two DWORDs: Pack = 4 keeps the longs where Windows puts them.
        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        struct Info
        {
            public uint Attributes;
            public long Created, Accessed, Written;
            public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetFileInformationByHandle(SafeFileHandle handle, out Info info);

        // "volume:index" of a file -- no access asked, every share allowed, so a file held open elsewhere still
        // answers -- or null when there is no such file.
        public static string Of(string path)
        {
            using (SafeFileHandle h = CreateFileW(path, 0, 7, IntPtr.Zero, 3, 0, IntPtr.Zero))
            {
                Info i;
                if (h.IsInvalid || !GetFileInformationByHandle(h, out i)) return null;
                return i.Volume.ToString("X8") + ":" + i.IndexHigh.ToString("X8") + i.IndexLow.ToString("X8");
            }
        }
    }
}
'@
}
if (Test-Path -LiteralPath $GameData -PathType Container) {
    $waits = @()
    foreach ($rel in $ship.Keys) {
        $inData = [SilhouetteDeploy.FileId]::Of((Join-Path $GameData $rel))
        if (-not $inData -or $inData -ne [SilhouetteDeploy.FileId]::Of((Join-Path $Staging $rel))) { $waits += $rel }
    }
    $removes = @(Get-NotShipped $GameData)
    if ($waits.Count -eq 0 -and $removes.Count -eq 0) {
        Write-Host "Every file is live in Data through its hardlink ($GameData): no Deploy is needed."
    } else {
        if ($waits.Count) {
            Write-Host "In Data, not yet the staged file (missing, or another copy) -- waits for Vortex's Deploy:"
            $waits | ForEach-Object { Write-Host "  $_" }
        }
        if ($removes.Count) {
            Write-Host "In Data, and no longer shipped -- Vortex's Deploy removes it:"
            $removes | ForEach-Object { Write-Host "  $_" }
        }
        Write-Host "Press Deploy in Vortex BEFORE starting the game: until then Data is not this build."
    }
} else {
    Write-Host "No game Data at $GameData (-GameData): whether Vortex's Deploy is needed was NOT checked - press Deploy before starting the game."
}
Write-Host "Silhouette.esp must be ENABLED for the bridge."
