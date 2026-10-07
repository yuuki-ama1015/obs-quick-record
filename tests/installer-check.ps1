$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '../launcher/setup.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Installer parse failed' }
$copy = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Install-Plugin' }, $true)
if (!$copy) { throw 'Plugin-only copy function missing' }
if ($copy.Extent.Text -match 'LOCALAPPDATA|Startup|Start-Process') { throw 'Elevated copy must not access user install paths or launch processes' }
foreach ($name in 'Install-Text', 'Assert-Package', 'Invoke-PluginInstall') {
    $function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
    if (!$function) { throw "Missing installer function: $name" }
    Invoke-Expression $function.Extent.Text
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('obs-quick-record-installer-check-' + [guid]::NewGuid())
$oldProgramData = $env:ProgramData
try {
    # Exercise the packaged entry point under Restricted policy without installing anything.
    $entryPackage = Join-Path $root 'entry 日本語'
    New-Item -ItemType Directory -Path $entryPackage -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $entryPackage 'OBS Quick RecordとOBS起動アシストをまとめてインストール.ps1')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../launcher/setup.cmd') -Destination (Join-Path $entryPackage 'setup.cmd')
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $env:ComSpec
    $info.Arguments = '/c setup.cmd'
    $info.WorkingDirectory = $entryPackage
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.EnvironmentVariables['PSExecutionPolicyPreference'] = 'Restricted'
    $process = [Diagnostics.Process]::Start($info)
    try {
        $process.StandardInput.WriteLine('')
        $process.StandardInput.Close()
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000)) { $process.Kill(); throw 'Installer entry timed out' }
        $output = $outputTask.Result + $errorTask.Result
        if ($process.ExitCode -ne 1 -or $output -notmatch 'Missing package file|必要なファイル') {
            throw ('Entry must bypass process policy and report missing package: ' + $output)
        }
    } finally { $process.Dispose() }
    $package = Join-Path $root 'package'
    $env:ProgramData = Join-Path $root 'machine'
    New-Item -ItemType Directory -Path (Join-Path $package 'bin/64bit'), (Join-Path $package 'data/locale') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $package 'bin/64bit/obs-quick-record.dll'), 'test plugin')
    foreach ($language in 'ja-JP', 'en-US') { [IO.File]::WriteAllText((Join-Path $package "data/locale/$language.ini"), $language) }
    # Extract the production function; only process discovery is replaced in this isolated copy check.
    Invoke-Expression $copy.Extent.Text
    function Get-Process { param($Name, $ErrorAction) return $null }
    # A missing locale must be rejected before an existing DLL is overwritten.
    $installed = Join-Path $env:ProgramData 'obs-studio/plugins/obs-quick-record'
    New-Item -ItemType Directory -Path (Join-Path $installed 'bin/64bit') -Force | Out-Null
    $installedDll = Join-Path $installed 'bin/64bit/obs-quick-record.dll'
    [IO.File]::WriteAllText($installedDll, 'previous plugin')
    $localePath = Join-Path $package 'data/locale/ja-JP.ini'
    Remove-Item -LiteralPath $localePath
    $blocked = $false
    try { Install-Plugin $package } catch { $blocked = $_.Exception.Message -like '*ja-JP.ini*' }
    if (!$blocked -or [IO.File]::ReadAllText($installedDll) -ne 'previous plugin') { throw 'Incomplete package changed existing installation' }
    [IO.File]::WriteAllText($localePath, 'ja-JP')
    # Full-package validation also requires the assistant and its installer.
    foreach ($missing in 'bin/64bit/obs-quick-record-launcher.exe', 'OBS起動アシストのみをインストール.ps1') {
        $blocked = $false
        try { Assert-Package $package } catch { $blocked = $_.Exception.Message -like "*$missing*" }
        if (!$blocked) { throw "Missing dependency was accepted: $missing" }
        [IO.File]::WriteAllText((Join-Path $package $missing), 'test')
    }
    Assert-Package $package
    Install-Plugin $package
    $installed = Join-Path $env:ProgramData 'obs-studio/plugins/obs-quick-record'
    foreach ($relative in 'bin/64bit/obs-quick-record.dll', 'data/locale/ja-JP.ini', 'data/locale/en-US.ini') {
        if ((Get-FileHash -LiteralPath (Join-Path $package $relative)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $installed $relative)).Hash) { throw 'Installed content differs' }
    }
    function Get-Process { param($Name, $ErrorAction) return @{ Id = 1 } }
    $blocked = $false
    try { Install-Plugin $package } catch { $blocked = $_.Exception.Message -match 'Exit OBS|OBS.*終了' }
    if (!$blocked) { throw 'Running OBS must prevent plugin replacement' }
    # Substitute only elevation: production wrapper must propagate details and remove the diagnostic file.
    function Start-Process {
        param($FilePath, $Verb, $ArgumentList, $WindowStyle, [switch]$PassThru, [switch]$Wait)
        if ($ArgumentList -notmatch '-ResultFile "([^"]+)"') { throw 'Diagnostic path missing' }
        $script:diagnosticFile = $Matches[1]
        [IO.File]::WriteAllText($script:diagnosticFile, 'test copy failure: locale access denied')
        return @{ ExitCode = 1 }
    }
    $blocked = $false
    try { Invoke-PluginInstall 'test.ps1' } catch { $blocked = $_.Exception.Message -like '*test copy failure: locale access denied*' }
    if (!$blocked -or (Test-Path -LiteralPath $script:diagnosticFile)) { throw 'Copy diagnostics were lost or left behind' }
    function Start-Process {
        param($FilePath, $Verb, $ArgumentList, $WindowStyle, [switch]$PassThru, [switch]$Wait)
        $ArgumentList -match '-ResultFile "([^"]+)"' | Out-Null
        $script:diagnosticFile = $Matches[1]
        throw 'test UAC cancelled'
    }
    try { Invoke-PluginInstall 'test.ps1'; throw 'Cancellation accepted' } catch {
        if ($_.Exception.Message -ne 'test UAC cancelled') { throw }
    }
    if (Test-Path -LiteralPath $script:diagnosticFile) { throw 'Cancelled elevation left diagnostics behind' }
    Write-Host 'Plugin-only copy, exact files and active-OBS guard passed'
} finally {
    $env:ProgramData = $oldProgramData
    $resolved = [IO.Path]::GetFullPath($root)
    $temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (!$resolved.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase)) { throw 'Test cleanup path escaped temporary directory' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
