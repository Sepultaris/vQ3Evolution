param([string]$Compiler = 'C:/msys64/ucrt64/bin/gcc.exe')
$ErrorActionPreference='Stop'
$flashRepo=Split-Path $PSScriptRoot -Parent
$flashOutput=Join-Path $flashRepo 'build-widescreen/muzzle-flash-audit'
New-Item -ItemType Directory -Path $flashOutput -Force | Out-Null
foreach($flashMode in @('normal','release')) {
    [string[]]$flashFlags=if($flashMode -eq 'release') { @('-O3','-ffast-math') } else { @('-O2') }
    $flashExe=Join-Path $flashOutput "scale-$flashMode.exe"
    & $Compiler @flashFlags (Join-Path $PSScriptRoot 'muzzle_flash_fixture.c') -o $flashExe
    if($LASTEXITCODE) { throw 'Muzzle scale fixture compilation failed.' }
    & $flashExe
    if($LASTEXITCODE) { throw 'Muzzle scale fixture failed.' }
}
