# Compile a same-math integrator variant with selected SPIR-V function boundaries.
# Only OpFunction's Inline/DontInline performance hints are modified, before -O.
# No source calculations, specialization values, or storage layouts are changed.
param(
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$VulkanSDK = $env:VULKAN_SDK,
    [string]$Source = 'code/renderer_vulkan/shaders/pt_brdf.comp',
    [string[]]$Functions = @('layerSampleUV','layerSample','materialColor','emissionAt','applyDecals','trace','visibility'),
    [switch]$HintsOnly
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked([string]$Name, [string[]]$Arguments) {
    & (Join-Path $VulkanSDK "Bin/$Name.exe") @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Name failed: $LASTEXITCODE" }
}
Invoke-Checked 'glslangValidator' @('--target-env','vulkan1.2','-V',$Source,'-o',$Output)
$ptBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Output))
$ptWords = New-Object uint32[] ($ptBytes.Length / 4)
[Buffer]::BlockCopy($ptBytes,0,$ptWords,0,$ptBytes.Length)
if ($ptWords[0] -ne 0x07230203) { throw 'Not SPIR-V' }
$ptNames = @{}
for ($ptAt=5; $ptAt -lt $ptWords.Length;) {
    $ptCount = [int]($ptWords[$ptAt] -shr 16)
    $ptOpcode = $ptWords[$ptAt] -band 0xffff
    if ($ptCount -eq 0 -or $ptAt+$ptCount -gt $ptWords.Length) { throw 'Invalid SPIR-V instruction' }
    if ($ptOpcode -eq 5) { # OpName: result-id, null-terminated UTF-8 name
        $ptName = [Text.Encoding]::UTF8.GetString($ptBytes,($ptAt+2)*4,($ptCount-2)*4).Split([char]0)[0]
        if ($Functions -contains $ptName.Split('(')[0]) { $ptNames[$ptWords[$ptAt+1]] = $ptName }
    }
    $ptAt += $ptCount
}
$ptMarked=0
for ($ptAt=5; $ptAt -lt $ptWords.Length; $ptAt += [int]($ptWords[$ptAt] -shr 16)) {
    if (($ptWords[$ptAt] -band 0xffff) -eq 54 -and $ptNames.ContainsKey($ptWords[$ptAt+2])) { # OpFunction
        $ptWords[$ptAt+3] = ($ptWords[$ptAt+3] -band 0xfffffffe) -bor 2
        Write-Output "DontInline $($ptNames[$ptWords[$ptAt+2]])"
        ++$ptMarked
    }
}
if ($ptMarked -eq 0) { throw 'No selected functions found; refusing a no-op variant' }
foreach ($ptFunction in $Functions) {
    if (!($ptNames.Values | Where-Object { $_.Split('(')[0] -eq $ptFunction })) { throw "Function not found: $ptFunction" }
}
[Buffer]::BlockCopy($ptWords,0,$ptBytes,0,$ptBytes.Length)
[IO.File]::WriteAllBytes((Resolve-Path -LiteralPath $Output),$ptBytes)
Invoke-Checked 'spirv-val' @('--target-env','vulkan1.2',$Output)
if (!$HintsOnly) {
    Invoke-Checked 'spirv-opt' @('--target-env=vulkan1.2','-O',$Output,'-o',$Output)
    Invoke-Checked 'spirv-val' @('--target-env','vulkan1.2',$Output)
}
Write-Output "Compiled $Output ($ptMarked function hints)"
