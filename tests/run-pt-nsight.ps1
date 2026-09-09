# Run with PowerShell 7. Capture-only: never changes saved graphics settings.
[CmdletBinding()]
param(
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = 'nsight',
    [ValidateSet('Systems','Graphics')][string]$Tool = 'Systems',
    [string]$SettingsFile = '',
    [switch]$PrepareOnly,
    [switch]$FunctionsOnly,
    [switch]$Worker
)
$ErrorActionPreference = 'Stop'
$ptRepo = Split-Path -Parent $PSScriptRoot
$ptBuild = Join-Path $ptRepo 'build-widescreen/release-mingw64-x86_64'
$ptAudit = Join-Path $ptRepo 'build-widescreen/rt-audit'
$ptHome = Join-Path $ptAudit "performance-$Label"
$ptCapture = Join-Path $ptHome 'nsight'
$ptGuard = Join-Path $ptAudit 'tools/pt-profile-guard.exe'
$ptExe = Join-Path $ptBuild 'vQ3Evolution.exe'
$ptSystems = 'C:/Program Files/NVIDIA Corporation/Nsight Systems 2025.6.3/target-windows-x64/nsys.exe'
$ptGraphics = 'C:/Program Files/NVIDIA Corporation/Nsight Graphics 2026.3.1/host/windows-desktop-nomad-x64/ngfx.exe'

# Windows CommandLineToArgvW/MSVCRT quoting, including embedded quotes and trailing slashes.
function ConvertTo-PtArgument([string]$Value) {
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
function Wait-PtGuardResult([string]$Path, [int]$TimeoutSeconds = 60) {
    $ptGuardTimer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $ptGuardText = [string](Get-Content -Raw -LiteralPath $Path -ErrorAction SilentlyContinue)
        $ptGuardExit = [regex]::Match($ptGuardText, 'VQ3E_BOUNDED exit=(\d+) elapsed_ms=(\d+) timed_out=(\d+)')
        if ($ptGuardExit.Success) {
            $ptOwner = [regex]::Match($ptGuardText, 'VQ3E_BOUNDED pid=\d+ deadline_ms=\d+ owner=(\d+)')
            return [ordered]@{ exit=[long]$ptGuardExit.Groups[1].Value; elapsedMilliseconds=[long]$ptGuardExit.Groups[2].Value; timedOut=$ptGuardExit.Groups[3].Value -eq '1'; owner=if ($ptOwner.Success) {[int]$ptOwner.Groups[1].Value} else {$null} }
        }
        Start-Sleep -Milliseconds 100
    } while ($ptGuardTimer.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    return $null
}
function Test-PtNsightConnection {
    param([int]$BasePort = 0, [ValidateRange(1,65535)][int]$MaxPorts = 64)
    if ($BasePort -eq 0) {
        # ngfx 2026.3.1 uses native QSettings (HKCU), whereas ngfx-ui uses an
        # INI file. Changing Tools > Options alone does not fix CLI launches.
        $connection = Get-ItemProperty -LiteralPath 'HKCU:/Software/NVIDIA Corporation/NVIDIA Nsight Graphics/Connection' -ErrorAction SilentlyContinue
        $BasePort = if ($connection.ConnectionBasePort) { [int]$connection.ConnectionBasePort } else { 49152 }
        if ($connection.ConnectionMaxPorts) { $MaxPorts = [int]$connection.ConnectionMaxPorts }
    }
    if ($BasePort -lt 1 -or $MaxPorts -lt 1 -or $BasePort + $MaxPorts -gt 65536) { throw 'Invalid Nsight connection range.' }
    $failures = @{}
    for ($port = $BasePort; $port -lt $BasePort + $MaxPorts; ++$port) {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $port)
        try {
            $listener.Start()
            return [ordered]@{ basePort=$BasePort; maxPorts=$MaxPorts; availablePort=$port }
        } catch [Net.Sockets.SocketException] {
            $failures[[string]$_.Exception.SocketErrorCode] = $true
        } finally { $listener.Stop() }
    }
    throw "Nsight cannot bind any port in $BasePort-$($BasePort+$MaxPorts-1): $($failures.Keys -join ', '). Check Windows excluded TCP ranges and Nsight CLI's HKCU connection settings; see docs/GPU_PROFILING.md. No game was launched."
}
if ($FunctionsOnly) { return }

