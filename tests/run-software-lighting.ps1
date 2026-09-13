param(
    [string]$Renderer = 'build-widescreen/software-rt-build/renderer_vulkan_x86_64.dll',
    [ValidateSet(0,1,2)][int]$Mode = 1,
    [string]$Map = 'q3dm6',
    [ValidateSet('baseq3','Q3UT3')][string]$Game = 'baseq3',
    [string]$Scenario = 'rt_software_lighting.cfg',
    [int]$Width = 960, [int]$Height = 540,
    [switch]$Validation,
    [switch]$ReferenceKernel,
    [ValidateSet(0,1)][int]$Denoiser = 0,
    [ValidateSet(0,1)][int]$LocalHistory = 1,
    [switch]$MissingDenoiser,
    [switch]$UseSavedSettings,
    [switch]$NativeGame,
    [switch]$SavedNvidiaSettings,
    [switch]$RayReconstruction,
    [switch]$Dlaa,
    [switch]$AllowShutdownTimeout,
    [ValidateSet(0,1,2)][int]$ShaderDiagnostic = 0,
    [ValidateRange(15,90)][int]$TimeLimitSeconds = 45,
    [string]$CC = 'gcc'
)
$ErrorActionPreference = 'Stop'
if($RayReconstruction -and $Mode -ne 2) { throw 'Ray Reconstruction requires mode 2.' }
$swRepo = Split-Path $PSScriptRoot -Parent
$swAssets = Join-Path $swRepo 'build-widescreen/release-mingw64-x86_64'
$swAudit = Join-Path $swRepo ('build-widescreen/software-lighting-audit/run-'+[guid]::NewGuid().ToString('N'))
$swGame = Join-Path $swAudit 'game'
$swHome = Join-Path $swAudit 'home'
if($SavedNvidiaSettings -and (!$UseSavedSettings -or $Mode -ne 2 -or $Scenario -ne 'rt_config_write.cfg')) {
    throw 'Saved NVIDIA settings are only supported for the mode-2 config-write diagnostic, not performance tests.'
}
if (Get-Process -Name vQ3Evolution -ErrorAction SilentlyContinue) { throw 'Close the running game before testing.' }
if ($Map -notmatch '^[a-zA-Z0-9_]+$') { throw 'Use a simple map name.' }
New-Item -ItemType Directory -Path $swGame,(Join-Path $swHome $Game) -Force | Out-Null
$swSettings=@{}
if($UseSavedSettings) {
    $swSettingsRoot=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Quake3'
    foreach($swRelative in @("$Game/q3config.cfg",'vq3e-rendering.cfg')) {
        $swSettingsPath=Join-Path $swSettingsRoot $swRelative
        $swSettings[$swSettingsPath]=(Get-FileHash -LiteralPath $swSettingsPath).Hash
        Copy-Item -LiteralPath $swSettingsPath -Destination (Join-Path $swHome $swRelative)
    }
}
$swGuard=Join-Path $swAudit 'bounded-process.exe'
& $CC -O2 -municode (Join-Path $swRepo 'tools/pt-bounded-process.c') -o $swGuard
if ($LASTEXITCODE -ne 0) { throw 'Could not build the bounded-process helper.' }
$swCandidate=Split-Path (Join-Path $swRepo $Renderer) -Parent
foreach ($swName in @('vQ3Evolution.exe','SDL264.dll')) {
    $swSource=Join-Path $swCandidate $swName
    if (!(Test-Path -LiteralPath $swSource)) { $swSource=Join-Path $swAssets $swName }
    Copy-Item -LiteralPath $swSource -Destination $swGame
}
foreach($swModule in $(if($Game -eq 'baseq3') { @('uix86_64.dll','cgamex86_64.dll') } else { @() })) {
    if (Test-Path -LiteralPath (Join-Path $swCandidate "baseq3/$swModule")) {
        New-Item -ItemType Directory -Path (Join-Path $swGame 'baseq3') -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $swCandidate "baseq3/$swModule") -Destination (Join-Path $swGame 'baseq3')
        Copy-Item -LiteralPath (Join-Path $swCandidate "baseq3/$swModule") -Destination (Join-Path $swHome 'baseq3')
    }
}
foreach($swModule in $(if($Game -eq 'baseq3') { @('ui.qvm','cgame.qvm') } else { @() })) {
    if (Test-Path -LiteralPath (Join-Path $swCandidate "baseq3/vm/$swModule")) {
        New-Item -ItemType Directory -Path (Join-Path $swHome 'baseq3/vm') -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $swCandidate "baseq3/vm/$swModule") -Destination (Join-Path $swHome 'baseq3/vm')
    }
}
Copy-Item -LiteralPath (Join-Path $swRepo $Renderer) -Destination (Join-Path $swGame 'renderer_vulkan_x86_64.dll')
if($SavedNvidiaSettings -or $RayReconstruction -or $Dlaa) {
    Get-ChildItem -LiteralPath $swAssets -File -Filter '*.dll' |
        Where-Object { $_.Name -match '^(nvngx|sl\.|NvLowLatencyVk)' } |
        Copy-Item -Destination $swGame
}
if($Denoiser -eq 1 -and !$MissingDenoiser) {
    Copy-Item -LiteralPath (Join-Path $swRepo 'build-widescreen/nrd-adapter/Release/vq3e_nrd.dll') -Destination $swGame
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $Scenario) -Destination (Join-Path $swHome $Game)
if($Scenario -eq 'rt_config_write_failure.cfg') {
    New-Item -ItemType Directory -Path (Join-Path $swHome 'baseq3/blocked-dump.txt') | Out-Null
}
$swSaved = @{}
foreach ($swKey in @('VK_INSTANCE_LAYERS','VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH','VQ3E_BOUNDED_LOG')) {
    $swSaved[$swKey] = [Environment]::GetEnvironmentVariable($swKey,'Process')
}
try {
    $env:VK_INSTANCE_LAYERS = if ($Validation) { 'VK_LAYER_KHRONOS_validation' } else { '' }
    $env:VK_LAYER_PATH = 'C:/VulkanSDK/1.4.350.0/Bin'
    $env:VK_LAYER_SETTINGS_PATH = Join-Path $PSScriptRoot 'vulkan-software-validation'
    $env:VQ3E_BOUNDED_LOG = Join-Path $swAudit 'guard.log'
    $swArgs = '+set fs_basepath "'+$swAssets+'" +set fs_homepath "'+$swHome+'"'+
        $(if($Game -ne 'baseq3') { " +set fs_game $Game +set vm_cgame 2 +set vm_game 2 +set vm_ui 2" } else { '' })+
        $(if($Scenario -eq 'rt_urban_nv.cfg') { ' +set gear FLAOSAA' } else { '' })+
        " +set cl_renderer vulkan +set r_rayTracing $Mode"+
        $(if($SavedNvidiaSettings) { '' } else { ' +set r_dlss 0 +set r_dlssFrameGeneration 0 +set r_dlssNeuralRendering 0' })+
        " +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $Width +set r_customheight $Height"+
        ' +set r_pathTracingReference 0 +set r_pathTracingDenoise 1 +set r_pathTracingTemporal 1'+
        $(if($Mode -eq 1) { " +set r_softwareRayTracingDenoiser $Denoiser +set r_softwareRayTracingHistory $LocalHistory +set r_softwareRayTracingProfile $ShaderDiagnostic" } else { '' })+
        ' +set s_initsound 0'+
        " +set logfile 2 +set bot_enable 0 +set com_introplayed 1"+
        " +set activeAction `"exec $Scenario;`" +devmap $Map"
    if($UseSavedSettings) {
        # Urban Terror rejects the uncapped value during its cvar-limit check.
        # Keep the mod's saved cap for this integration test, not a benchmark.
        $swArgs=$(if($Game -eq 'baseq3') { ' +set com_maxfps 0' } else { ' +set developer 1' })+
            ' +set com_maxfpsUnfocused 0 +set r_pathTracingSamples 2 '+$swArgs
    } else {
        $swArgs=' +set r_postBloom 0 +set r_pathTracingSamples 2 +set r_pathTracingBounces 2'+
            ' +set r_pathTracingScale 1 +set com_maxfps 60 +set com_maxfpsUnfocused 60 '+$swArgs
    }
    if ($ReferenceKernel) {
        $swArgs = '+set r_pathTracingCompactTransport 0 +set r_pathTracingMaterialCache 0'+
            ' +set r_pathTracingBRDFReuse 0 +set r_pathTracingLightLoop 0 +set r_pathTracingEmitterGeometry 0 '+$swArgs
    }
    if($NativeGame -and $Game -eq 'baseq3') { $swArgs=' +set vm_cgame 0 '+$swArgs }
    if($RayReconstruction) {
        $swArgs=$swArgs.Replace(' +set r_dlss 0 ', ' +set r_dlss 5 +set r_dlssRayReconstruction 1 ')
    } elseif($Dlaa) {
        $swArgs=$swArgs.Replace(' +set r_dlss 0 ', ' +set r_dlss 5 +set r_dlssRayReconstruction 0 ')
    }
    # The engine accepts 32 startup segments, including the initial empty one.
    # Never silently drop the final activeAction/devmap after adding options.
    if([regex]::Matches($swArgs, '\+').Count -ge 32) { throw 'Test exceeds the engine startup-command limit; reduce launch overrides.' }
    & $swGuard `
        (Join-Path $swGame 'vQ3Evolution.exe') $swGame $TimeLimitSeconds $swArgs *> (Join-Path $swAudit 'process.log')
    $swExit = $LASTEXITCODE
    Write-Output "Test results: $swAudit (exit $swExit)"
    $swLog=Get-Content -LiteralPath (Join-Path $swHome "$Game/qconsole.log") -Raw
    if ($swExit -ne 0) {
        if($AllowShutdownTimeout -and $Scenario -eq 'rt_urban_nv.cfg' -and $swExit -eq 124 -and
            $swLog -match 'SOFTWARE_LIGHTING_URBAN_NV_COMPLETE' -and
            $swLog -match 'PT_SDK_SHUTDOWN_BEGIN') {
            Write-Warning 'NV scenario completed, but NVIDIA shutdown required the guard. Not a clean process-lifecycle pass.'
        } else { throw 'The bounded game test did not complete successfully.' }
    }
    if($swExit -eq 0 -and $swLog -match 'PT_SDK_SHUTDOWN_BEGIN' -and
        $swLog -notmatch 'PT_SDK_SHUTDOWN_END') {
        throw 'NVIDIA shutdown did not report completion.'
    }
    if ($swLog -notmatch 'SOFTWARE_LIGHTING_[A-Z_]*COMPLETE') { throw 'The test scenario did not finish.' }
    if($RayReconstruction -and ($swLog -notmatch 'Ray Reconstruction: supported 1, enabled 1' -or
        $swLog -match 'Ray Reconstruction evaluation failed')) { throw 'Ray Reconstruction was not successfully exercised.' }
    if($Scenario -eq 'rt_urban_nv.cfg') {
        if($Game -ne 'Q3UT3' -or $swLog -notmatch 'Q3UT3\\zQ3UT37.pk3' -or
            $swLog -notmatch 'night vision active' -or
            $swLog -notmatch '"r_nvOverride" is:"0\^7"') { throw 'Actual mod / automatic NV activation not confirmed.' }
        if([regex]::Matches($swLog, 'clientCommand: [^\r\n]+ : ut_itemuse 18\r?\n').Count -ne 3) {
            throw 'Expected three actual goggles-use commands (item 18), not the vest or spectator commands.'
        }
        foreach($swImage in @('urban_nv_off','urban_nv_on','urban_nv_colour8','urban_nv_off_again')) {
            if(!(Test-Path -LiteralPath (Join-Path $swHome "Q3UT3/screenshots/$swImage.jpg"))) { throw "Missing $swImage capture." }
        }
        Write-Output 'PASS: actual Urban Terror goggles toggled three times with override off; four captures ready for visual inspection.'
    }
    if($Scenario -like 'rt_q3dm0_portal*' -and $Mode -ne 0) {
        if($swLog -notmatch 'Path tracing: 1 BSP camera portal surfaces loaded' -or
            $swLog -notmatch 'PT_PORTAL 0 clip 0.000 1.000 0.000 -376.000') {
            throw 'Q3DM0 did not load and bind its actual remote camera.'
        }
        $swImages = if($Scenario -eq 'rt_q3dm0_portal.cfg') { @('portal_close','portal_front','portal_angle','portal_far','portal_mirror') }
            elseif($Scenario -eq 'rt_q3dm0_portal_brightness.cfg') { @('portal_brightness_near','portal_brightness_front','portal_brightness_angle','portal_brightness_far') }
            elseif($Scenario -eq 'rt_q3dm0_portal_rr.cfg') { @('portal_rr_front','portal_rr_mirror') }
            else { @('portal_no_bloom','portal_camera_off','portal_emission') }
        foreach($swImage in $swImages) {
            if(!(Test-Path -LiteralPath (Join-Path $swHome "baseq3/screenshots/$swImage.jpg"))) { throw "Missing $swImage capture." }
        }
        Write-Output 'PASS: Q3DM0 native camera portal bound; requested view captures completed (inspect images for appearance).'
    }
    if($Scenario -in @('rt_config_write.cfg','rt_config_write_soak.cfg')) {
        $swExpectedBrightness=if($Scenario -eq 'rt_config_write_soak.cfg') { '0.375' } else { '0.25' }
        foreach($swConfig in @('q3config.cfg','flash-write-check.cfg')) {
            $swContent=Get-Content -LiteralPath (Join-Path $swHome "baseq3/$swConfig") -Raw
            if($swContent -notmatch ('seta r_muzzleFlashBrightness "'+[regex]::Escape($swExpectedBrightness)+'"')) { throw "Brightness not saved in $swConfig" }
        }
        if($Scenario -eq 'rt_config_write_soak.cfg') {
            $swFrame=[regex]::Match($swLog,'Path tracer: active 1, frame ([0-9]+),')
            if(!$swFrame.Success -or [int]$swFrame.Groups[1].Value -lt 600) { throw 'Did not exceed the old file-handle exhaustion point.' }
        }
        if(!(Test-Path -LiteralPath (Join-Path $swHome 'baseq3/flash-error.txt'))) { throw 'Console dump was not created.' }
        if($swLog -match "Couldn't write|ERROR: couldn't open|could not write|could not replace") { throw 'Config write diagnostic reported an error.' }
        Write-Output 'PASS: brightness saved to both game configs, and console dump created.'
    }
    if($Scenario -eq 'rt_config_write_failure.cfg') {
        $swDiagnostic=Get-Content -LiteralPath (Join-Path $swHome 'filesystem-write-errors.log') -Raw
        if($swDiagnostic -notmatch 'baseq3[/\\]blocked-dump.txt \(errno [1-9][0-9]*:.*OS error [1-9][0-9]*\)') {
            throw 'Original write failure path and OS reason were not captured.'
        }
        if(!(Test-Path -LiteralPath (Join-Path $swHome 'baseq3/working-dump.txt'))) { throw 'Normal console saving failed after the diagnostic.' }
        Write-Output 'PASS: real Windows write error logged outside the failing target; subsequent console dump succeeded.'
    }
    if ($Mode -eq 1 -and $swLog -notmatch 'Software BVH full lighting active') { throw 'Software lighting did not activate.' }
    if ($Mode -eq 1 -and $Denoiser -eq 1) {
        if($MissingDenoiser) {
            if($swLog -notmatch 'Software NRD unavailable:.*using native denoising' -or $swLog -match 'Software NRD RELAX active') {
                throw 'Missing-runtime native fallback was not confirmed.'
            }
        } elseif($swLog -notmatch 'Software NRD RELAX active') { throw 'NRD did not activate; native fallback is not an NRD test pass.' }
    }
    if($Scenario -eq 'rt_software_denoise.cfg') {
        foreach($swCapture in @('software_denoise_still','software_denoise_move_1','software_denoise_move_8')) {
            if(!(Test-Path -LiteralPath (Join-Path $swHome "baseq3/screenshots/$swCapture.jpg"))) { throw "Missing comparison screenshot: $swCapture" }
        }
    }
    if($Scenario -eq 'rt_nv_look.cfg') {
        if($swLog -notmatch 'Vulkan post-processing night vision active') { throw 'NV post-processing did not activate.' }
        foreach($swCapture in @('nv_off','nv_white','nv_green','nv_fullscreen','nv_debug')) {
            if(!(Test-Path -LiteralPath (Join-Path $swHome "baseq3/screenshots/$swCapture.jpg"))) { throw "Missing NV screenshot: $swCapture" }
        }
        Write-Output 'PASS: NV post-processing activated; off/white/green/fullscreen/debug views captured. Inspect appearance separately.'
    }
    if($Scenario -eq 'rt_software_profile.cfg') {
        & (Join-Path $PSScriptRoot 'summarize-software-profile.ps1') -Log (Join-Path $swHome 'baseq3/qconsole.log') | Format-Table -AutoSize
    }
    if($Scenario -eq 'rt_muzzle_flash.cfg') {
        foreach($swCapture in @('muzzle_default','muzzle_hidden_lit','muzzle_visible_unlit','muzzle_both_off','muzzle_boost')) {
            if(!(Test-Path -LiteralPath (Join-Path $swHome "baseq3/screenshots/$swCapture.jpg"))) { throw "Missing muzzle screenshot: $swCapture" }
        }
        foreach($swState in @('1.000, light 1.000','0.000, light 1.000','1.000, light 0.000','0.000, light 0.000','2.000, light 2.000')) {
            $swPattern='Muzzle flash controls: brightness '+[regex]::Escape($swState)+', tagged triangles [1-9][0-9]*'
            if($swLog -notmatch $swPattern) { throw "Muzzle state / tagged geometry not confirmed: $swState" }
        }
        Write-Output 'PASS: five independent live control states with tagged muzzle geometry and screenshots; inspect images separately.'
    }
    if($Scenario -eq 'rt_rocket_brightness.cfg') {
        foreach($swCapture in @('rocket_default','rocket_hidden_lit','rocket_visible_unlit','rocket_both_off','rocket_dim')) {
            if(!(Test-Path -LiteralPath (Join-Path $swHome "baseq3/screenshots/$swCapture.jpg"))) { throw "Missing rocket screenshot: $swCapture" }
        }
        foreach($swState in @('1.000, light 1.000','0.000, light 1.000','1.000, light 0.000','0.000, light 0.000','0.250, light 0.250')) {
            $swPattern='Rocket controls: brightness '+[regex]::Escape($swState)+', tagged triangles [1-9][0-9]*'
            if($swLog -notmatch $swPattern) { throw "Rocket state / tagged geometry not confirmed: $swState" }
        }
        $swRocketConfig=Get-Content -LiteralPath (Join-Path $swHome 'baseq3/rocket-controls-check.cfg') -Raw
        foreach($swKey in @('r_rocketBrightness','r_rocketLightScale')) {
            if($swRocketConfig -notmatch ('seta '+$swKey+' "(?:0)?\.25"')) { throw "Rocket setting not saved: $swKey" }
        }
        Write-Output 'PASS: five independent live rocket controls with tagged projectile geometry and saved settings; inspect captures separately.'
    }
    if($Scenario -eq 'rt_weapon_effect_lights.cfg') {
        foreach($swEffect in @('explosion','lightning')) {
            foreach($swState in @('on','off','dim')) {
                $swCapture="${swEffect}_light_${swState}"
                if(!(Test-Path -LiteralPath (Join-Path $swHome "baseq3/screenshots/$swCapture.jpg"))) { throw "Missing effect screenshot: $swCapture" }
            }
        }
        foreach($swValue in @('1.000','0.000','0.250')) {
            $swPattern='Weapon effect lights: rocket explosion '+[regex]::Escape($swValue)+' \([1-9][0-9]* triangles\)'
            if($swLog -notmatch $swPattern) { throw "Rocket explosion tagging/control not confirmed: $swValue" }
            $swParticleCheck=[regex]::Match($swLog, 'Weapon effect lights: rocket explosion '+[regex]::Escape($swValue)+' \(([0-9]+) triangles\)')
            if([int]$swParticleCheck.Groups[1].Value -lt 4) { throw 'Expected both the impact sprite and animated explosion polygon.' }
            $swPattern='lightning gun '+[regex]::Escape($swValue)+' \([1-9][0-9]* triangles\)'
            if($swLog -notmatch $swPattern) { throw "Lightning tagging/control not confirmed: $swValue" }
        }
        $swContent=Get-Content -LiteralPath (Join-Path $swHome 'baseq3/weapon-lights-check.cfg') -Raw
        foreach($swKey in @('r_rocketExplosionLightScale','r_lightningGunLightScale')) {
            if($swContent -notmatch ('seta '+$swKey+' "(?:0)?\.25"')) { throw "Weapon light setting not saved: $swKey" }
        }
        Write-Output 'PASS: live on/off/dim effect-light states with tagged rocket explosions and lightning; saved controls. Inspect captures separately.'
    }
    if($Scenario -eq 'rt_software_diagnostic.cfg') {
        if(!$ShaderDiagnostic) { throw 'The diagnostic scenario requires -ShaderDiagnostic 1 or 2.' }
        & (Join-Path $PSScriptRoot 'summarize-software-diagnostic.ps1') -Log (Join-Path $swHome 'baseq3/qconsole.log') -ExpectedMode $ShaderDiagnostic | ConvertTo-Json -Depth 5
    }
    $swValidation=Join-Path $swGame 'vulkan-validation.log'
    if($Validation -and !(Test-Path -LiteralPath $swValidation)) { throw 'Validation log was not created.' }
    if ((Test-Path -LiteralPath $swValidation) -and
        (Select-String -LiteralPath $swValidation -Pattern 'VUID-|SYNC-HAZARD|Validation Error')) {
        throw 'Vulkan validation reported an error.'
    }
} finally {
    foreach ($swKey in $swSaved.Keys) { [Environment]::SetEnvironmentVariable($swKey,$swSaved[$swKey],'Process') }
    foreach($swSettingsPath in $swSettings.Keys) {
        if((Get-FileHash -LiteralPath $swSettingsPath).Hash -ne $swSettings[$swSettingsPath]) { throw "User settings changed: $swSettingsPath" }
    }
}
