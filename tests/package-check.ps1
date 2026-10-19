param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$expected = @{
    'LICENSE' = "$repo/LICENSE"
    'README.md' = "$repo/README.md"
    '使い方.txt' = "$repo/使い方.txt"
    'OBS Quick RecordとOBS起動アシストをまとめてインストール.cmd' = "$repo/launcher/setup.cmd"
    'OBS Quick RecordとOBS起動アシストをまとめてインストール.ps1' = "$repo/launcher/setup.ps1"
    'OBS起動アシストのみをインストール.ps1' = "$repo/launcher/install.ps1"
    'data/locale/en-US.ini' = "$repo/data/locale/en-US.ini"
    'data/locale/ja-JP.ini' = "$repo/data/locale/ja-JP.ini"
    'bin/64bit/obs-quick-record.dll' = "$BuildDirectory/obs-quick-record.dll"
    'bin/64bit/obs-quick-record-launcher.exe' = "$BuildDirectory/obs-quick-record-launcher.exe"
}
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Archive).Path)
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $seen = @{}
    foreach ($entry in $zip.Entries) {
        $name = $entry.FullName.Replace('\', '/')
        if ($name.EndsWith('/')) { continue }
        if (!$name.StartsWith('obs-quick-record/')) { throw "Unexpected ZIP root: $name" }
        $name = $name.Substring('obs-quick-record/'.Length)
        if (!$expected.ContainsKey($name) -or $seen.ContainsKey($name)) { throw "Unexpected or duplicate package file: $name" }
        $stream = $entry.Open()
        try { $digest = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose() }
        if ($digest -ne (Get-FileHash -LiteralPath $expected[$name] -Algorithm SHA256).Hash) { throw "Package content differs: $name" }
        $seen[$name] = $true
    }
    foreach ($name in $expected.Keys) { if (!$seen.ContainsKey($name)) { throw "Missing package file: $name" } }
    Write-Output 'Package files match sources and build outputs; development and private files excluded.'
} finally { $sha.Dispose(); $zip.Dispose() }
