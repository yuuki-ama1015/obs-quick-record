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
