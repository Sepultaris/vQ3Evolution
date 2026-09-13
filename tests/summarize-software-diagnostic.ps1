param([Parameter(Mandatory=$true)][string]$Log,[ValidateSet(0,1,2)][int]$ExpectedMode=0)
$ErrorActionPreference='Stop'
$diagText=Get-Content -LiteralPath $Log -Raw
$diagCamera=@([regex]::Matches($diagText,'(?m)^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)\r?$'))
if($diagCamera.Count -ne 2 -or $diagCamera[0].Value.Trim() -ne $diagCamera[1].Value.Trim() -or
    $diagCamera[0].Groups[2].Value -ne '448' -or $diagCamera[0].Groups[4].Value -ne '0') {
    throw 'Diagnostic camera did not reach and hold the test location.'
}
$diagRegion=[regex]::Match($diagText,'(?s)SW_DIAGNOSTIC_BEGIN(.*?)SW_DIAGNOSTIC_END')
if(!$diagRegion.Success) { throw 'Incomplete diagnostic interval.' }
$diagRows=@([regex]::Matches($diagRegion.Groups[1].Value,'SW_SHADER_PROFILE [^\r\n]+') | ForEach-Object {
    $diagRow=@{}
    foreach($diagMatch in [regex]::Matches($_.Value,'([a-z][a-z0-9_]*)=([0-9.]+)')) {
        $diagRow[$diagMatch.Groups[1].Value]=[double]::Parse($diagMatch.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture)
    }
    foreach($diagField in (@('frame','phase','mode','pixels','invalid')+(0..15 | ForEach-Object {"t$_"})+(0..31 | ForEach-Object {"c$_"}))) {
        if(!$diagRow.ContainsKey($diagField)) { throw "Missing diagnostic field $diagField" }
    }
    if($diagRow.invalid -ne 0 -or $diagRow.pixels -le 0 -or $diagRow.c0 -ne 2*$diagRow.pixels) {
        throw 'Invalid diagnostic records or sampling settings.'
    }
    if($diagRow.c28+$diagRow.c29+$diagRow.c30 -le 0) { throw 'No software queries were measured.' }
    if($diagRow.c31 -gt 6*$diagRow.c0) { throw 'Unexpected bounce count in saved-settings diagnostic.' }
    if($ExpectedMode -and $diagRow.mode -ne $ExpectedMode) { throw 'Requested diagnostic mode did not activate.' }
    [pscustomobject]$diagRow
})
if($diagRows.Count -lt 24) { throw "Insufficient diagnostic frames: $($diagRows.Count)" }
$diagModes=@($diagRows.mode | Select-Object -Unique)
if($diagModes.Count -ne 1) { throw 'Mixed diagnostic modes.' }
$diagPixels=($diagRows | Measure-Object pixels -Sum).Sum
$diagTicks=@(0..15 | ForEach-Object { ($diagRows | Measure-Object "t$_" -Sum).Sum })
$diagCounts=@(0..31 | ForEach-Object { ($diagRows | Measure-Object "c$_" -Sum).Sum })
$diagTotal=($diagTicks | Measure-Object -Sum).Sum
if(($diagModes[0] -eq 1 -and $diagTotal -le 0) -or ($diagModes[0] -eq 2 -and $diagTotal -ne 0)) {
    throw 'Clock data does not match diagnostic mode.'
}
$diagNames=@('Other/control','Trace callback/control','Material/texture','Emitter sampling',
    'Map-light proposals','BRDF evaluation','Path continuation','Point/sun setup',
    'Fog sampling','Fog light setup','Fog visibility control','Fog transmittance',
    'Visibility callback/control','Ordinary/decal BVH traversal','Visibility BVH traversal','Weapon BVH traversal')
$diagShares=@(for($diagIndex=0;$diagIndex -lt 16;++$diagIndex) {
    [pscustomobject]@{Section=$diagNames[$diagIndex];ClockShare=if($diagTotal) {[math]::Round(100*$diagTicks[$diagIndex]/$diagTotal,3)} else {$null}}
})
$diagWork=@(for($diagKind=0;$diagKind -lt 3;++$diagKind) {
    $diagRays=$diagCounts[28+$diagKind]
    [pscustomobject]@{Kind=@('Ordinary/decal','Visibility','Weapon')[$diagKind];
        RaysPerPixel=[math]::Round($diagRays/$diagPixels,3);
        NodesPerRay=if($diagRays) {[math]::Round($diagCounts[16+$diagKind]/$diagRays,3)} else {0};
        TriangleTestsPerRay=if($diagRays) {[math]::Round($diagCounts[19+$diagKind]/$diagRays,3)} else {0};
        MaskRejectionsPerRay=if($diagRays) {[math]::Round($diagCounts[22+$diagKind]/$diagRays,3)} else {0}}
})
[pscustomobject]@{Mode=[int]$diagModes[0];Camera=$diagCamera[0].Value.Trim();Frames=$diagRows.Count;
    Phases=@($diagRows.phase | Select-Object -Unique).Count;Period=64;SampledPixels=$diagPixels;
    SamplesPerPixel=$diagCounts[0]/$diagPixels;BounceIterationsPerPath=[math]::Round($diagCounts[31]/$diagCounts[0],3);
    ClockShares=$diagShares;Work=$diagWork;
    Note='Diagnostic replay clock shares, not GPU milliseconds or production performance. Partial phase coverage is not a full-frame census.'}
