param([string]$Output = "$PSScriptRoot/../build/obs-quick-record-launcher.exe")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force (Split-Path $Output -Parent) | Out-Null
$compiler = "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
$Output = [IO.Path]::GetFullPath($Output)
$source = Join-Path $PSScriptRoot 'Launcher.cs'
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:Microsoft.CSharp.dll "/out:$Output" $source
if ($LASTEXITCODE) { throw 'Launcher build failed' }
$checkErrorPath = "$Output.check-error.txt"
$check = Start-Process -FilePath $Output -ArgumentList '--check' -PassThru -Wait -WindowStyle Hidden -RedirectStandardError $checkErrorPath
if ($check.ExitCode) { throw ('Launcher check failed: ' + [IO.File]::ReadAllText($checkErrorPath)) }
