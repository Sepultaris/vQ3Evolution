# Diagnostic metadata only: this never decides whether an A/B result is valid.
# Keep the full-run/sample-count acceptance rules in pt_material_program_check.py.
function Get-PtRunSummary {
    param([string[]]$Lines, [double]$ElapsedSeconds, [bool]$TimedOut, [int]$ExitCode)
    $ptPhases = [ordered]@{}
    $ptCurrentPhase = ''
    $ptCompiles = @()
    $ptPending = @{}
    $ptComplete = $false
    foreach ($ptLine in $Lines) {
        if ($ptLine -match '^PT_LIGHTING_PIPELINE_BEGIN mode=(\d+)$') {
            $ptPending[$Matches[1]] = $true
        } elseif ($ptLine -match '^PT_LIGHTING_PIPELINE_END mode=(\d+) cpu_ms=(\d+) result=(-?\d+)$') {
            $ptMode = $Matches[1]
            $ptCompiles += [ordered]@{ mode = [int]$ptMode; cpuMilliseconds = [int]$Matches[2]; result = [int]$Matches[3] }
            $ptPending.Remove($ptMode)
        } elseif ($ptLine -in @('PT_PROGRAM_COMPLETE', 'PT_PIPELINE_COMPLETE', 'PT_HOOKS_COMPLETE', 'PT_CACHE_COMPLETE')) {
            $ptComplete = $true
            $ptCurrentPhase = ''
        } elseif ($ptLine -match '^PT_PROGRAM_(REFERENCE_A|OPTIMIZED_A|OPTIMIZED_B|REFERENCE_B)$') {
            $ptCurrentPhase = $Matches[1]
            if (!$ptPhases.Contains($ptCurrentPhase)) { $ptPhases[$ptCurrentPhase] = 0 }
        } elseif ($ptLine -eq 'PT_PIPELINE_BASELINE') {
            $ptCurrentPhase = 'PIPELINE_BASELINE'
            if (!$ptPhases.Contains($ptCurrentPhase)) { $ptPhases[$ptCurrentPhase] = 0 }
        } elseif ($ptLine -eq 'PT_HOOKS_BEGIN') {
            $ptCurrentPhase = 'IDLE_HOOKS'
            if (!$ptPhases.Contains($ptCurrentPhase)) { $ptPhases[$ptCurrentPhase] = 0 }
        } elseif ($ptLine -match '^PT_PROFILE ' -and $ptCurrentPhase) {
            $ptPhases[$ptCurrentPhase]++
        }
    }
    return [ordered]@{
        elapsedSeconds = $ElapsedSeconds
        timedOut = $TimedOut
        exitCode = $ExitCode
        completionMarker = $ptComplete
        rawPhaseSamples = $ptPhases
        pipelineCompilations = @($ptCompiles)
        unfinishedPipelineModes = @($ptPending.Keys | Sort-Object | ForEach-Object { [int]$_ })
        diagnosticOnly = $true
    }
}
