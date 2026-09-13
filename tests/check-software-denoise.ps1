param(
    [string]$VulkanSDK=$env:VULKAN_SDK,
    [int[]]$DeviceIndices=@(0),
    [switch]$Validation
)
$ErrorActionPreference='Stop'
$denoiseRepo=Split-Path $PSScriptRoot -Parent
$denoiseExe=Join-Path $denoiseRepo 'build-widescreen/nrd-adapter/Release/software_denoise_check.exe'
$denoiseShader=Join-Path $denoiseRepo 'code/renderer_vulkan/shaders/Compiled/pt_software_temporal.cspv'
if(!(Test-Path -LiteralPath $denoiseExe)) { throw 'Build the optional adapter first with tools/build-software-nrd.ps1.' }
if($Validation -and !$VulkanSDK) { throw 'Validation requires -VulkanSDK or VULKAN_SDK.' }
$denoiseAudit=Join-Path $denoiseRepo ('build-widescreen/denoiser-check-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $denoiseAudit | Out-Null
$denoiseSaved=@{}
foreach($denoiseKey in @('VK_INSTANCE_LAYERS','VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH')) {
    $denoiseSaved[$denoiseKey]=[Environment]::GetEnvironmentVariable($denoiseKey,'Process')
}
try {
    $env:VK_INSTANCE_LAYERS=if($Validation) { 'VK_LAYER_KHRONOS_validation' } else { '' }
    if($Validation) {
        $env:VK_LAYER_PATH=Join-Path $VulkanSDK 'Bin'
        $env:VK_LAYER_SETTINGS_PATH=Join-Path $PSScriptRoot 'vulkan-software-validation'
    }
    foreach($denoiseDevice in $DeviceIndices) {
        $denoiseRun=Join-Path $denoiseAudit "device-$denoiseDevice"
        New-Item -ItemType Directory -Path $denoiseRun | Out-Null
        Push-Location $denoiseRun
        try {
            & $denoiseExe $denoiseDevice $denoiseShader | Tee-Object -FilePath result.log
            if($LASTEXITCODE -ne 0) { throw "Denoiser fixture failed on device $denoiseDevice." }
            if($Validation -and !(Test-Path -LiteralPath vulkan-validation.log)) { throw 'Validation log was not created.' }
            if((Test-Path -LiteralPath vulkan-validation.log) -and
                (Select-String -LiteralPath vulkan-validation.log -Pattern 'VUID-|SYNC-HAZARD|Validation Error')) {
                throw "Vulkan validation failed on device $denoiseDevice."
            }
        } finally { Pop-Location }
    }
    Write-Output "PASS: software temporal/NRD fixtures; results: $denoiseAudit"
} finally {
    foreach($denoiseKey in $denoiseSaved.Keys) { [Environment]::SetEnvironmentVariable($denoiseKey,$denoiseSaved[$denoiseKey],'Process') }
}
