param(
    [string]$Label = 'profile', [int]$Dlss = 5, [int]$NeuralRendering = 1,
    [int]$Width = 1920, [int]$Height = 1080, [int]$TimeoutSeconds = 180, [int]$Sampling = 1,
    [switch]$Validation, [switch]$VisibleWindow,
    [string]$BuildDirectory = '',
    [ValidatePattern('^pt_[a-z_]+\.cfg$')][string]$Config = 'pt_performance.cfg',
    [switch]$Synthetic, [string]$SettingsFile = '', [switch]$PrepareOnly,
    [string]$SharedSettingsFile = '',
    [switch]$EnablePathTracing, [switch]$PipelineStatistics, [switch]$KeepIdleFrameGenerationHooks,
    [ValidateRange(-1,3)][int]$BenchmarkNeuralRendering = -1,
    [ValidateRange(-1,1)][int]$BenchmarkRayReconstruction = -1,
    [ValidateRange(-1,5)][int]$BenchmarkDlss = -1,
    [ValidateRange(0,64)][int]$BenchmarkSamples = 0,
    [ValidateRange(-1,1)][int]$BenchmarkLightReuse = -1,
    [ValidateRange(-1,1)][int]$BenchmarkAdaptive = -1,
    [ValidateSet(-1,0,8,32,64,128)][int]$BenchmarkRRRows = -1,
    [switch]$Bounded,
    [ValidatePattern('^[a-zA-Z0-9_]+$')][string]$Map = 'q3dm6',
    [ValidatePattern('^[a-zA-Z0-9_]+$')][string]$Game = 'baseq3'
)
$ErrorActionPreference = 'Stop'
if ($Synthetic -and $SharedSettingsFile) { throw 'Shared profiles apply only to saved-settings runs, not synthetic benchmarks.' }
if ($Bounded -and ($TimeoutSeconds -gt 120 -or $TimeoutSeconds -lt 1)) {
    throw 'Bounded runs require a 1-120 second deadline.'
}
$ptRepo = Split-Path -Parent $PSScriptRoot
$ptBuild = Join-Path $ptRepo 'build-widescreen/release-mingw64-x86_64'
$ptAssets = $ptBuild
if ($BuildDirectory) { $ptBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path }
$ptProfile = Join-Path $ptRepo 'build-widescreen/rt-audit'
if (!(Test-Path -LiteralPath (Join-Path $ptBuild $Game) -PathType Container)) { throw 'Selected game/mod directory does not exist.' }
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Label must be a simple filename stem.' }
if (Get-Process -Name vQ3Evolution -ErrorAction SilentlyContinue) { throw 'Close the game before running the isolated GPU benchmark.' }
$ptHome = Join-Path $ptProfile "performance-$Label"
if ((Test-Path -LiteralPath $ptHome) -or (Test-Path -LiteralPath (Join-Path $ptProfile "pt-perf-$Label.log"))) {
    throw 'Choose a new label; never reuse a previous benchmark profile or log.'
}
New-Item -ItemType Directory -Path (Join-Path $ptHome $Game) -Force | Out-Null
$ptConfigText = Get-Content -LiteralPath (Join-Path $PSScriptRoot $Config)
$ptSettings = [ordered]@{}
$ptRendererOverride = $null
$ptBenchmarkOverrides = @()
if (!$Synthetic) {
    foreach ($ptOverride in @('Dlss', 'NeuralRendering', 'Width', 'Height', 'Sampling')) {
        if ($PSBoundParameters.ContainsKey($ptOverride)) { throw "Use -Synthetic explicitly for $ptOverride overrides; default benchmarks match saved settings." }
    }
    if (!$SettingsFile) {
        $SettingsFile = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Quake3/baseq3/q3config.cfg'
    }
    if (!(Test-Path -LiteralPath $SettingsFile)) { throw 'Saved graphics configuration not found; provide -SettingsFile.' }
    # Copy graphics values only, never bindings, network credentials or commands.
    foreach ($ptLine in Get-Content -LiteralPath $SettingsFile) {
        if ($ptLine -match '^seta? (r_[A-Za-z0-9_]+|cg_[A-Za-z0-9_]+|cl_renderer|ui_scale|com_maxfps[A-Za-z]*) "([^";\r\n]*)"$') {
            $ptSettings[$Matches[1]] = $Matches[2]
        }
    }
    # Universal presentation values override the per-game config. Keep both
    # inputs separate so the engine, not the harness, performs that override.
    $ptGameSettings = [ordered]@{}
    foreach ($ptName in $ptSettings.Keys) { $ptGameSettings[$ptName] = $ptSettings[$ptName] }
    if (!$SharedSettingsFile) {
        $ptSharedCandidate = Join-Path (Split-Path -Parent (Split-Path -Parent $SettingsFile)) 'vq3e-rendering.cfg'
        if (Test-Path -LiteralPath $ptSharedCandidate -PathType Leaf) { $SharedSettingsFile = $ptSharedCandidate }
    }
    if ($SharedSettingsFile) {
        # Use the engine schema, not arbitrary variables from a data file.
        $ptSharedNames = @{}
        foreach ($ptDefinition in Get-Content -LiteralPath (Join-Path $ptRepo 'code/client/cl_options.c')) {
            if ($ptDefinition -match '^\s*\{"([A-Za-z0-9_]+)",') { $ptSharedNames[$Matches[1]] = $true }
        }
        foreach ($ptLine in Get-Content -LiteralPath $SharedSettingsFile) {
            if ($ptLine -match '^([A-Za-z0-9_]+) ([A-Za-z0-9_.+\-]+)$' -and $ptSharedNames.ContainsKey($Matches[1])) {
                $ptSettings[$Matches[1]] = $Matches[2]
            }
        }
        Copy-Item -LiteralPath $SharedSettingsFile -Destination (Join-Path $ptHome 'vq3e-rendering.cfg')
    }
    if ($EnablePathTracing -and $ptSettings['cl_renderer'] -eq 'vulkan' -and $ptSettings['r_rayTracing'] -ne '2') {
        $ptRendererOverride = [ordered]@{ name = 'r_rayTracing'; from = $ptSettings['r_rayTracing']; to = '2' }
        $ptSettings['r_rayTracing'] = '2'
    }
    if ($ptSettings['cl_renderer'] -ne 'vulkan' -or $ptSettings['r_rayTracing'] -ne '2') {
        throw 'Saved settings do not select Vulkan path tracing; no silent renderer override is allowed.'
    }
    # Measure rendered frames, never generated presentation frames. Record the
    # deliberate override without changing the user's saved configuration.
    $ptBenchmarkOverrides += [ordered]@{ name = 'r_dlssFrameGeneration'; from = $ptSettings['r_dlssFrameGeneration']; to = '0' }
    $ptSettings['r_dlssFrameGeneration'] = '0'
    if ($BenchmarkNeuralRendering -ge 0) {
        $ptBenchmarkOverrides += [ordered]@{ name = 'r_dlssNeuralRendering'; from = $ptSettings['r_dlssNeuralRendering']; to = "$BenchmarkNeuralRendering" }
        $ptSettings['r_dlssNeuralRendering'] = "$BenchmarkNeuralRendering"
    }
    foreach ($ptSettingOverride in @(
        @{ name = 'r_pathTracingLightReuse'; value = $BenchmarkLightReuse; enabled = ($BenchmarkLightReuse -ge 0) },
        @{ name = 'r_pathTracingAdaptive'; value = $BenchmarkAdaptive; enabled = ($BenchmarkAdaptive -ge 0) },
        @{ name = 'r_pathTracingRRRows'; value = $BenchmarkRRRows; enabled = ($BenchmarkRRRows -ge 0) },
        @{ name = 'r_dlss'; value = $BenchmarkDlss; enabled = ($BenchmarkDlss -ge 0) },
        @{ name = 'r_dlssRayReconstruction'; value = $BenchmarkRayReconstruction; enabled = ($BenchmarkRayReconstruction -ge 0) },
        @{ name = 'r_pathTracingSamples'; value = $BenchmarkSamples; enabled = ($BenchmarkSamples -gt 0) }
    )) {
        if ($ptSettingOverride.enabled) {
            $ptBenchmarkOverrides += [ordered]@{ name = $ptSettingOverride.name; from = $ptSettings[$ptSettingOverride.name]; to = "$($ptSettingOverride.value)" }
            $ptSettings[$ptSettingOverride.name] = "$($ptSettingOverride.value)"
        }
    }
    $ptConfigSettings = if ($SharedSettingsFile) { $ptGameSettings } else { $ptSettings }
    $ptSavedGraphics = foreach ($ptName in $ptConfigSettings.Keys) { 'seta {0} "{1}"' -f $ptName, $ptConfigSettings[$ptName] }
    Set-Content -LiteralPath (Join-Path $ptHome "$Game/q3config.cfg") -Value $ptSavedGraphics -Encoding ASCII
    # The scenario owns camera movement and diagnostic switches, not quality.
    # Keep the user's resolution, DLSS, samples, bounces, NR, filtering,
    # exposure, sharpening, FOV, HUD, weapon presentation and FPS cap intact.
    $ptConfigText = $ptConfigText | Where-Object {
        $_ -notmatch '^set (r_[A-Za-z0-9_]+|cg_[A-Za-z0-9_]+|com_maxfps[A-Za-z]*) ' -or
        $_ -match '^set r_pathTracing(Profile|ShaderProfile|Reference|Debug|TemporalDebug|TestScene|TestMotion|MaterialFastPath|EmitterSearch|EmitterGeometry|BRDFReuse|MapLightCull|AliasPDF|LightLoop|MaterialCache|CompactTransport|Staged|StagedRows|StagedProfile|AdaptiveDebug|DynamicOpaque) '
    }
}
Set-Content -LiteralPath (Join-Path $ptHome "$Game/$Config") -Value $ptConfigText -Encoding ASCII
$ptManifest = [ordered]@{
    synthetic = [bool]$Synthetic; config = $Config; map = $Map; game = $Game; source = $SettingsFile
    sourceSha256 = if ($SettingsFile) { (Get-FileHash -LiteralPath $SettingsFile -Algorithm SHA256).Hash } else { $null }
    sharedSource = $SharedSettingsFile
    sharedSourceSha256 = if ($SharedSettingsFile) { (Get-FileHash -LiteralPath $SharedSettingsFile -Algorithm SHA256).Hash } else { $null }
    settings = $ptSettings
    rendererOverride = $ptRendererOverride
    benchmarkOverrides = $ptBenchmarkOverrides
    bounded = [bool]$Bounded
    pipelineStatistics = [bool]$PipelineStatistics
    keepIdleFrameGenerationHooks = [bool]$KeepIdleFrameGenerationHooks
    executableSha256 = (Get-FileHash -LiteralPath (Join-Path $ptBuild 'vQ3Evolution.exe') -Algorithm SHA256).Hash
    rendererSha256 = (Get-FileHash -LiteralPath (Join-Path $ptBuild 'renderer_vulkan_x86_64.dll') -Algorithm SHA256).Hash
}
$ptManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $ptHome 'settings.json') -Encoding UTF8
Write-Output "Prepared $(if ($Synthetic) {'synthetic'} else {'saved-settings'}) benchmark: $ptHome"
if ($PrepareOnly) { return }
$ptProcess = $null
$ptPreviousEnvironment = @{}
foreach ($ptEnvName in @('VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_LAYER_SETTINGS_PATH')) {
    $ptPreviousEnvironment[$ptEnvName] = [Environment]::GetEnvironmentVariable($ptEnvName, 'Process')
}
try {
if ($Validation) {
    $env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
    $env:VK_LAYER_PATH='C:\VulkanSDK\1.4.350.0\Bin'
    # Each run owns its validation output; never overwrite a prior audit log.
    $ptValidationSettings = @(
        'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG'
        ('khronos_validation.log_filename = ' + (Join-Path $ptHome 'validation.log').Replace('\','/'))
        'khronos_validation.report_flags = error,warn'
        'khronos_validation.enable_message_limit = true'
        'khronos_validation.duplicate_message_limit = 2'
        'khronos_validation.validate_sync = true'
    )
    Set-Content -LiteralPath (Join-Path $ptHome 'vk_layer_settings.txt') -Value $ptValidationSettings -Encoding ASCII
    $env:VK_LAYER_SETTINGS_PATH=$ptHome
} else {
    $env:VK_INSTANCE_LAYERS=''
}
$ptArguments = "+set fs_homepath `"$($ptHome.Replace('\','/'))`""
if ($BuildDirectory) { $ptArguments += " +set fs_basepath `"$($ptAssets.Replace('\','/'))`"" }
if ($Game -ne 'baseq3') { $ptArguments += " +set fs_game $Game" }
if ($Synthetic) {
    $ptArguments += " +set cl_renderer vulkan +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $Width +set r_customheight $Height +set r_rayTracing 2 +set r_dlss $Dlss +set r_dlssNeuralRendering $NeuralRendering +set r_dlssFrameGeneration 0 +set r_pathTracingTestScene 0 +set r_pathTracingSampling $Sampling +set r_swapInterval 0"
}
$ptArguments += " +set logfile 2 +set developer 0 +devmap $Map +exec $Config"
if ($ptRendererOverride) { $ptArguments += ' +set r_rayTracing 2' }
$ptArguments += ' +set r_dlssFrameGeneration 0'
if ($BenchmarkNeuralRendering -ge 0) { $ptArguments += " +set r_dlssNeuralRendering $BenchmarkNeuralRendering" }
if ($BenchmarkRayReconstruction -ge 0) { $ptArguments += " +set r_dlssRayReconstruction $BenchmarkRayReconstruction" }
if ($BenchmarkDlss -ge 0) { $ptArguments += " +set r_dlss $BenchmarkDlss" }
if ($BenchmarkSamples -gt 0) { $ptArguments += " +set r_pathTracingSamples $BenchmarkSamples" }
if ($BenchmarkLightReuse -ge 0) { $ptArguments += " +set r_pathTracingLightReuse $BenchmarkLightReuse" }
if ($BenchmarkAdaptive -ge 0) { $ptArguments += " +set r_pathTracingAdaptive $BenchmarkAdaptive" }
if ($BenchmarkRRRows -ge 0) { $ptArguments += " +set r_pathTracingRRRows $BenchmarkRRRows" }
if ($PipelineStatistics) { $ptArguments = '+set r_pathTracingPipelineStats 1 ' + $ptArguments }
if ($KeepIdleFrameGenerationHooks) { $ptArguments = '+set r_dlssFGIdleHooks 1 ' + $ptArguments }
$ptWindowStyle = if ($VisibleWindow) { 'Normal' } else { 'Hidden' }
$ptStarted = Get-Date
$ptTimer = [Diagnostics.Stopwatch]::StartNew()
if ($Bounded) {
    # An independent job owner closes ONLY its game process even if this script
    # is interrupted. Use PowerShell 7 argument lists, not nested shell quoting.
    $ptStart = [Diagnostics.ProcessStartInfo]::new()
    $ptStart.FileName = Join-Path $ptProfile 'tools/pt-profile-guard.exe'
    $ptStart.WorkingDirectory = $ptBuild
    $ptStart.UseShellExecute = $false
    $ptStart.CreateNoWindow = $true
    $ptStart.RedirectStandardOutput = $true
    $ptStart.RedirectStandardError = $true
    $ptStart.Environment['VQ3E_BOUNDED_LOG'] = Join-Path $ptHome 'guard.log'
    foreach ($ptArg in @((Join-Path $ptBuild 'vQ3Evolution.exe'), $ptBuild, "$TimeoutSeconds", $ptArguments)) {
        $ptStart.ArgumentList.Add($ptArg)
    }
    $ptProcess = [Diagnostics.Process]::Start($ptStart)
    $ptGuardOutput = $ptProcess.StandardOutput.ReadToEndAsync()
    $ptGuardErrors = $ptProcess.StandardError.ReadToEndAsync()
} else {
    $ptProcess = Start-Process -FilePath (Join-Path $ptBuild 'vQ3Evolution.exe') -WorkingDirectory $ptBuild -ArgumentList $ptArguments -WindowStyle $ptWindowStyle -PassThru
}
$ptTimedOut = $false
$ptValidationLayerLoaded = $false
$ptNextLayerCheck = 1.0
while (-not $ptProcess.WaitForExit(100)) {
    if ($Validation -and !$ptValidationLayerLoaded -and $ptTimer.Elapsed.TotalSeconds -ge $ptNextLayerCheck) {
        $ptNextLayerCheck = $ptTimer.Elapsed.TotalSeconds + 1.0
        try {
            $ptModuleProcess = $ptProcess
            if ($Bounded) {
                $ptGuardText = Get-Content -Raw -LiteralPath (Join-Path $ptHome 'guard.log') -ErrorAction Stop
                if ($ptGuardText -notmatch ('VQ3E_BOUNDED pid=(\d+) deadline_ms=\d+ owner=' + $ptProcess.Id + '\b')) { throw 'Game PID not reported by this run owner yet' }
                $ptModuleProcess = [Diagnostics.Process]::GetProcessById([int]$Matches[1])
            }
            $ptModuleProcess.Refresh()
            $ptValidationLayerLoaded = [bool]($ptModuleProcess.Modules | Where-Object { $_.ModuleName -ieq 'VkLayer_khronos_validation.dll' })
        } catch { }
    }
    if ($ptTimer.Elapsed.TotalSeconds -ge ($TimeoutSeconds + $(if ($Bounded) { 3 } else { 0 }))) {
        Stop-Process -Id $ptProcess.Id
        $ptProcess.WaitForExit()
        $ptTimedOut = $true
        break
    }
}
if ($Bounded) {
    $ptTimedOut = $ptTimedOut -or $ptProcess.ExitCode -eq 124
    $ptGuardErrors.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $ptHome 'guard-stderr.log')
    $null = $ptGuardOutput.GetAwaiter().GetResult()
}
$ptLog = Join-Path $ptHome "$Game/qconsole.log"
if (!(Test-Path -LiteralPath $ptLog) -or (Get-Item -LiteralPath $ptLog).LastWriteTime -lt $ptStarted) {
    throw "Performance test $Label produced no fresh log (timed out: $ptTimedOut); no benchmark result is valid."
}
Copy-Item -LiteralPath $ptLog -Destination (Join-Path $ptProfile "pt-perf-$Label.log")
. (Join-Path $PSScriptRoot 'pt-run-summary.ps1')
$ptSummary = Get-PtRunSummary -Lines (Get-Content -LiteralPath $ptLog) -ElapsedSeconds $ptTimer.Elapsed.TotalSeconds -TimedOut $ptTimedOut -ExitCode $ptProcess.ExitCode
$ptSummary['validationLayerLoaded'] = if ($Validation) { $ptValidationLayerLoaded } else { $null }
$ptSummary['sourceSettingsUnchanged'] = if ($SettingsFile) {
    (Get-FileHash -LiteralPath $SettingsFile -Algorithm SHA256).Hash -eq $ptManifest.sourceSha256
} else { $null }
$ptSummary['sharedSourceSettingsUnchanged'] = if ($SharedSettingsFile) {
    (Get-FileHash -LiteralPath $SharedSettingsFile -Algorithm SHA256).Hash -eq $ptManifest.sharedSourceSha256
} else { $null }
$ptSummary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ptProfile "pt-perf-$Label.run.json") -Encoding UTF8
Write-Output ('Diagnostic phases (raw samples): ' + ($ptSummary.rawPhaseSamples | ConvertTo-Json -Compress))
foreach ($ptCompile in $ptSummary.pipelineCompilations) {
    Write-Output "Pipeline mode $($ptCompile.mode): compilation $($ptCompile.cpuMilliseconds) ms, result $($ptCompile.result)"
}
if ($ptSummary.unfinishedPipelineModes.Count) {
    Write-Output "Pipeline creation did not finish for mode(s): $($ptSummary.unfinishedPipelineModes -join ', ')"
}
Write-Output "Performance test $Label exit: $($ptProcess.ExitCode), elapsed $([math]::Round($ptTimer.Elapsed.TotalSeconds, 1)) s"
if ($ptTimedOut) { throw "Performance test $Label timed out; partial log is diagnostic only." }
if ($ptProcess.ExitCode -ne 0) { throw 'Game failed.' }
} finally {
    if ($Bounded -and $ptProcess -and !$ptProcess.HasExited) { $ptProcess.Kill(); $ptProcess.WaitForExit() }
    foreach ($ptEnvName in $ptPreviousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($ptEnvName, $ptPreviousEnvironment[$ptEnvName], 'Process')
    }
}
