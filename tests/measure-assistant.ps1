[CmdletBinding(DefaultParameterSetName = 'Measure')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Measure')][string]$NativeExe,
    [Parameter(Mandatory, ParameterSetName = 'Prepare')][switch]$PrepareOnly,
    [ValidateRange(3, 3600)][int]$Seconds = 60,
    [string]$OutputDirectory = "$PSScriptRoot/../build-benchmark"
)
$ErrorActionPreference = 'Stop'
if (!$PrepareOnly) { $NativeExe = (Resolve-Path -LiteralPath $NativeExe).Path }
$root = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($root) | Out-Null
$sourcePath = "$PSScriptRoot/../launcher/Launcher.cs"
$source = [IO.File]::ReadAllText($sourcePath).Replace("`r`n", "`n")
# Compile an isolated copy of the previous implementation. No real hotkeys,
# tray icon, user configuration, Startup link or installed OBS process is touched.
function Replace-Once([string]$Old, [string]$New) {
    if (!$script:source.Contains($Old)) { throw "Benchmark baseline changed: $Old" }
    $script:source = $script:source.Replace($Old, $New)
}
Replace-Once '[DllImport("user32.dll", SetLastError = true)] static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint key);' 'static bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint key) { return true; }'
Replace-Once '[DllImport("user32.dll")] static extern bool UnregisterHotKey(IntPtr hwnd, int id);' 'static bool UnregisterHotKey(IntPtr hwnd, int id) { return true; }'
Replace-Once 'readonly string config = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "obs-studio", "plugin_config", "obs-quick-record");' 'static string benchmarkDirectory; readonly string config = benchmarkDirectory;'
Replace-Once 'icon.Visible = true;' 'icon.Visible = false;'
Replace-Once 'Process.GetProcessesByName("obs64")' 'Process.GetProcessesByName("obs-qr-legacy-qa")'
$entry = @'
    [STAThread] static int Main(string[] args)
    {
        if (args.Length == 3 && args[0] == "--benchmark") {
            benchmarkDirectory = Path.Combine(Path.GetTempPath(), "obs-quick-record-legacy-benchmark-" + Guid.NewGuid());
            Directory.CreateDirectory(benchmarkDirectory);
            try {
                File.WriteAllText(Path.Combine(benchmarkDirectory, "settings.json"), "{\"launcherModifiers\":1,\"launcherVirtualKey\":82}");
                Application.EnableVisualStyles();
                var watch = Stopwatch.StartNew();
                var launcher = new Launcher();
                using (var finish = new System.Windows.Forms.Timer { Interval = Int32.Parse(args[1]) * 1000 }) {
                    finish.Tick += delegate {
                        using (var process = Process.GetCurrentProcess()) {
                            process.Refresh();
                            File.WriteAllText(args[2], new JavaScriptSerializer().Serialize(new {
                                elapsedMs = watch.ElapsedMilliseconds, cpu100ns = process.TotalProcessorTime.Ticks,
                                workingSet = process.WorkingSet64, privateBytes = process.PrivateMemorySize64
                            }));
                        }
                        finish.Stop(); launcher.ExitThread();
                    };
                    finish.Start(); File.WriteAllText(args[2] + ".ready", "ready"); Application.Run(launcher);
                }
            } finally { Directory.Delete(benchmarkDirectory, true); }
            return 0;
        }
'@
Replace-Once "    [STAThread] static int Main(string[] args)`n    {" $entry
$baselineSource = Join-Path $root 'legacy-benchmark.cs'
[IO.File]::WriteAllText($baselineSource, $source, [Text.UTF8Encoding]::new($true))
$baselineExe = Join-Path $root 'legacy-benchmark.exe'
$compiler = "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe"
$icon = [IO.Path]::GetFullPath("$PSScriptRoot/../launcher/assets/obs-startup-assistant.ico")
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:Microsoft.CSharp.dll "/win32icon:$icon" "/resource:$icon,obs-startup-assistant.ico" "/out:$baselineExe" $baselineSource
if ($LASTEXITCODE) { throw 'Cannot compile isolated baseline' }
if ($PrepareOnly) { Write-Output $baselineExe; return }
$children = @()
try {
    foreach ($kind in @('legacy', 'native')) {
        $exe = if ($kind -eq 'legacy') { $baselineExe } else { $NativeExe }
        $report = Join-Path $root "$kind.json"
        Remove-Item -LiteralPath $report, "$report.ready" -Force -ErrorAction SilentlyContinue
        $child = Start-Process -FilePath $exe -ArgumentList @('--benchmark', $Seconds, "`"$report`"") -WindowStyle Hidden -PassThru -RedirectStandardError "$root/$kind.stderr.txt"
        $children += $child
    }
    foreach ($child in $children) {
        if (!$child.WaitForExit(($Seconds + 20) * 1000)) { throw 'Benchmark child timed out' }
        if ($child.ExitCode -ne 0) { throw "Benchmark failed: inspect $root/*.stderr.txt" }
    }
    $legacy = Get-Content -LiteralPath "$root/legacy.json" -Raw | ConvertFrom-Json
    $native = Get-Content -LiteralPath "$root/native.json" -Raw | ConvertFrom-Json
    foreach ($kind in @('legacy', 'native')) {
        $value = if ($kind -eq 'legacy') { $legacy } else { $native }
        [pscustomobject]@{ Implementation = $kind; Seconds = $value.elapsedMs / 1000; CpuMilliseconds = $value.cpu100ns / 10000; WorkingSetMiB = [Math]::Round($value.workingSet / 1MB, 2); PrivateMiB = [Math]::Round($value.privateBytes / 1MB, 2) }
    }
} finally {
    foreach ($child in $children) {
        if (!$child.HasExited) { $child.Kill(); $child.WaitForExit() } # Only child processes created above.
        $child.Dispose()
    }
}
