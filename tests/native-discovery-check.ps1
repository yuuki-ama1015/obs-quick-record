param([Parameter(Mandatory)][string]$NativeExe)
$ErrorActionPreference = 'Stop'
$NativeExe = (Resolve-Path -LiteralPath $NativeExe).Path
$buildDirectory = Split-Path -Parent $NativeExe
$root = [IO.Path]::GetFullPath((Join-Path $buildDirectory ('discovery-check-' + [guid]::NewGuid())))
if (![IO.Path]::GetDirectoryName($root).Equals($buildDirectory, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected test directory' }
[IO.Directory]::CreateDirectory($root) | Out-Null
$fixtureExe = Join-Path $root 'obs-qr-native-qa.exe'
$children = [Collections.Generic.List[Diagnostics.Process]]::new()
function Start-Child([string]$Exe, [string[]]$Arguments) {
    $child = Start-Process -FilePath $Exe -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    $children.Add($child)
    return $child
}
function Wait-Ready([Diagnostics.Process]$Child, [string]$Path) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (!(Test-Path -LiteralPath $Path)) {
        if ($Child.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw "Test child not ready: $Path" }
        Start-Sleep -Milliseconds 20
    }
}
function Start-Observer([string]$Name, [int]$Seconds, [switch]$Polling) {
    $report = Join-Path $root "$Name.json"
    $mode = if ($Polling) { '--benchmark-fallback' } else { '--benchmark' }
    $child = Start-Child $NativeExe @($mode, $Seconds, "`"$report`"")
    Wait-Ready $child "$report.ready"
    return [pscustomobject]@{ Child = $child; Report = $report }
}
function Read-Report($Observer) {
    if (!$Observer.Child.WaitForExit(25000) -or $Observer.Child.ExitCode -ne 0) { throw 'Observer failed or timed out' }
    return Get-Content -LiteralPath $Observer.Report -Raw | ConvertFrom-Json
}
function Start-Fixture([string]$Name, [int]$Milliseconds) {
    $ready = Join-Path $root "$Name.ready"
    $child = Start-Child $fixtureExe @('--window-event-check', $Milliseconds, "`"$ready`"")
    Wait-Ready $child $ready
    return $child
}
try {
    Copy-Item -LiteralPath $NativeExe -Destination $fixtureExe
    # Only the renamed fixture is observed; no OBS, hotkeys, tray or user settings.
    $fixture = Start-Fixture 'existing' 15000
    $value = Read-Report (Start-Observer 'existing' 3)
    if ($value.processDetections -lt 1 -or $value.discoveryActive -or $value.hookInstalls -ne 0 -or !$value.eventDriven -or $value.timerTicks -ne 0) {
        throw 'An already-running process must use its handle without discovery notifications'
    }
    $fixture.Kill(); $fixture.WaitForExit()

    $observer = Start-Observer 'restart' 15
    foreach ($round in 1, 2) {
        $fixture = Start-Fixture "restart-$round" 1500
        if (!$fixture.WaitForExit(10000) -or $fixture.ExitCode -ne 0) { throw 'Event fixture failed' }
        Start-Sleep -Milliseconds 500 # Allow the observer to handle the exit before the next launch.
    }
    $value = Read-Report $observer
    if ($value.processDetections -lt 2 -or $value.hookInstalls -lt 3 -or $value.hookRemovals -lt 2 -or !$value.discoveryActive -or !$value.eventDriven -or $value.timerTicks -ne 0) {
        throw 'Discovery must suspend while running and rearm after every external process exit'
    }

    $value = Read-Report (Start-Observer 'idle' 3)
    if ($value.hookInstalls -ne 1 -or $value.hookRemovals -ne 0 -or !$value.discoveryActive -or $value.timerTicks -ne 0) { throw 'Idle discovery must not poll' }
    $value = Read-Report (Start-Observer 'fallback' 3 -Polling)
    if ($value.discoveryActive -or $value.eventDriven -or $value.timerTicks -lt 1) { throw 'Failed notification setup must retain polling' }
    'Native process-handle discovery, external restarts, idle and polling fallback passed'
} finally {
    foreach ($child in $children) {
        if (!$child.HasExited) { $child.Kill(); $child.WaitForExit() } # Only fixtures created above.
        $child.Dispose()
    }
    # Exact, previously checked child of this build directory; never delete reparse targets.
    $entry = Get-Item -LiteralPath $root -Force
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing to delete a redirected test directory' }
    Remove-Item -LiteralPath $root -Recurse -Force
}
