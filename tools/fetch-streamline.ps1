param(
    [string]$Destination = "build-widescreen/deps/streamline-sdk-v2.12.0"
)

$ErrorActionPreference = "Stop"
$version = "2.12.0"
$expectedSha256 = "f5c0a3d870707dddc3570fb4bcd3655cf48a8a68c3a9d342910cfa21b77dcf48"
$url = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v$version/streamline-sdk-v$version.zip"
$destinationPath = [IO.Path]::GetFullPath($Destination)
$archivePath = "$destinationPath.zip"

if (Test-Path -LiteralPath "$destinationPath/include/sl.h") {
    Write-Host "Streamline SDK $version is already available at $destinationPath"
    exit 0
}

$parent = Split-Path -Parent $destinationPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
Invoke-WebRequest -Uri $url -OutFile $archivePath

$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "Streamline archive checksum mismatch. Expected $expectedSha256, got $actualSha256"
}

Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath -Force
Write-Host "Verified and extracted NVIDIA Streamline SDK $version to $destinationPath"
