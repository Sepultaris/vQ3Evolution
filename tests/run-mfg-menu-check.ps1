param(
    [ValidateRange(2,6)][int]$Multiplier = 2,
    [ValidateRange(15,90)][int]$TimeLimitSeconds = 45,
    [ValidateSet(0,1,2)][int]$Developer = 0,
    [switch]$NoValidation,
    [switch]$Foreground,
    [string]$RuntimeDirectory = '',
    [string]$CC = 'C:/msys64/ucrt64/bin/gcc.exe'
)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name vQ3Evolution -ErrorAction SilentlyContinue) { throw 'Close the running game before testing.' }
$mfgRepo = Split-Path $PSScriptRoot -Parent
$mfgBuild = Join-Path $mfgRepo 'build-widescreen/release-mingw64-x86_64'
$mfgBase = $mfgBuild
if ($RuntimeDirectory) { $mfgBuild = (Resolve-Path -LiteralPath $RuntimeDirectory).Path }
$mfgAudit = Join-Path $mfgRepo ('build-widescreen/mfg-menu-audit/run-'+[guid]::NewGuid().ToString('N'))
$mfgHome = Join-Path $mfgAudit 'home'
New-Item -ItemType Directory -Path (Join-Path $mfgHome 'baseq3') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'pt_mfg_menu.cfg') -Destination (Join-Path $mfgHome 'baseq3/pt_mfg_menu.cfg')
$mfgGuard = Join-Path $mfgAudit 'bounded-process.exe'
& $CC -O2 -municode (Join-Path $mfgRepo 'tools/pt-bounded-process.c') -o $mfgGuard
if ($LASTEXITCODE) { throw 'Guard compilation failed.' }
$mfgSettings = @{}
$mfgSettingsRoot = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Quake3'
foreach ($mfgRelative in @('baseq3/q3config.cfg','vq3e-rendering.cfg')) {
    $mfgPath = Join-Path $mfgSettingsRoot $mfgRelative
    if (Test-Path -LiteralPath $mfgPath) { $mfgSettings[$mfgPath] = (Get-FileHash -LiteralPath $mfgPath).Hash }
}
$mfgEnv = @{}
foreach ($mfgName in @('VQ3E_BOUNDED_LOG','VQ3E_BOUNDED_FOREGROUND','VQ3E_VULKAN_AUDIT_LOG','VK_INSTANCE_LAYERS','VK_LAYER_PATH')) {
    $mfgEnv[$mfgName] = [Environment]::GetEnvironmentVariable($mfgName,'Process')
}
try {
    $env:VQ3E_BOUNDED_LOG = Join-Path $mfgAudit 'guard.log'
    $env:VQ3E_BOUNDED_FOREGROUND = if ($Foreground) { '1' } else { $null }
    $env:VQ3E_VULKAN_AUDIT_LOG = Join-Path $mfgAudit 'validation.log'
    $env:VK_INSTANCE_LAYERS = if ($NoValidation) { $null } else { 'VK_LAYER_KHRONOS_validation' }
    $env:VK_LAYER_PATH = 'C:/VulkanSDK/1.4.350.0/Bin'
    $mfgArgs = "+set fs_homepath `"$($mfgHome.Replace('\','/'))`" +set fs_game baseq3 +set cl_renderer vulkan +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set r_rayTracing 2 +set r_pathTracingScale 1 +set r_pathTracingSamples 2 +set r_pathTracingAdaptive 0 +set r_dlss 5 +set r_dlssRayReconstruction 1 +set r_dlssNeuralRendering 0 +set r_dlssFrameGeneration 1 +set r_dlssFrameGenerationMultiplier $Multiplier +set r_swapInterval 0 +set r_reflex 1 +set logfile 2 +set developer $Developer +set ui_scale 1 +devmap q3dm6 +exec pt_mfg_menu.cfg"
    $mfgArgs += " +set fs_basepath `"$($mfgBase.Replace('\','/'))`""
    # Windows PowerShell wraps native stderr in ErrorRecords when redirecting.
    # They are diagnostics, not launcher failures; use the real exit code.
    $ErrorActionPreference = 'Continue'
    try {
        & $mfgGuard (Join-Path $mfgBuild 'vQ3Evolution.exe') $mfgBuild $TimeLimitSeconds $mfgArgs *> (Join-Path $mfgAudit 'stdout.log')
        $mfgExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = 'Stop' }
    Write-Output "MFG functional run: ${Multiplier}x, exit $mfgExit, evidence $mfgAudit"
    Get-Content -LiteralPath (Join-Path $mfgAudit 'guard.log')
    Select-String -Path (Join-Path $mfgHome 'baseq3/qconsole.log') -Pattern 'PT_FG_|PT_MFG_|Frame Generation|NVIDIA window state|FG query history|shutdown complete'
    if ($mfgExit) { throw 'Run did not exit cleanly; inspect phase evidence separately from shutdown.' }
} finally {
    foreach ($mfgName in $mfgEnv.Keys) { [Environment]::SetEnvironmentVariable($mfgName,$mfgEnv[$mfgName],'Process') }
    foreach ($mfgPath in $mfgSettings.Keys) {
        if ((Get-FileHash -LiteralPath $mfgPath).Hash -ne $mfgSettings[$mfgPath]) { throw "Saved settings changed: $mfgPath" }
    }
}
