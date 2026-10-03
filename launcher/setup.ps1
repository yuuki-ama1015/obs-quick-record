param([switch]$NoStartup)
$ErrorActionPreference = 'Stop'

$pluginSource = Join-Path $PSScriptRoot 'bin/64bit/obs-quick-record.dll'
$launcherSource = Join-Path $PSScriptRoot 'bin/64bit/obs-quick-record-launcher.exe'
if (!(Test-Path -LiteralPath $pluginSource) -or !(Test-Path -LiteralPath $launcherSource)) {
    throw 'Extract the complete obs-quick-record package before running setup.ps1.'
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $script = '"' + $PSCommandPath + '"'
    $installerArguments = "-NoProfile -ExecutionPolicy Bypass -NoExit -File $script"
    if ($NoStartup) { $installerArguments += ' -NoStartup' }
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $installerArguments
    exit
}

if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
    throw 'Exit OBS from its tray icon before installing or updating Quick Record.'
}
if (Get-Process -Name obs-quick-record-launcher -ErrorAction SilentlyContinue) {
    throw 'Exit Quick Record Launcher from its tray menu before installing or updating.'
}

$pluginDestination = Join-Path $env:ProgramData 'obs-studio/plugins/obs-quick-record'
$pluginBin = Join-Path $pluginDestination 'bin/64bit'
$locale = Join-Path $pluginDestination 'data/locale'
$launcherDestination = Join-Path $env:LOCALAPPDATA 'OBSQuickRecordLauncher'
$launcherExe = Join-Path $launcherDestination 'obs-quick-record-launcher.exe'
New-Item -ItemType Directory -Path $pluginBin, $locale, $launcherDestination -Force | Out-Null
Copy-Item -LiteralPath $pluginSource -Destination (Join-Path $pluginBin 'obs-quick-record.dll') -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'data/locale/*.ini') -Destination $locale -Force
Copy-Item -LiteralPath $launcherSource -Destination $launcherExe -Force

if (!$NoStartup) {
    $startup = [Environment]::GetFolderPath([Environment+SpecialFolder]::Startup)
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $startup 'OBS Quick Record Launcher.lnk'))
    $shortcut.TargetPath = $launcherExe
    $shortcut.WorkingDirectory = $launcherDestination
    $shortcut.Save()
}

Start-Process -FilePath $launcherExe -WindowStyle Hidden
Write-Host 'Quick Record and its Launcher are installed. OBS will load the plugin the next time it starts.'
if (!$NoStartup) { Write-Host 'The Launcher will also start automatically when you sign in to Windows.' }
