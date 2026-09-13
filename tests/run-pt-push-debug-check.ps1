param([string]$Compiler='C:/msys64/ucrt64/bin/gcc.exe')
$ErrorActionPreference='Stop'
$pushRepo=Split-Path $PSScriptRoot -Parent
$pushAudit=Join-Path $pushRepo 'build-widescreen/config-write-audit'
New-Item -ItemType Directory -Path $pushAudit -Force | Out-Null
foreach($pushMode in @('normal','release')) {
    [string[]]$pushFlags=if($pushMode -eq 'release') { @('-O3','-ffast-math') } else { @('-O2') }
    $pushExe=Join-Path $pushAudit "pt-push-$pushMode.exe"
    & $Compiler @pushFlags (Join-Path $PSScriptRoot 'pt_push_debug_fixture.c') -o $pushExe
    if($LASTEXITCODE) { throw 'Push-log fixture compilation failed.' }
    & $pushExe
    if($LASTEXITCODE) { throw 'Push-log lifetime regression failed.' }
}
