param(
    [string]$VulkanSDK = $env:VULKAN_SDK,
    [string]$CC = 'gcc',
    [switch]$Raster,
    [switch]$DebugSymbols,
    [string]$OutputDirectory = '',
    [string[]]$Shaders = @('rt_shadows', 'rt_shadows_cpu', 'pt_software', 'pt_software_guides', 'pathtrace', 'pt_brdf', 'pt_profile', 'pt_profile_brdf', 'pt_light_loop', 'pt_light_loop_brdf', 'pt_light_loop_profile', 'pt_light_loop_profile_brdf', 'pt_cached_materials', 'pt_cached_materials_brdf', 'pt_cached_materials_loop', 'pt_cached_materials_loop_brdf', 'pt_material_cache', 'pt_compact_transport', 'pt_staged_0', 'pt_staged_1', 'pt_staged_2', 'pt_staged_3', 'pt_guides', 'pt_rr_guides', 'pt_rr_pack', 'pt_rr_post', 'pt_denoise', 'pt_temporal', 'pt_exposure', 'pt_exposure_rr', 'post_NV')
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($VulkanSDK)) {
    throw 'Set VULKAN_SDK or pass -VulkanSDK with the installed Vulkan SDK directory.'
}
$ptCompiler = Join-Path $VulkanSDK 'Bin/glslangValidator.exe'
$ptValidator = Join-Path $VulkanSDK 'Bin/spirv-val.exe'
$ptOptimizer = Join-Path $VulkanSDK 'Bin/spirv-opt.exe'
$ptOutput = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $PSScriptRoot 'Compiled' }
$ptConverter = Join-Path $ptOutput 'bintoc.exe'
if (-not (Test-Path -LiteralPath $ptOutput)) {
    New-Item -ItemType Directory -Path $ptOutput | Out-Null
}
function Invoke-Checked([string]$Program, [string[]]$ToolArguments) {
    & $Program @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Invoke-Checked $CC @((Join-Path $PSScriptRoot 'bintoc.c'), '-o', $ptConverter)
if (-not $PSBoundParameters.ContainsKey('Shaders')) { $Shaders += @('pt_rr_trace', 'pt_rr_spatial', 'pt_rr_fallback') }
if (-not $PSBoundParameters.ContainsKey('Shaders')) { $Shaders += @('pt_software_temporal', 'pt_software_nrd', 'pt_software_nrd_guides', 'pt_software_profile', 'pt_software_nrd_profile', 'pt_software_counts', 'pt_software_nrd_counts') }
foreach ($ptShader in $Shaders) {
    if ($ptShader -notmatch '^[a-z][a-z0-9_]+$') { throw 'Shader must be a simple source basename.' }
    $ptSource = Join-Path $PSScriptRoot "$ptShader.comp"
    $ptBinary = Join-Path $ptOutput "$ptShader.cspv"
    $ptEmbedded = Join-Path $ptOutput "${ptShader}_comp.c"
    $ptCompilerArgs = @('--target-env', 'vulkan1.2', '-V', $ptSource, '-o', $ptBinary)
    # Source/line metadata for Nsight; keep the SAME optimizing pass below.
    if ($DebugSymbols) { $ptCompilerArgs += '-g' }
    Invoke-Checked $ptCompiler $ptCompilerArgs
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
