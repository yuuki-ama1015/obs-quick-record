param([switch]$StartWithWindows, [switch]$NoPause)
$ErrorActionPreference = 'Stop'
function Install-Text([string]$Japanese, [string]$English) {
    if ((Get-UICulture).TwoLetterISOLanguageName -eq 'ja') { return $Japanese }
    return $English
}
try {
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
if ($admin) { throw (Install-Text '管理者として開いていない通常のPowerShellから実行してください。' 'Run this installer from a normal, non-administrator PowerShell window so OBS startup assistant runs as your user.') }
$source = Join-Path $PSScriptRoot 'bin/64bit/obs-quick-record-launcher.exe'
if (!(Test-Path $source)) { throw (Install-Text 'ZIPをすべて展開したフォルダーから実行してください。' 'Run the installer from an extracted plugin package') }
$destination = Join-Path $env:LOCALAPPDATA 'OBSQuickRecordLauncher'
$exe = Join-Path $destination 'obs-quick-record-launcher.exe'
if (Get-Process -Name obs-quick-record-launcher -ErrorAction SilentlyContinue) { throw (Install-Text '通知領域からOBS起動アシストを終了してから更新してください。' 'Exit the launcher from its tray menu before updating') }
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

Write-Host (Install-Text 'OBS起動アシストを導入しました。' 'OBS startup assistant is installed.')
} catch {
    if ($NoPause) { throw }
    Write-Host ((Install-Text 'インストールに失敗しました：' 'Installation failed: ') + $_.Exception.Message) -ForegroundColor Red
    Read-Host (Install-Text 'Enterを押すと閉じます' 'Press Enter to close') | Out-Null
    exit 1
}
if (!$NoPause) { Read-Host (Install-Text 'Enterを押すと閉じます' 'Press Enter to close') | Out-Null }
