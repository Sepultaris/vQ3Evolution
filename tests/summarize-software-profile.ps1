param([Parameter(Mandatory=$true)][string]$Log)
$ErrorActionPreference='Stop'
$swText=Get-Content -LiteralPath $Log -Raw
$swCameras=@([regex]::Matches($swText,'(?m)^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)\r?$'))
if($swCameras.Count -ne 2 -or $swCameras[0].Value.Trim() -ne $swCameras[1].Value.Trim() -or
    $swCameras[0].Groups[2].Value -ne '448' -or $swCameras[0].Groups[4].Value -ne '0') {
    throw 'Camera did not reach and hold the requested test location.'
}
"Verified fixed camera: $($swCameras[0].Value.Trim())"
$swRegion=[regex]::Match($swText,'(?s)SW_MEASURE_BEGIN(.*?)SW_MEASURE_END')
if(!$swRegion.Success) { throw 'The complete software measurement interval is missing.' }
if($swRegion.Groups[1].Value -match 'SW_PROFILE_DIAGNOSTIC|SW_SHADER_PROFILE ') {
    throw 'Diagnostic shader runs must not be counted as normal performance measurements.'
}
$swRows=@([regex]::Matches($swRegion.Groups[1].Value,'SW_PROFILE[^\r\n]+') | ForEach-Object {
    $swRow=@{}
    foreach($swMatch in [regex]::Matches($_.Value,'([a-z_]+)=([0-9.]+)')) { $swRow[$swMatch.Groups[1].Value]=[double]::Parse($swMatch.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture) }
    [pscustomobject]$swRow
})
if($swRows.Count -lt 24) { throw "Insufficient completed GPU frames: $($swRows.Count)" }
"Completed measurement frames: $($swRows.Count)"
foreach($swMetric in @('wall','scene_cpu','upload_bytes','raster','upload','guides','trace','exposure','native','nrd','spatial','post','upscale','ui','gpu')) {
    if(@($swRows | Where-Object { $null -eq $_.$swMetric }).Count) { throw "Missing timing: $swMetric" }
    $swValues=@($swRows | ForEach-Object { $_.$swMetric } | Sort-Object)
    if($swValues.Count -ne $swRows.Count) { throw "Missing timing: $swMetric" }
    $swMiddle=[int][math]::Floor($swValues.Count/2)
    $swMedian=if($swValues.Count%2) {$swValues[$swMiddle]} else {($swValues[$swMiddle-1]+$swValues[$swMiddle])/2}
    [pscustomobject]@{Metric=$swMetric;Median=[math]::Round($swMedian,3);P90=$swValues[[int][math]::Floor(($swValues.Count-1)*.9)]}
}
