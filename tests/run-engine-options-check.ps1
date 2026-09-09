param([string]$Compiler = 'C:/msys64/ucrt64/bin/gcc.exe')
$ErrorActionPreference = 'Stop'
$optionsRepo = Split-Path -Parent $PSScriptRoot
$optionsOutput = Join-Path $optionsRepo 'build-widescreen/rt-audit'
New-Item -ItemType Directory -Path $optionsOutput -Force | Out-Null
Push-Location $optionsRepo
try {
    foreach ($optionsMode in @('normal','release')) {
        [string[]]$optionsFlags = if ($optionsMode -eq 'release') { @('-O3','-ffast-math') } else { @('-O2') }
        $optionsExe = Join-Path $optionsOutput "engine-options-$optionsMode.exe"
        & $Compiler @optionsFlags -DUSE_LOCAL_HEADERS -Icode/SDL2/include tests/engine_options_fixture.c code/qcommon/q_shared.c -o $optionsExe
        if ($LASTEXITCODE) { throw 'Options fixture compilation failed.' }
        & $optionsExe
        if ($LASTEXITCODE) { throw 'Options fixture failed.' }
        $optionsExe = Join-Path $optionsOutput "engine-vm-search-$optionsMode.exe"
        & $Compiler @optionsFlags tests/engine_vm_search_fixture.c -o $optionsExe
        if ($LASTEXITCODE) { throw 'VM lookup fixture compilation failed.' }
        & $optionsExe
        if ($LASTEXITCODE) { throw 'VM lookup fixture failed.' }
    }
} finally { Pop-Location }
