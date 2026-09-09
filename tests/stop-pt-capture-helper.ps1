# Scoped cleanup for a completed capture only; run as administrator.
param(
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label,
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$OwnerPid
)
$ErrorActionPreference='Stop'
$ptRepo=Split-Path -Parent $PSScriptRoot
$ptDirectory=Join-Path $ptRepo "build-widescreen/rt-audit/performance-$Label"
$ptCleanup=[ordered]@{owner=$OwnerPid; closed=$false; error=$null}
try {
    $ptEvidence=Get-Content -Raw -LiteralPath (Join-Path $ptDirectory 'nsight/guard.log')
    if ($ptEvidence -notmatch 'VQ3E_BOUNDED exit=\d+ elapsed_ms=\d+ timed_out=\d+') { throw 'No completed child-exit evidence.' }
    $ptHelperInfo=Get-CimInstance Win32_Process -Filter "ProcessId=$OwnerPid"
    $ptCleanup['observed']= $ptHelperInfo | Select-Object ProcessId,ExecutablePath,CommandLine,CreationDate
    if (!$ptHelperInfo) { $ptCleanup.closed=$true }
    else {
        $ptExpected=@('pt-profile-guard.exe','pt-bounded-process.exe') | ForEach-Object {
            [IO.Path]::GetFullPath((Join-Path $ptRepo "build-widescreen/rt-audit/tools/$_"))
        }
        if ($ptHelperInfo.ExecutablePath -notin $ptExpected -or !$ptHelperInfo.CommandLine.Contains($ptDirectory.Replace('\','/'))) {
            throw 'Executable and unique capture command line did not establish ownership; nothing terminated.'
        }
        # A process in native DLL teardown may remain in Windows' process list
        # while .NET GetProcessById rejects it. Query the owned handle directly.
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PtOwnedHelper {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool TerminateProcess(IntPtr process,uint code);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr process,uint ms);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
        $ptHandle=[PtOwnedHelper]::OpenProcess(0x101001,$false,$OwnerPid)
        if ($ptHandle -eq [IntPtr]::Zero) { throw "Cannot open verified helper: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
        try {
            [uint32]$ptExit=0
            if (![PtOwnedHelper]::GetExitCodeProcess($ptHandle,[ref]$ptExit)) { throw 'Cannot query verified helper exit code' }
            $ptCleanup['nativeExitCode']=$ptExit
            if ($ptExit -eq 259 -and ![PtOwnedHelper]::TerminateProcess($ptHandle,0)) { throw "Cannot terminate verified helper: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
            $ptCleanup.closed=[PtOwnedHelper]::WaitForSingleObject($ptHandle,5000) -eq 0
            if (!$ptCleanup.closed -and $ptExit -eq 0) {
                # An exited debuggee can remain unsignaled while its Nsight
                # launcher retains the exit debug event. Validate that exact
                # freshly-created parent, not a global profiler/service search.
                $ptParent=Get-CimInstance Win32_Process -Filter "ProcessId=$($ptHelperInfo.ParentProcessId)"
                $ptCleanup['profilerParent']=$ptParent | Select-Object ProcessId,ExecutablePath,CommandLine,CreationDate
                $ptExpectedProfiler='C:\Program Files\NVIDIA Corporation\Nsight Systems 2025.6.3\target-windows-x64\nsys.exe'
                $ptParentAge=if($ptParent){($ptHelperInfo.CreationDate-$ptParent.CreationDate).TotalSeconds}else{-1}
                if ($ptParent -and $ptParent.ExecutablePath -ieq $ptExpectedProfiler -and $ptParentAge -ge 0 -and $ptParentAge -lt 5) {
                    $ptParentHandle=[PtOwnedHelper]::OpenProcess(0x101001,$false,$ptParent.ProcessId)
                    if ($ptParentHandle -ne [IntPtr]::Zero) {
                        try {
                            $ptCleanup['profilerParentTerminated']=[PtOwnedHelper]::TerminateProcess($ptParentHandle,0)
                            [PtOwnedHelper]::WaitForSingleObject($ptParentHandle,2000) | Out-Null
                        } finally { [PtOwnedHelper]::CloseHandle($ptParentHandle) | Out-Null }
                        $ptCleanup.closed=[PtOwnedHelper]::WaitForSingleObject($ptHandle,5000) -eq 0
                    }
                }
            }
        } finally { [PtOwnedHelper]::CloseHandle($ptHandle) | Out-Null }
    }
} catch { $ptCleanup.error=$_.Exception.Message }
$ptCleanup | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ptDirectory 'nsight/helper-cleanup.json') -Encoding UTF8
