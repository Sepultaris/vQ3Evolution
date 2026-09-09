param(
    [string]$Destination = "build-texture-upscaler"
)

$ErrorActionPreference = "Stop"
$release = "v0.2.5.0"
$package = "realesrgan-ncnn-vulkan-20220424-windows.zip"
$expectedSha256 = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d"
$url = "https://github.com/xinntao/Real-ESRGAN/releases/download/$release/$package"
$destinationPath = [IO.Path]::GetFullPath($Destination)
$archivePath = Join-Path ([IO.Path]::GetTempPath()) $package

if (Test-Path -LiteralPath (Join-Path $destinationPath "realesrgan-ncnn-vulkan.exe")) {
    Write-Host "Real-ESRGAN is already available at $destinationPath"
    exit 0
}

New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
Invoke-WebRequest -Uri $url -OutFile $archivePath
$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    Remove-Item -LiteralPath $archivePath -Force
    throw "Real-ESRGAN archive checksum mismatch: expected $expectedSha256, got $actualSha256"
}
Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath -Force
Remove-Item -LiteralPath $archivePath -Force

$executable = Get-ChildItem -LiteralPath $destinationPath -Filter "realesrgan-ncnn-vulkan.exe" -Recurse | Select-Object -First 1
if ($null -eq $executable) {
    throw "The official archive did not contain realesrgan-ncnn-vulkan.exe"
}
if ($executable.DirectoryName -ne $destinationPath) {
    Get-ChildItem -LiteralPath $executable.DirectoryName -Force | Move-Item -Destination $destinationPath -Force
}

Write-Host "Verified and installed the official portable Real-ESRGAN release at $destinationPath"
Write-Host "The model and executable are local build dependencies and are not added to Git."
