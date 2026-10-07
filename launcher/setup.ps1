param([switch]$NoStartup, [switch]$PluginOnly, [switch]$NoPause, [string]$ResultFile)
$ErrorActionPreference = 'Stop'
function Install-Text([string]$Japanese, [string]$English) {
    if ((Get-UICulture).TwoLetterISOLanguageName -eq 'ja') { return $Japanese }
    return $English
}
function Assert-Package([string]$PackageRoot, [switch]$PluginOnly) {
    $required = @('bin/64bit/obs-quick-record.dll', 'data/locale/en-US.ini', 'data/locale/ja-JP.ini')
    if (!$PluginOnly) { $required += 'bin/64bit/obs-quick-record-launcher.exe', 'OBS起動アシストのみをインストール.ps1' }
    foreach ($relative in $required) {
        if (!(Test-Path -LiteralPath (Join-Path $PackageRoot $relative) -PathType Leaf)) {
            throw (Install-Text "必要なファイルがありません：$relative。ZIPをすべて展開してください。" "Missing package file: $relative. Extract the complete ZIP before installing.")
        }
    }
}
function Install-Plugin([string]$PackageRoot) {
    Assert-Package $PackageRoot -PluginOnly
    if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
        throw (Install-Text '通知領域のOBSアイコンからOBSを終了してから実行してください。' 'Exit OBS from its tray icon before installing or updating Quick Record.')
    }
    $destination = Join-Path $env:ProgramData 'obs-studio/plugins/obs-quick-record'
    $bin = Join-Path $destination 'bin/64bit'
    $locale = Join-Path $destination 'data/locale'
    New-Item -ItemType Directory -Path $bin, $locale -Force | Out-Null
    foreach ($language in 'en-US', 'ja-JP') {
        Copy-Item -LiteralPath (Join-Path $PackageRoot "data/locale/$language.ini") -Destination $locale -Force
    }
    Copy-Item -LiteralPath (Join-Path $PackageRoot 'bin/64bit/obs-quick-record.dll') -Destination (Join-Path $bin 'obs-quick-record.dll') -Force
}
function Invoke-PluginInstall([string]$ScriptPath) {
    $result = Join-Path ([IO.Path]::GetTempPath()) ('obs-quick-record-install-' + [guid]::NewGuid() + '.txt')
    try {
        [IO.File]::WriteAllText($result, '')
        $powershell = Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
        $child = Start-Process -FilePath $powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$ScriptPath`" -PluginOnly -ResultFile `"$result`"" -WindowStyle Hidden -PassThru -Wait
        if ($child.ExitCode -ne 0) {
            $reason = [IO.File]::ReadAllText($result).Trim()
            if (!$reason) { $reason = Install-Text '管理者側の処理を完了できませんでした。OBSが終了しているか、展開先を読み取れるか確認してください。' 'The elevated copy did not complete. Check that OBS is closed and the extracted package can be read.' }
            throw (Install-Text "プラグインのコピーに失敗しました。起動アシストは導入していません。`n$reason" "Plugin copy failed. Startup assistant was not installed.`n$reason")
        }
    } finally {
        if (Test-Path -LiteralPath $result) { Remove-Item -LiteralPath $result -Force }
    }
}
$exitCode = 0
try {
    Assert-Package $PSScriptRoot -PluginOnly:$PluginOnly
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
    finally { $identity.Dispose() }
    # Elevated child only copies the plugin and reports diagnostics to the caller's temporary file.
    if ($PluginOnly) {
        if (!$admin) { throw (Install-Text 'プラグインのコピーにはWindowsの管理者確認が必要です。' 'The plugin copy requires administrator approval.') }
        Install-Plugin $PSScriptRoot
        exit 0
    }
    if ($admin) { throw (Install-Text '管理者として開いたPowerShellでは実行しないでください。同封の.cmdを通常のダブルクリックで実行してください。' 'Use a normal, non-administrator PowerShell window or double-click the bundled .cmd.') }
    if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) { throw (Install-Text '通知領域のOBSアイコンからOBSを終了してから実行してください。' 'Exit OBS from its tray icon before installing or updating Quick Record.') }
    if (Get-Process -Name obs-quick-record-launcher -ErrorAction SilentlyContinue) {
        throw (Install-Text '通知領域から「OBS起動アシストを終了」を選んでから実行してください。' 'Exit OBS startup assistant from its tray menu before installing or updating.')
    }
    Invoke-PluginInstall $PSCommandPath
    # Continue in the original user's unelevated process, even with alternate UAC credentials.
    & (Join-Path $PSScriptRoot 'OBS起動アシストのみをインストール.ps1') -StartWithWindows:(!$NoStartup) -NoPause
    Write-Host (Install-Text 'OBS Quick RecordとOBS起動アシストを導入しました。次回のOBS起動から使用できます。' 'Quick Record and OBS startup assistant are installed. OBS will load the plugin the next time it starts.')
    if (!$NoStartup) { Write-Host (Install-Text 'Windowsサインイン時のOBS起動アシスト自動起動も登録しました。' 'OBS startup assistant will also start automatically when you sign in to Windows.') }
} catch {
    $exitCode = 1
    $message = $_.Exception.Message
    if ($PluginOnly -and $ResultFile) { [IO.File]::WriteAllText($ResultFile, $message, [Text.UTF8Encoding]::new($true)) }
    Write-Host ((Install-Text 'インストールに失敗しました：' 'Installation failed: ') + $message) -ForegroundColor Red
} finally {
    if (!$PluginOnly -and !$NoPause) { Read-Host (Install-Text 'Enterを押すと閉じます' 'Press Enter to close') | Out-Null }
}
exit $exitCode
