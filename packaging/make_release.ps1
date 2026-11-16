# SPDX-License-Identifier: GPL-2.0-or-later
# Packs dist\ into release\TriforceRecompiled-v<Version>-windows-x64.zip (+ symbols, checksums).
# Called by publish_release.bat.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    # Where the exe/pdb are taken from (default: dist\). Useful while the game is running
    # and dist\TriforceRecompiled.exe is locked.
    [string]$DistDir = 'dist'
)
$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot
$dist  = if ([System.IO.Path]::IsPathRooted($DistDir)) { $DistDir } else { Join-Path $root $DistDir }
$out   = Join-Path $root 'release'
$name  = "TriforceRecompiled-v$Version-windows-x64"
$stage = Join-Path $out $name

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null

foreach ($f in 'TriforceRecompiled.exe', 'steam_api64.dll') {
    $p = Join-Path $dist $f
    if (-not (Test-Path $p)) { throw "dist\$f is missing - run setup_game.bat first." }
    Copy-Item $p $stage
}

# Visual C++ runtime next to the exe (app-local), so players don't need the redistributable.
$crt = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        $redist = Join-Path $vs 'VC\Redist\MSVC'
        $crt = Get-ChildItem $redist -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d' } | Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue } |
            Select-Object -First 1
    }
}
if ($crt) {
    Copy-Item (Join-Path $crt.FullName '*.dll') $stage
    Write-Host "VC++ runtime: $($crt.FullName)"
} else {
    Write-Warning 'VC++ runtime DLLs not found - players will need the Visual C++ Redistributable.'
}

# Documentation and licenses
Copy-Item (Join-Path $PSScriptRoot 'README.txt') $stage
Copy-Item (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.txt') $stage
Copy-Item (Join-Path $root 'COPYING') (Join-Path $stage 'COPYING-GPL2.txt')
$gpl3 = Join-Path $root 'external\azahar\LICENSE.txt'
if (-not (Test-Path $gpl3)) { throw 'external\azahar\LICENSE.txt is missing - run setup_game.bat first.' }
Copy-Item $gpl3 (Join-Path $stage 'LICENSE-GPL3.txt')

# Safety net: never ship game data or keys.
$bad = Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Extension -in '.cia', '.3ds', '.cci', '.cxi', '.app', '.bin', '.cro', '.zip' -or $_.Name -match 'key|aes'
}
if ($bad) { throw "Refusing to package: $($bad.Name -join ', ')" }

$zip = Join-Path $out "$name.zip"
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Package: $zip"

$pdb = Join-Path $dist 'TriforceRecompiled.pdb'
$assets = @($zip)
if (Test-Path $pdb) {
    $sym = Join-Path $out "TriforceRecompiled-v$Version-symbols.zip"
    Compress-Archive -Path $pdb -DestinationPath $sym -CompressionLevel Optimal
    $assets += $sym
    Write-Host "Symbols: $sym"
}

$sums = foreach ($a in $assets) { '{0}  {1}' -f (Get-FileHash $a -Algorithm SHA256).Hash.ToLower(), (Split-Path $a -Leaf) }
Set-Content -Path (Join-Path $out 'SHA256SUMS.txt') -Value $sums -Encoding ascii
Get-ChildItem $stage | Format-Table Name, Length -AutoSize | Out-String | Write-Host
