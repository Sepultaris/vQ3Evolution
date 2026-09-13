param([string]$VulkanSDK=$env:VULKAN_SDK)
$ErrorActionPreference='Stop'
$nrdRepo=Split-Path $PSScriptRoot -Parent
$nrdSource=Join-Path $nrdRepo 'build-widescreen/nrd-source'
$nrdBuild=Join-Path $nrdSource '_Build'
$nrdAdapter=Join-Path $nrdRepo 'build-widescreen/nrd-adapter'
$nrdCommit='bf877181058988ec5785f82c4189be0be75c1902'
if(!$VulkanSDK -or !(Test-Path -LiteralPath (Join-Path $VulkanSDK 'Bin/dxc.exe'))) {
    throw 'Pass -VulkanSDK with an installed Vulkan SDK containing Bin/dxc.exe.'
}
function Invoke-Nrd([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
}
if(!(Test-Path -LiteralPath (Join-Path $nrdSource 'Include/NRD.h'))) {
    throw 'Download NRD into build-widescreen/nrd-source and review its LICENSE.txt first. See docs/SOFTWARE_DENOISING.md.'
}
$nrdRevision=& git -C $nrdSource rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $nrdRevision -ne $nrdCommit) { throw "NRD must be pinned to $nrdCommit" }
Invoke-Nrd cmake @('-S',$nrdSource,'-B',$nrdBuild,'-DNRD_EMBEDS_DXBC_SHADERS=OFF',
    '-DNRD_EMBEDS_DXIL_SHADERS=OFF','-DNRD_SUPPORTS_QUAD_INTRINSICS=OFF',
    '-DNRD_NRI=OFF','-DNRD_NORMAL_ENCODING=3','-DNRD_STATIC_LIBRARY=ON')
Invoke-Nrd cmake @('--build',$nrdBuild,'--config','Release','--parallel','4')
Invoke-Nrd cmake @('-S',(Join-Path $nrdRepo 'code/renderer_vulkan/nrd'),'-B',$nrdAdapter,
    "-DNRD_SOURCE=$nrdSource","-DNRD_BUILD=$nrdBuild","-DDXC=$VulkanSDK/Bin/dxc.exe")
Invoke-Nrd cmake @('--build',$nrdAdapter,'--config','Release','--parallel','4')
Write-Output "Local evaluation adapter: $nrdAdapter/Release/vq3e_nrd.dll"