if (Get-Process -Name vQ3Evolution -ErrorAction SilentlyContinue) { throw 'Close the game before a capture.' }
foreach ($ptRequired in @($ptGuard, $ptExe, $(if ($Tool -eq 'Systems') {$ptSystems} else {$ptGraphics}))) {
    if (!(Test-Path -LiteralPath $ptRequired)) { throw "Required tool missing: $ptRequired" }
}
if (!$Worker) {
    # Preparation happens as the signed-in user, before UAC can change APPDATA.
    & (Join-Path $PSScriptRoot 'run-pt-performance.ps1') -Label $Label -Config pt_nsight.cfg -SettingsFile $SettingsFile -PrepareOnly
    New-Item -ItemType Directory -Path $ptCapture | Out-Null
    if ($PrepareOnly) { return }
    $ptShell = (Get-Process -Id $PID).Path
    $ptWorkerArgs = @('-NoProfile','-File',$PSCommandPath,'-Label',$Label,'-Tool',$Tool,'-Worker')
    $ptAdmin = Start-Process -FilePath $ptShell -Verb RunAs -WindowStyle Hidden -ArgumentList (($ptWorkerArgs | ForEach-Object { ConvertTo-PtArgument $_ }) -join ' ') -PassThru
    Write-Output "Capture worker PID $($ptAdmin.Id); game guard: 45 seconds. Output: $ptCapture"
    return
}
$ptIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (!(New-Object Security.Principal.WindowsPrincipal($ptIdentity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Hardware profiling worker must run through standard administrator approval.'
}
$ptResultPath = Join-Path $ptCapture 'result.json'
if (Test-Path -LiteralPath $ptResultPath) { throw 'Never overwrite a previous capture.' }
$ptResult = [ordered]@{ tool=$Tool; completed=$false; error=$null; profilerExit=$null; sourceSettingsUnchanged=$false }
$ptProcess = $null
try {
    if ($Tool -eq 'Graphics') { $ptResult['connection'] = Test-PtNsightConnection }
    $ptManifest = Get-Content -Raw -LiteralPath (Join-Path $ptHome 'settings.json') | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath $ptExe).Hash -ne $ptManifest.executableSha256 -or
        (Get-FileHash -LiteralPath (Join-Path $ptBuild 'renderer_vulkan_x86_64.dll')).Hash -ne $ptManifest.rendererSha256) {
        throw 'Build changed after capture preparation.'
    }
    if ($ptManifest.settings.r_fullscreen -ne '0') { throw 'This bounded capture requires saved windowed settings; no display-mode override is made.' }
    # The shared user profile can restore archived FG settings during startup;
    # command-line startup overrides must agree with the benchmark manifest.
    $ptGameArgs = '+set fs_homepath ' + (ConvertTo-PtArgument ($ptHome.Replace('\','/'))) + ' +set r_dlssFrameGeneration 0 +set logfile 2 +set developer 0 +devmap q3dm6 +exec pt_nsight.cfg'
    $ptStart = [Diagnostics.ProcessStartInfo]::new()
    $ptStart.UseShellExecute = $false
    $ptStart.CreateNoWindow = $true
    $ptStart.WorkingDirectory = $ptBuild
    $ptStart.RedirectStandardOutput = $true
    $ptStart.RedirectStandardError = $true
    # Clear inherited validation BEFORE Nsight injects its own Vulkan layer.
    foreach ($ptEnv in @('VK_INSTANCE_LAYERS','VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH')) { $ptStart.Environment.Remove($ptEnv) | Out-Null }
    $ptStart.Environment['VQ3E_GPU_LABELS'] = '1'
    $ptStart.Environment['VQ3E_BOUNDED_LOG'] = Join-Path $ptCapture 'guard.log'
    $ptStart.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
    if ($Tool -eq 'Systems') {
        $ptStart.FileName = $ptSystems
        $ptArgs = @('profile', "--output=$(Join-Path $ptCapture 'systems')", '--trace=vulkan,vulkan-annotations',
            '--sample=process-tree','--cpuctxsw=process-tree','--gpu-metrics-devices=0','--gpu-metrics-set=gb20x-top',
            '--gpu-metrics-frequency=1000','--delay=8','--duration=10','--kill=false','--wait=all',
            $ptGuard,$ptExe,$ptBuild,'45',$ptGameArgs)
    } else {
        # The job owns the profiler AND any game it creates, even if injection fails.
        # No clock locking, global counter-access changes or background capture.
        # 2026.3.1 passes --platform to Qt, which treats the documented Windows
        # display name as a QPA plugin and aborts. Use the native default instead.
        $ptGraphicsArgs = @('--activity','GPU Trace Profiler',
            '--exe',$ptExe,'--dir',$ptBuild,'--args',$ptGameArgs,'--output-dir',$ptCapture,
            '--start-after-frames','120','--max-duration-ms','250','--trace-timeout','20',
            '--allocated-hes-buffer-memory-kb','8192','--pc-samples-per-pm-interval-per-sm','16384',
            '--architecture','Blackwell GB20x','--metric-set-name','Top-Level Triage',
            # Keep the native trace. 2026.3.1's automatic post-capture metrics
            # export crashed in Qt6Core after saving the report on this host.
            '--real-time-shader-profiler','--set-gpu-clocks','unaltered','--verbose')
        $ptStart.FileName = $ptGuard
        $ptArgs = @($ptGraphics,$ptBuild,'45',(($ptGraphicsArgs | ForEach-Object { ConvertTo-PtArgument $_ }) -join ' '))
    }
    foreach ($ptArg in $ptArgs) { $ptStart.ArgumentList.Add($ptArg) }
    $ptResult['arguments'] = $ptArgs
    $ptResult['startedUtc'] = [DateTime]::UtcNow.ToString('o')
    $ptTimer = [Diagnostics.Stopwatch]::StartNew()
    $ptProcess = [Diagnostics.Process]::Start($ptStart)
    $ptOut = $ptProcess.StandardOutput.ReadToEndAsync()
    $ptErr = $ptProcess.StandardError.ReadToEndAsync()
    # Export may continue after the game has closed. Its independent job guard
    # stays capped at 45 s even if this worker or the profiler crashes.
    while (!$ptProcess.WaitForExit(200)) {
        if ($ptTimer.Elapsed.TotalSeconds -ge 120) {
            $ptProcess.Kill($true)
            $ptProcess.WaitForExit(5000) | Out-Null
            throw 'Profiler/export timed out; only this capture process tree was terminated.'
        }
    }
    Set-Content -LiteralPath (Join-Path $ptCapture 'stdout.log') -Value $ptOut.GetAwaiter().GetResult() -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $ptCapture 'stderr.log') -Value $ptErr.GetAwaiter().GetResult() -Encoding UTF8
    $ptResult.profilerExit = $ptProcess.ExitCode
    # nsys may finish exporting just before the guard's final stdout flush.
    # Observe the independent owner finishing, not merely the profiler exiting.
    $ptResult['guard'] = Wait-PtGuardResult -Path (Join-Path $ptCapture 'guard.log') -TimeoutSeconds ([math]::Max(1, [math]::Ceiling(60-$ptTimer.Elapsed.TotalSeconds)))
    $ptResult['elapsedSeconds'] = $ptTimer.Elapsed.TotalSeconds
    $ptLog = Join-Path $ptHome 'baseq3/qconsole.log'
    $ptResult['scenarioCompleted'] = (Test-Path -LiteralPath $ptLog) -and [bool](Select-String -LiteralPath $ptLog -SimpleMatch 'PT_NSIGHT_COMPLETE')
    $ptResult['artifacts'] = @(Get-ChildItem -LiteralPath $ptCapture -File | Where-Object Extension -NotIn '.log','.json' | Select-Object Name,Length)
    if (!$ptResult.guard) { throw 'No independent process-guard exit evidence.' }
    if ($Tool -eq 'Systems' -and $ptResult.guard.owner) {
        # nsys injects the wrapper as well as the game. Its own DLL can linger
        # at wrapper process-exit AFTER our child finished and job closed.
        # Only clean that exact captured helper, never a name-based process list.
        $ptOwnerPid = $ptResult.guard.owner
        & (Join-Path $PSScriptRoot 'stop-pt-capture-helper.ps1') -Label $Label -OwnerPid $ptOwnerPid
        $ptCleanupResult = Get-Content -Raw -LiteralPath (Join-Path $ptCapture 'helper-cleanup.json') | ConvertFrom-Json
        $ptResult['injectedHelperCleanedUp'] = $ptCleanupResult.closed
        if (!$ptCleanupResult.closed) { throw "Capture saved, but profiler helper cleanup was incomplete: $($ptCleanupResult.error)" }
    }
    if ($ptProcess.ExitCode -ne 0) { throw "Profiler exited $($ptProcess.ExitCode); inspect capture logs." }
    if ($ptResult.guard.exit -ne 0) { throw 'Guard reported failed/unfinished target execution.' }
    if (!($ptResult.artifacts | Where-Object Length -GT 0)) { throw 'Profiler returned without a nonempty report.' }
    if ($Tool -eq 'Systems' -and !$ptResult.scenarioCompleted) { throw 'Game did not finish the system-trace scenario.' }
    $ptResult.completed = $true
} catch {
    $ptResult.error = $_.Exception.Message
} finally {
    if ($ptProcess -and !$ptProcess.HasExited) { $ptProcess.Kill($true) }
    if ($ptManifest) {
        $ptResult.sourceSettingsUnchanged = (Get-FileHash -LiteralPath $ptManifest.source).Hash -eq $ptManifest.sourceSha256
    }
    $ptResult | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ptResultPath -Encoding UTF8
}
# A successful launch/report is not proof of usable counters. Inspect/export the
# report and verify Vulkan workloads plus nonempty GPU metrics before accepting.
