param([string]$Destination = "$PSScriptRoot/../.deps")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Destination | Out-Null
$Destination = (Resolve-Path $Destination).Path
function Fetch-Zip($Url, $File, $Hash, $Directory) {
    if (!(Test-Path $File)) { Invoke-WebRequest $Url -OutFile $File }
    if ((Get-FileHash $File -Algorithm SHA256).Hash -ne $Hash) { throw "SHA256 mismatch: $File" }
    if (!(Test-Path $Directory)) { Expand-Archive $File $Directory }
}
$source = "$Destination/obs-studio"
if (!(Test-Path "$source/.git")) {
    git clone --depth 1 --branch 32.2.2 https://github.com/obsproject/obs-studio.git $source
    if ($LASTEXITCODE) { throw 'OBS source download failed' }
}
$commit = git -C $source rev-parse HEAD
if ($commit -ne 'ba2f32bdf791005443988a4955e963663e16b1ed') { throw 'Unexpected OBS source commit' }
Fetch-Zip 'https://github.com/obsproject/obs-studio/releases/download/32.2.2/OBS-Studio-32.2.2-Windows-x64.zip' "$Destination/obs.zip" '4d6e40e3ab155f56b30de517380566a206d74b63cdf5ad49aa596924768f97e1' "$Destination/runtime"
Fetch-Zip 'https://github.com/obsproject/obs-deps/releases/download/2026-07-15/windows-deps-qt6-2026-07-15-x64.zip' "$Destination/qt.zip" '7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579' "$Destination/qt"
New-Item -ItemType Directory -Force "$Destination/sdk" | Out-Null
foreach ($name in @('obs', 'obs-frontend-api')) {
    $exports = & dumpbin /nologo /exports "$Destination/runtime/bin/64bit/$name.dll"
    if ($LASTEXITCODE) { throw "dumpbin failed: $name" }
    $symbols = $exports | ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $Matches[1] } }
    if (!$symbols) { throw "No exports: $name" }
    @("LIBRARY $name.dll", 'EXPORTS') + $symbols | Set-Content "$Destination/sdk/$name.def" -Encoding ascii
    & lib /nologo /machine:x64 "/def:$Destination/sdk/$name.def" "/out:$Destination/sdk/$name.lib"
    if ($LASTEXITCODE) { throw "Import library failed: $name" }
}
Write-Output "OBS headers: $source"
Write-Output "OBS libraries: $Destination/sdk"
Write-Output "Qt SDK: $Destination/qt"
