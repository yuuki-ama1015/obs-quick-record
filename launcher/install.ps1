param([switch]$StartWithWindows)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
if ($admin) { throw 'Run this installer from a normal, non-administrator PowerShell window so OBS startup assistant runs as your user.' }
$source = Join-Path $PSScriptRoot 'bin/64bit/obs-quick-record-launcher.exe'
if (!(Test-Path $source)) { throw 'Run the installer from an extracted plugin package' }
$destination = Join-Path $env:LOCALAPPDATA 'OBSQuickRecordLauncher'
$exe = Join-Path $destination 'obs-quick-record-launcher.exe'
if (Get-Process -Name obs-quick-record-launcher -ErrorAction SilentlyContinue) { throw 'Exit the launcher from its tray menu before updating' }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $exe -Force
if ($StartWithWindows) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Startup')) 'OBS Quick Record Launcher.lnk'))
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = $destination
    $shortcut.Save()
}
Start-Process -FilePath $exe -WindowStyle Hidden
