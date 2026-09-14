param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot '../postfx'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-widescreen/release-mingw64-x86_64/postfx'),
    [string]$VulkanSDK = $env:VULKAN_SDK
)
$ErrorActionPreference='Stop'
if (!$VulkanSDK) { throw 'Set VULKAN_SDK or pass -VulkanSDK pointing to the installed SDK.' }
$fxCompiler=Join-Path $VulkanSDK 'Bin/glslangValidator.exe'
$fxValidator=Join-Path $VulkanSDK 'Bin/spirv-val.exe'
if (!(Test-Path -LiteralPath $fxCompiler) -or !(Test-Path -LiteralPath $fxValidator)) { throw 'Vulkan shader compiler/validator not found.' }
$fxSource=(Resolve-Path -LiteralPath $SourceDirectory).Path
$fxDestination=[IO.Path]::GetFullPath($OutputDirectory)
if ($fxSource.TrimEnd('\','/') -eq $fxDestination.TrimEnd('\','/')) { throw 'Choose a separate output directory; source files must be preserved.' }
New-Item -ItemType Directory -Path $fxDestination -Force | Out-Null
$fxStaging=Join-Path ([IO.Path]::GetTempPath()) ('vq3e-postfx-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fxStaging | Out-Null
# Compile/validate everything before installing any file. No engine rebuild.
# Retain staging on errors for inspection; do not run a shell supplied by a package.
$fxShaders=Get-ChildItem -LiteralPath $fxSource -File | Where-Object Extension -In '.comp','.vert','.frag'
if (!$fxShaders) { throw 'No GLSL compute/vertex/fragment files found.' }
foreach ($fxShader in $fxShaders) {
    $fxBinary=Join-Path $fxStaging ($fxShader.Name+'.spv')
    & $fxCompiler --target-env vulkan1.0 -V $fxShader.FullName -o $fxBinary
    if ($LASTEXITCODE) { throw "Shader compilation failed: $($fxShader.Name). Previous installed files preserved." }
    & $fxValidator --target-env vulkan1.0 $fxBinary
    if ($LASTEXITCODE) { throw "SPIR-V validation failed: $($fxShader.Name). Previous installed files preserved." }
}
# Texture inputs are copied losslessly, never baked into SPIR-V. Keep file and
# decoded-dimension limits consistent with the local loader before installation.
$fxTextures=Get-ChildItem -LiteralPath $fxSource -File -Filter '*.png'
foreach ($fxTexture in $fxTextures) {
    if ($fxTexture.Length -gt 32*1024*1024 -or $fxTexture.Length -lt 33) { throw "Invalid PNG size: $($fxTexture.Name)" }
    $fxBytes=[IO.File]::ReadAllBytes($fxTexture.FullName)
    if ([BitConverter]::ToString($fxBytes,0,8) -ne '89-50-4E-47-0D-0A-1A-0A' -or
        [Text.Encoding]::ASCII.GetString($fxBytes,12,4) -ne 'IHDR') { throw "Invalid PNG header: $($fxTexture.Name)" }
    $fxWidth=([uint64]$fxBytes[16]*16777216)+([uint64]$fxBytes[17]*65536)+([uint64]$fxBytes[18]*256)+$fxBytes[19]
    $fxHeight=([uint64]$fxBytes[20]*16777216)+([uint64]$fxBytes[21]*65536)+([uint64]$fxBytes[22]*256)+$fxBytes[23]
    if ($fxWidth -lt 1 -or $fxHeight -lt 1 -or $fxWidth -gt 4096 -or $fxHeight -gt 4096) { throw "PNG dimensions outside 1-4096: $($fxTexture.Name)" }
}
Get-ChildItem -LiteralPath $fxStaging -Filter '*.spv' -File | Copy-Item -Destination $fxDestination
Get-ChildItem -LiteralPath $fxSource -File | Where-Object Extension -In '.effect','.comp','.vert','.frag','.glsl','.png' | Copy-Item -Destination $fxDestination
Write-Output "Post-effect packages compiled and installed in $fxDestination"
Write-Output 'Use postfx_reload to reload; select effects in Shift+F10 > Effects. Fresh effect defaults are off; saved legacy bloom preferences are imported at startup.'
