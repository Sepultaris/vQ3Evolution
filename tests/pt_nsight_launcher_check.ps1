# No game, injection, elevation or GPU. Exercise the real native guard.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'run-pt-nsight.ps1') -FunctionsOnly
$ptTestDirectory = New-Item -ItemType Directory -Path (Join-Path $ptAudit ('tools/guard-check-' + [guid]::NewGuid().ToString('N')))
$ptFixture = Join-Path $ptAudit 'tools/pt_bounded_process_check.exe'
function Start-PtGuardTest([string]$Name, [string]$Arguments) {
    $ptInfo = [Diagnostics.ProcessStartInfo]::new()
    $ptInfo.FileName=$ptGuard; $ptInfo.WorkingDirectory=$ptRepo
    $ptInfo.UseShellExecute=$false; $ptInfo.CreateNoWindow=$true
    $ptInfo.RedirectStandardOutput=$true; $ptInfo.RedirectStandardError=$true
    $ptInfo.Environment['VQ3E_BOUNDED_LOG']=Join-Path $ptTestDirectory "$Name.log"
    foreach ($ptArg in @($ptFixture,$ptRepo,'45',$Arguments)) { $ptInfo.ArgumentList.Add($ptArg) }
    [Diagnostics.Process]::Start($ptInfo)
}
$ptQuoted = Start-PtGuardTest 'quoted' ('--argument ' + (ConvertTo-PtArgument 'space "quoted" C:\tail\'))
if (!$ptQuoted.WaitForExit(5000)) { $ptQuoted.Kill($true); throw 'Quoting test hung' }
if ($ptQuoted.ExitCode -ne 23) { throw "Argument roundtrip failed: $($ptQuoted.ExitCode)" }
$ptLate = Start-PtGuardTest 'late-exit' '--delay'
$ptLateResult = Wait-PtGuardResult -Path (Join-Path $ptTestDirectory 'late-exit.log') -TimeoutSeconds 2
if (!$ptLateResult -or $ptLateResult.exit -ne 23 -or $ptLateResult.timedOut) { throw 'Late guard flush was not observed' }
if ($ptLateResult.owner -ne $ptLate.Id) { throw 'Guard did not record its own process ID' }
$ptLate.WaitForExit()
if (Wait-PtGuardResult -Path (Join-Path $ptTestDirectory 'missing.log') -TimeoutSeconds 1) { throw 'Missing guard evidence was accepted' }
$ptOwned = Start-PtGuardTest 'crash' '--sleep'
$ptChild=$null
try {
    $ptTimer=[Diagnostics.Stopwatch]::StartNew()
    while (!$ptChild -and $ptTimer.Elapsed.TotalSeconds -lt 3) {
        $ptLines=Get-Content -LiteralPath (Join-Path $ptTestDirectory 'crash.log') -ErrorAction SilentlyContinue
        if ($ptLines -match 'VQ3E_BOUNDED pid=(\d+)') {
            $ptChild=[Diagnostics.Process]::GetProcessById([int]([regex]::Match(($ptLines -join ' '),'pid=(\d+)').Groups[1].Value))
            # Hold its handle so PID recycling cannot affect this test.
            $null=$ptChild.Handle
        }
        if (!$ptChild) { Start-Sleep -Milliseconds 20 }
    }
    if (!$ptChild) { throw 'No owned child PID reported' }
    $ptOwned.Kill(); $ptOwned.WaitForExit()
    if (!$ptChild.WaitForExit(2000)) { throw 'Guard crash left its child alive' }
    Write-Output 'PASS: quoting roundtrip, late/missing guard evidence; killing the guard kills its owned child.'
} finally {
    if (!$ptOwned.HasExited) { $ptOwned.Kill($true) }
    if ($ptChild -and !$ptChild.HasExited) { $ptChild.Kill() }
}
