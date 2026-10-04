$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '../launcher/setup.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Installer parse failed' }
$copy = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Install-Plugin' }, $true)
if (!$copy) { throw 'Plugin-only copy function missing' }
if ($copy.Extent.Text -match 'LOCALAPPDATA|Startup|Start-Process') { throw 'Elevated copy must not access user install paths or launch processes' }
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
        if ($process.ExitCode -ne 1 -or $output -notmatch 'Installation failed: Extract the complete') {
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
    Install-Plugin $package
    $installed = Join-Path $env:ProgramData 'obs-studio/plugins/obs-quick-record'
    foreach ($relative in 'bin/64bit/obs-quick-record.dll', 'data/locale/ja-JP.ini', 'data/locale/en-US.ini') {
        if ((Get-FileHash -LiteralPath (Join-Path $package $relative)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $installed $relative)).Hash) { throw 'Installed content differs' }
    }
    function Get-Process { param($Name, $ErrorAction) return @{ Id = 1 } }
    $blocked = $false
    try { Install-Plugin $package } catch { $blocked = $_.Exception.Message -like 'Exit OBS*' }
    if (!$blocked) { throw 'Running OBS must prevent plugin replacement' }
    Write-Host 'Plugin-only copy, exact files and active-OBS guard passed'
} finally {
    $env:ProgramData = $oldProgramData
    $resolved = [IO.Path]::GetFullPath($root)
    $temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (!$resolved.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase)) { throw 'Test cleanup path escaped temporary directory' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
