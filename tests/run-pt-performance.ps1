param(
    [string]$Label = 'profile', [int]$Dlss = 5, [int]$NeuralRendering = 1,
    [int]$Width = 1920, [int]$Height = 1080, [int]$TimeoutSeconds = 180,
    [switch]$Validation
)
$ErrorActionPreference = 'Stop'
$ptRepo = Split-Path -Parent $PSScriptRoot
$ptBuild = Join-Path $ptRepo 'build-widescreen/release-mingw64-x86_64'
$ptProfile = Join-Path $ptRepo 'build-widescreen/rt-audit'
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Label must be a simple filename stem.' }
if (Get-Process -Name vQ3Evolution -ErrorAction SilentlyContinue) { throw 'Close the game before running the isolated GPU benchmark.' }
New-Item -ItemType Directory -Path (Join-Path $ptProfile 'baseq3') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'pt_performance.cfg') -Destination (Join-Path $ptProfile 'baseq3/pt_performance.cfg')
if ($Validation) {
    $env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
    $env:VK_LAYER_PATH='C:\VulkanSDK\1.4.350.0\Bin'
    $env:VK_LAYER_SETTINGS_PATH=$ptProfile
} else {
    $env:VK_INSTANCE_LAYERS=''
}
$ptArguments = "+set fs_homepath `"$($ptProfile.Replace('\','/'))`" +set cl_renderer vulkan +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $Width +set r_customheight $Height +set r_rayTracing 2 +set r_dlss $Dlss +set r_dlssNeuralRendering $NeuralRendering +set r_dlssFrameGeneration 0 +set r_swapInterval 0 +set logfile 2 +set developer 0 +devmap q3dm6 +exec pt_performance.cfg"
$ptProcess = Start-Process -FilePath (Join-Path $ptBuild 'vQ3Evolution.exe') -WorkingDirectory $ptBuild -ArgumentList $ptArguments -WindowStyle Hidden -PassThru
$ptTimer = [Diagnostics.Stopwatch]::StartNew()
while (-not $ptProcess.WaitForExit(1000)) {
    if ($ptTimer.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
        Stop-Process -Id $ptProcess.Id
        $ptProcess.WaitForExit()
        throw "Performance test $Label timed out"
    }
}
Copy-Item -LiteralPath (Join-Path $ptProfile 'baseq3/qconsole.log') -Destination (Join-Path $ptProfile "pt-perf-$Label.log")
Write-Output "Performance test $Label exit: $($ptProcess.ExitCode), elapsed $([math]::Round($ptTimer.Elapsed.TotalSeconds, 1)) s"
if ($ptProcess.ExitCode -ne 0) { throw 'Game failed.' }
