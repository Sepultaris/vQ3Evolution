param(
    [string]$VulkanSDK = $env:VULKAN_SDK,
    [string]$CC = 'gcc',
    [switch]$Raster
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($VulkanSDK)) {
    throw 'Set VULKAN_SDK or pass -VulkanSDK with the installed Vulkan SDK directory.'
}
$ptCompiler = Join-Path $VulkanSDK 'Bin/glslangValidator.exe'
$ptValidator = Join-Path $VulkanSDK 'Bin/spirv-val.exe'
$ptOptimizer = Join-Path $VulkanSDK 'Bin/spirv-opt.exe'
$ptOutput = Join-Path $PSScriptRoot 'Compiled'
$ptConverter = Join-Path $ptOutput 'bintoc.exe'
if (-not (Test-Path -LiteralPath $ptOutput)) {
    New-Item -ItemType Directory -Path $ptOutput | Out-Null
}
function Invoke-Checked([string]$Program, [string[]]$ToolArguments) {
    & $Program @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Invoke-Checked $CC @((Join-Path $PSScriptRoot 'bintoc.c'), '-o', $ptConverter)
foreach ($ptShader in @('rt_shadows', 'pathtrace', 'pt_denoise', 'pt_temporal')) {
    $ptSource = Join-Path $PSScriptRoot "$ptShader.comp"
    $ptBinary = Join-Path $ptOutput "$ptShader.cspv"
    $ptEmbedded = Join-Path $ptOutput "${ptShader}_comp.c"
    Invoke-Checked $ptCompiler @('--target-env', 'vulkan1.2', '-V', $ptSource, '-o', $ptBinary)
    Invoke-Checked $ptOptimizer @('--target-env=vulkan1.2', '-O', $ptBinary, '-o', $ptBinary)
    Invoke-Checked $ptValidator @('--target-env', 'vulkan1.2', $ptBinary)
    Invoke-Checked $ptConverter @($ptBinary, "${ptShader}_comp_spv", $ptEmbedded)
}
if ($Raster) {
    foreach ($ptSource in Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.vert','.frag' }) {
        $ptSuffix = if ($ptSource.Extension -eq '.vert') { 'vspv' } else { 'fspv' }
        $ptStage = $ptSource.Extension.TrimStart('.')
        $ptBinary = Join-Path $ptOutput "$($ptSource.BaseName).$ptSuffix"
        $ptSymbol = "$($ptSource.BaseName)_${ptStage}_spv"
        Invoke-Checked $ptCompiler @('--target-env', 'vulkan1.2', '-V', $ptSource.FullName, '-o', $ptBinary)
        Invoke-Checked $ptValidator @('--target-env', 'vulkan1.2', $ptBinary)
        Invoke-Checked $ptConverter @($ptBinary, $ptSymbol, (Join-Path $ptOutput "$($ptSource.BaseName)_$ptStage.c"))
    }
}
