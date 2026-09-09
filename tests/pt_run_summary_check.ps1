$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'pt-run-summary.ps1')
function Assert-PtSummary([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
$ptLines = @(
    'PT_LIGHTING_PIPELINE_BEGIN mode=1',
    'PT_LIGHTING_PIPELINE_END mode=1 cpu_ms=3500 result=0',
    'PT_PROGRAM_REFERENCE_A', 'PT_PROFILE frame=72', 'PT_PROFILE frame=72',
    'PT_LIGHTING_PIPELINE_BEGIN mode=3'
)
$ptResult = Get-PtRunSummary $ptLines 45.2 $true -1
Assert-PtSummary ($ptResult.timedOut -and !$ptResult.completionMarker) 'A timeout must not become a completed comparison.'
Assert-PtSummary ($ptResult.pipelineCompilations.Count -eq 1 -and $ptResult.pipelineCompilations[0].cpuMilliseconds -eq 3500) 'Completed pipeline timing was lost.'
Assert-PtSummary ($ptResult.unfinishedPipelineModes.Count -eq 1 -and $ptResult.unfinishedPipelineModes[0] -eq 3) 'Interrupted compilation was not reported.'
Assert-PtSummary ($ptResult.rawPhaseSamples['REFERENCE_A'] -eq 2) 'Raw phase count is incorrect.'
Assert-PtSummary $ptResult.diagnosticOnly 'Metadata must not replace full benchmark acceptance.'
$ptLines += @('PT_LIGHTING_PIPELINE_END mode=3 cpu_ms=100 result=-2', 'PT_PROGRAM_OPTIMIZED_A', 'PT_PROFILE frame=72', 'PT_PROGRAM_COMPLETE', 'PT_PROFILE frame=72')
$ptResult = Get-PtRunSummary $ptLines 19.0 $false 0
Assert-PtSummary ($ptResult.unfinishedPipelineModes.Count -eq 0 -and $ptResult.pipelineCompilations[1].result -eq -2) 'A completed failed compilation must be reported, not left pending.'
Assert-PtSummary ($ptResult.completionMarker -and $ptResult.rawPhaseSamples['OPTIMIZED_A'] -eq 1) 'Post-completion timing leaked into a phase.'
$ptResult = Get-PtRunSummary @('PT_PROGRAM_REFERENCE_A', 'PT_PROFILE frame=75') 45.0 $true -1
Assert-PtSummary ($ptResult.pipelineCompilations.Count -eq 0 -and $ptResult.unfinishedPipelineModes.Count -eq 0) 'Old logs must not invent compilation timing.'
$ptResult = Get-PtRunSummary @() 0.1 $false 1
Assert-PtSummary (!$ptResult.completionMarker -and $ptResult.rawPhaseSamples.Count -eq 0) 'Empty failed run must stay empty.'
Write-Output 'PASS: diagnostic summaries handle completed, interrupted, failed and absent compilation; timeout and phase boundaries retained.'
$ptResult = Get-PtRunSummary @('PT_PIPELINE_BASELINE', 'PT_PROFILE frame=80', 'PT_PIPELINE_COMPLETE', 'PT_PROFILE frame=80') 14.3 $false 0
Assert-PtSummary ($ptResult.completionMarker -and $ptResult.rawPhaseSamples['PIPELINE_BASELINE'] -eq 1) 'Compile-only baseline markers were not recognized.'
$ptResult = Get-PtRunSummary @('PT_HOOKS_BEGIN', 'PT_PROFILE frame=70', 'PT_HOOKS_COMPLETE', 'PT_PROFILE frame=70') 14.0 $false 0
Assert-PtSummary ($ptResult.completionMarker -and $ptResult.rawPhaseSamples['IDLE_HOOKS'] -eq 1) 'Idle-hook phase boundaries were not recognized.'
$ptResult = Get-PtRunSummary @('PT_CACHE_STATIONARY', 'PT_CACHE_MOVED', 'PT_CACHE_COMPLETE') 16.0 $false 0
Assert-PtSummary ($ptResult.completionMarker -and $ptResult.rawPhaseSamples.Count -eq 0) 'Non-timing cache validation marker was not recognized.'
