param(
    [string]$VulkanSDK=$env:VULKAN_SDK,
    [string]$CC='gcc',
    [int[]]$DeviceIndices=@(0)
)
$ErrorActionPreference='Stop'
$rtRepo=Split-Path -Parent $PSScriptRoot
$rtOutput=Join-Path $rtRepo ('build-widescreen/software-check-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $rtOutput | Out-Null
function Invoke-Checked([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
if (!$VulkanSDK) { throw 'Set VULKAN_SDK or pass -VulkanSDK.' }
$rtBin=Join-Path $VulkanSDK 'Bin'
$rtShaders=@('pt_software','pt_software_guides','pt_software_temporal','pt_software_nrd','pt_software_nrd_guides',
    'pt_software_profile','pt_software_nrd_profile','pt_software_counts','pt_software_nrd_counts')
& (Join-Path $rtRepo 'code/renderer_vulkan/shaders/compile-raytracing.ps1') `
    -VulkanSDK $VulkanSDK -CC $CC -Shaders $rtShaders -OutputDirectory $rtOutput
foreach ($rtShader in $rtShaders) {
    $rtPayload=Join-Path $rtOutput ($rtShader+'.cspv')
    $rtAssembly=& (Join-Path $rtBin 'spirv-dis.exe') $rtPayload
    if ($LASTEXITCODE -ne 0) { throw 'Could not disassemble software shader.' }
    if ($rtAssembly -match 'OpCapability (RayQuery|RayTracing|PhysicalStorageBufferAddresses|Int64)|OpExtension "SPV_.*(ray|physical_storage)|OpRayQuery|OpTraceRay') {
        throw 'Hardware tracing or device-address capability leaked into the software shader.'
    }
    if ($rtAssembly -match 'OpDecorate.* Binding [12]$') {
        throw 'Software lighting must not depend on raster scene color/depth.'
    }
    $rtDiagnostic=$rtShader -match '_(profile|counts)$'
    if($rtDiagnostic -and $rtAssembly -match 'OpImageWrite|OpAtomic') { throw 'Diagnostic replay must not write images or use atomic counters.' }
    if($rtDiagnostic) {
        $rtStorageTypes=@{}; $rtPointerTypes=@{}; $rtPointerParents=@{}; $rtProfileSets=@{}; $rtProfileBindings=@{}
        foreach($rtLine in $rtAssembly) {
            if($rtLine -match '^\s*(%\S+) = OpTypePointer StorageBuffer ') { $rtStorageTypes[$Matches[1]]=$true }
            if($rtLine -match '^\s*(%\S+) = Op\S+ (%\S+)') { $rtPointerTypes[$Matches[1]]=$Matches[2] }
            if($rtLine -match '^\s*(%\S+) = Op(?:InBounds)?AccessChain %\S+ (%\S+)') { $rtPointerParents[$Matches[1]]=$Matches[2] }
            if($rtLine -match 'OpDecorate (%\S+) DescriptorSet 1$') { $rtProfileSets[$Matches[1]]=$true }
            if($rtLine -match 'OpDecorate (%\S+) Binding 0$') { $rtProfileBindings[$Matches[1]]=$true }
        }
        $rtProfileStores=0
        foreach($rtLine in $rtAssembly) {
            if($rtLine -notmatch '^\s*OpStore (%\S+) ') { continue }
            $rtPointer=$Matches[1]; $rtType=$rtPointerTypes[$rtPointer]
            if(!$rtType -or !$rtStorageTypes.ContainsKey($rtType)) { continue }
            while($rtPointerParents.ContainsKey($rtPointer)) { $rtPointer=$rtPointerParents[$rtPointer] }
            if(!$rtProfileSets.ContainsKey($rtPointer) -or !$rtProfileBindings.ContainsKey($rtPointer)) {
                throw "Diagnostic replay writes a production storage buffer: $rtPointer"
            }
            ++$rtProfileStores
        }
        if(!$rtProfileStores) { throw 'Diagnostic shader does not store its measurements.' }
    }
    if($rtShader -notmatch '_profile$' -and $rtAssembly -match 'OpCapability ShaderClockKHR|OpReadClockKHR') {
        throw 'Shader clocks leaked into normal/counters-only rendering.'
    }
    if($rtShader -match '_profile$' -and !($rtAssembly -match 'OpReadClockKHR')) { throw 'Timed diagnostic has no shader clock.' }
    foreach ($rtSuffix in @('.cspv','_comp.c')) {
        $rtShipping=Join-Path $rtRepo ('code/renderer_vulkan/shaders/Compiled/'+$rtShader+$rtSuffix)
        if ((Get-FileHash (Join-Path $rtOutput ($rtShader+$rtSuffix))).Hash -ne (Get-FileHash $rtShipping).Hash) {
            throw "Embedded software shader is stale: $rtShader$rtSuffix"
        }
    }
}
foreach ($rtFixture in @('rt_software_trace_check','rt_software_query_check')) {
    $rtSpv=Join-Path $rtOutput ($rtFixture+'.spv')
    Invoke-Checked (Join-Path $rtBin 'glslangValidator.exe') @('--target-env','vulkan1.2','-V',
        (Join-Path $PSScriptRoot ($rtFixture+'.comp')),'-o',$rtSpv)
    Invoke-Checked (Join-Path $rtBin 'spirv-opt.exe') @('--target-env=vulkan1.2','-O',$rtSpv,'-o',$rtSpv)
    Invoke-Checked (Join-Path $rtBin 'spirv-val.exe') @('--target-env','vulkan1.2',$rtSpv)
    $rtExe=Join-Path $rtOutput ($rtFixture+'.exe')
    Invoke-Checked $CC @('-std=c11','-Wall','-Wextra','-O2',('-I'+(Join-Path $VulkanSDK 'Include')),
        (Join-Path $PSScriptRoot ($rtFixture+'.c')),(Join-Path $VulkanSDK 'Lib/vulkan-1.lib'),'-o',$rtExe)
    foreach($rtIndex in $DeviceIndices) { Invoke-Checked $rtExe @($rtSpv,"$rtIndex") }
}
Write-Output 'PASS: reproducible full-lighting payloads, no hardware RT/raster-lighting inputs, BVH and resumable material queries'
