param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$Label,
    [int]$Width = 640, [int]$Height = 360, [int]$Dlss = 0,
    [int]$TimeoutSeconds = 180, [int]$FrameGeneration = 0,
    [int]$NeuralRendering = 0, [switch]$DebugCapture,
    [ValidateSet(0,1)][int]$Developer = 0,
    [ValidateSet(0,1)][int]$SwapInterval = 0,
    [switch]$VisibleWindow
)
$ErrorActionPreference = 'Stop'
if ($Config -notmatch '^pt_[a-z_]+\.cfg$' -or $Label -notmatch '^[a-zA-Z0-9_-]+$') {
    throw 'Use a checked-in pt_*.cfg test and a simple filename label.'
}
if (Get-Process -Name vQ3Evolution -ErrorAction SilentlyContinue) { throw 'Close the game before running the isolated GPU test.' }
$ptRepo = Split-Path -Parent $PSScriptRoot
$ptBuild = Join-Path $ptRepo 'build-widescreen/release-mingw64-x86_64'
$ptProfile = Join-Path $ptRepo 'build-widescreen/rt-audit'
New-Item -ItemType Directory -Path (Join-Path $ptProfile 'baseq3') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $Config) -Destination (Join-Path $ptProfile "baseq3/$Config")
if ($Config -eq 'pt_fg_interactive.cfg') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'pt_frame_generation.cfg') -Destination (Join-Path $ptProfile 'baseq3/pt_frame_generation.cfg')
}
# The SDK's default validation includes synchronization checks through this
# profile's vk_layer_settings.txt when present. No saved user profile is used.
$ptPreviousEnvironment = @{}
foreach ($ptEnvName in @('VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_LAYER_SETTINGS_PATH', 'VQ3E_VULKAN_AUDIT_LOG')) {
    $ptPreviousEnvironment[$ptEnvName] = [Environment]::GetEnvironmentVariable($ptEnvName, 'Process')
}
try {
$env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
$env:VK_LAYER_PATH='C:\VulkanSDK\1.4.350.0\Bin'
$env:VK_LAYER_SETTINGS_PATH=$ptProfile
# Keep every instance lifetime and post-SDK teardown diagnostics. A unique raw
# log is appended by the renderer; never overwrite an earlier run with this label.
$ptRawAudit = Join-Path $ptProfile "pt-$Label-raw-validation.log"
if (Test-Path -LiteralPath $ptRawAudit) { throw 'Choose a new label; its raw audit log already exists.' }
$env:VQ3E_VULKAN_AUDIT_LOG=$ptRawAudit
$ptArguments = "+set fs_homepath `"$($ptProfile.Replace('\','/'))`" +set cl_renderer vulkan +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $Width +set r_customheight $Height +set r_rayTracing 2 +set r_dlss $Dlss +set r_dlssNeuralRendering $NeuralRendering +set r_dlssFrameGeneration $FrameGeneration +set r_pathTracingTestScene 0 +set r_pathTracingTestMotion 0 +set r_pathTracingSampling 1 +set r_pathTracingDebug 0 +set r_pathTracingTemporalDebug 0 +set r_swapInterval $SwapInterval +set logfile 2 +set developer $Developer +devmap q3dm6 +exec $Config"
$ptStart = Get-Date
$ptTimedOut = $false
if ($DebugCapture) {
    # PowerShell native argument passing preserves the nested quoted home path.
    # The helper launches/debugs only its own child and owns its watchdog.
    $ptSymbols = Join-Path $ptRepo 'build-widescreen/deps/streamline-sdk-v2.12.0/symbols'
    $ptDebugWindowArguments = @()
    if ($VisibleWindow) { $ptDebugWindowArguments = @('--visible') }
    & (Join-Path $ptProfile 'pt-crash-capture.exe') (Join-Path $ptBuild 'vQ3Evolution.exe') $ptBuild $ptSymbols $TimeoutSeconds $ptArguments @ptDebugWindowArguments |
        Tee-Object -FilePath (Join-Path $ptProfile "pt-$Label-stack.log")
    $ptExitCode = $LASTEXITCODE
} else {
    # Opt in only for an explicitly authorized, attended focus test.
    $ptWindowStyle = if ($VisibleWindow) { 'Normal' } else { 'Hidden' }
    $ptProcess = Start-Process -FilePath (Join-Path $ptBuild 'vQ3Evolution.exe') -WorkingDirectory $ptBuild -ArgumentList $ptArguments -WindowStyle $ptWindowStyle -PassThru
    $ptTimer = [Diagnostics.Stopwatch]::StartNew()
    while (-not $ptProcess.WaitForExit(1000)) {
        if ($ptTimer.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
            Stop-Process -Id $ptProcess.Id
            $ptProcess.WaitForExit()
            $ptTimedOut = $true
            break
        }
    }
    $ptExitCode = $ptProcess.ExitCode
    if ($ptTimedOut) { $ptExitCode = 124 }
}
Copy-Item -LiteralPath (Join-Path $ptProfile 'baseq3/qconsole.log') -Destination (Join-Path $ptProfile "pt-$Label.log")
$ptValidation = Join-Path $ptProfile 'validation-after.log'
if ((Test-Path -LiteralPath $ptValidation) -and (Get-Item -LiteralPath $ptValidation).LastWriteTime -ge $ptStart) {
    Copy-Item -LiteralPath $ptValidation -Destination (Join-Path $ptProfile "pt-$Label-validation.log")
}
Write-Output "Test $Label exit: $ptExitCode, elapsed $([math]::Round(((Get-Date)-$ptStart).TotalSeconds,1)) s"
if ($ptExitCode -ne 0) { throw 'Game failed; do not use its captures.' }
} finally {
    foreach ($ptEnvName in $ptPreviousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($ptEnvName, $ptPreviousEnvironment[$ptEnvName], 'Process')
    }
}
