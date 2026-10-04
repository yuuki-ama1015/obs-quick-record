param([switch]$NoStartup, [switch]$PluginOnly, [switch]$NoPause)
$ErrorActionPreference = 'Stop'
$exitCode = 0
try {

$pluginSource = Join-Path $PSScriptRoot 'bin/64bit/obs-quick-record.dll'
$launcherSource = Join-Path $PSScriptRoot 'bin/64bit/obs-quick-record-launcher.exe'
if (!(Test-Path -LiteralPath $pluginSource) -or !(Test-Path -LiteralPath $launcherSource)) {
    throw 'Extract the complete obs-quick-record package before running this installer.'
}

function Install-Plugin {
    param([string]$PackageRoot)
    if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
        throw 'Exit OBS from its tray icon before installing or updating Quick Record.'
    }
    $destination = Join-Path $env:ProgramData 'obs-studio/plugins/obs-quick-record'
    $bin = Join-Path $destination 'bin/64bit'
    $locale = Join-Path $destination 'data/locale'
    New-Item -ItemType Directory -Path $bin, $locale -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PackageRoot 'bin/64bit/obs-quick-record.dll') -Destination (Join-Path $bin 'obs-quick-record.dll') -Force
    foreach ($language in 'en-US', 'ja-JP') {
        Copy-Item -LiteralPath (Join-Path $PackageRoot "data/locale/$language.ini") -Destination $locale -Force
    }
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
# Only the machine-wide plugin copy runs elevated; no user paths or process launches here.
if ($PluginOnly) {
    if (!$admin) { throw 'The plugin copy requires administrator approval.' }
    Install-Plugin $PSScriptRoot
    exit 0
}
if ($admin) { throw 'Run this installer from a normal, non-administrator PowerShell window. It requests approval for the plugin copy only.' }
if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) { throw 'Exit OBS from its tray icon before installing or updating Quick Record.' }
if (Get-Process -Name obs-quick-record-launcher -ErrorAction SilentlyContinue) {
    throw 'Exit OBS startup assistant from its tray menu before installing or updating.'
}

$powershell = Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
$child = Start-Process -FilePath $powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -PluginOnly" -WindowStyle Hidden -PassThru -Wait
if ($child.ExitCode -ne 0) { throw 'Quick Record plugin installation failed. OBS startup assistant was not installed or started.' }
# Continue in the original user's unelevated process, even with alternate UAC credentials.
& (Join-Path $PSScriptRoot 'OBS起動アシストのみをインストール.ps1') -StartWithWindows:(!$NoStartup) -NoPause
Write-Host 'Quick Record and OBS startup assistant are installed. OBS will load the plugin the next time it starts.'
if (!$NoStartup) { Write-Host 'OBS startup assistant will also start automatically when you sign in to Windows.' }

} catch {
    $exitCode = 1
    Write-Host ('Installation failed: ' + $_.Exception.Message) -ForegroundColor Red
} finally {
    if (!$PluginOnly -and !$NoPause) { Read-Host 'Press Enter to close' | Out-Null }
}
exit $exitCode
