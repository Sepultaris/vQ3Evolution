param(
    [string]$BuildLog = 'build-widescreen/rt-audit/rr-lean-idle-build-20260909.log',
    [string]$Output = 'build-widescreen/rt-audit/rr-lean-idle-baseline-build-20260909',
    [string]$VulkanSDK = 'C:/VulkanSDK/1.4.350.0'
)
$ErrorActionPreference = 'Stop'
$taskRepo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRepo
$taskOutput = [IO.Path]::GetFullPath((Join-Path $taskRepo $Output))
if (!$taskOutput.StartsWith(($taskRepo + '\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be within this workspace.' }
if (Test-Path -LiteralPath $taskOutput) { throw 'Use a fresh baseline output directory.' }
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskLog = Get-Content -LiteralPath $BuildLog
$taskBackup = 'build-widescreen/rt-audit/rr-lean-backup-20260908'
$taskObjects = 'build-widescreen/release-mingw64-x86_64/renderer_vulkan'
$taskDll = 'build-widescreen/release-mingw64-x86_64/renderer_vulkan_x86_64.dll'
Copy-Item -LiteralPath $taskDll -Destination (Join-Path $taskOutput 'renderer_vulkan-combined.dll')
function Invoke-BuildCommand([string]$Command) {
    if ($Command.Length -gt 8000) {
        # Avoid the Windows shell command-length limit; this is generated
        # compiler input, not another source or a modified build configuration.
        $taskResponse = Join-Path $taskOutput 'renderer-link.rsp'
        [IO.File]::WriteAllText($taskResponse, ($Command -replace '^/ucrt64/bin/[^ ]+gcc ',''))
        & C:/msys64/ucrt64/bin/gcc.exe ('@' + $taskResponse)
    } else {
        & C:/msys64/usr/bin/bash.exe --noprofile --norc -c ('PATH=/ucrt64/bin:/usr/bin ' + $Command)
    }
    if ($LASTEXITCODE -ne 0) { throw 'Baseline compile/link failed.' }
}
function Get-CompileCommand([string]$Source) {
    $taskMatches = @($taskLog | Where-Object { $_ -match '^/ucrt64/bin/.*gcc ' -and $_.EndsWith(' -c ' + $Source) })
    if ($taskMatches.Count -ne 1) { throw "Expected one logged compile command for $Source" }
    return $taskMatches[0]
}
$taskCompile = Get-CompileCommand 'code/renderer_vulkan/vk_pathtrace.c'
$taskCompile = $taskCompile.Replace("$taskObjects/vk_pathtrace.o", "$Output/vk_pathtrace.o").Replace(' -c code/renderer_vulkan/vk_pathtrace.c', " -Icode/renderer_vulkan -c $taskBackup/vk_pathtrace.c")
Invoke-BuildCommand $taskCompile
# Compile the backed-up separated RR tracer with its backed-up integrator and
# the current, unchanged shared includes. All other objects (including the HD
# upload fix) come from the exact same candidate build.
& "$VulkanSDK/Bin/glslangValidator.exe" --target-env vulkan1.2 -V '-Icode/renderer_vulkan/shaders' "$taskBackup/pt_rr_trace.comp" -o "$Output/pt_rr_trace.cspv"
if ($LASTEXITCODE -ne 0) { throw 'Baseline shader compilation failed.' }
& "$VulkanSDK/Bin/spirv-opt.exe" --target-env=vulkan1.2 -O "$Output/pt_rr_trace.cspv" -o "$Output/pt_rr_trace.cspv"
if ($LASTEXITCODE -ne 0) { throw 'Baseline shader optimization failed.' }
& "$VulkanSDK/Bin/spirv-val.exe" --target-env vulkan1.2 "$Output/pt_rr_trace.cspv"
if ($LASTEXITCODE -ne 0) { throw 'Baseline shader validation failed.' }
& code/renderer_vulkan/shaders/Compiled/bintoc.exe "$Output/pt_rr_trace.cspv" pt_rr_trace_comp_spv "$Output/pt_rr_trace_comp.c"
if ($LASTEXITCODE -ne 0) { throw 'Baseline shader embedding failed.' }
$taskCompile = Get-CompileCommand 'code/renderer_vulkan/shaders/Compiled/pt_rr_trace_comp.c'
$taskCompile = $taskCompile.Replace("$taskObjects/pt_rr_trace_comp.o", "$Output/pt_rr_trace_comp.o").Replace(' -c code/renderer_vulkan/shaders/Compiled/pt_rr_trace_comp.c', " -c $Output/pt_rr_trace_comp.c")
Invoke-BuildCommand $taskCompile
$taskLink = @($taskLog | Where-Object { $_ -match '^/ucrt64/bin/.*gcc ' -and $_.Contains(' -o ' + $taskDll + ' ') })
if ($taskLink.Count -ne 1) { throw 'Expected one renderer link command.' }
$taskLine = [array]::IndexOf($taskLog, $taskLink[0])
$taskCommand = $taskLink[0]
while ($taskCommand.EndsWith('\')) { $taskCommand = $taskCommand.TrimEnd('\') + ' ' + $taskLog[++$taskLine].Trim() }
$taskCommand = $taskCommand.Replace($taskDll, "$Output/renderer_vulkan-separated.dll").Replace("$taskObjects/vk_pathtrace.o", "$Output/vk_pathtrace.o").Replace("$taskObjects/pt_rr_trace_comp.o", "$Output/pt_rr_trace_comp.o")
Invoke-BuildCommand $taskCommand
Write-Output "Built separated baseline and preserved combined candidate in $taskOutput; active renderer unchanged."
