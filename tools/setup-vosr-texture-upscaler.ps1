param(
    [string]$Destination = "build-texture-upscaler",
    [string]$Python = "py"
)

$ErrorActionPreference = "Stop"
$vosrRepository = "https://github.com/cswry/VOSR.git"
$vosrRevision = "516f292b99cf23c76fdc33351e86dc4f97711fe8"
$destinationPath = [IO.Path]::GetFullPath($Destination)
$vosrPath = Join-Path $destinationPath "VOSR"
$environmentPath = Join-Path $destinationPath "vosr-env"
$environmentPython = Join-Path $environmentPath "Scripts\python.exe"

New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $vosrPath ".git"))) {
    git clone $vosrRepository $vosrPath
    git -C $vosrPath checkout --detach $vosrRevision
} else {
    $installedRevision = (git -C $vosrPath rev-parse HEAD).Trim()
    if ($installedRevision -ne $vosrRevision) {
        throw "Existing VOSR checkout is $installedRevision; expected $vosrRevision. Remove or move it before setup."
    }
}

if (-not (Test-Path -LiteralPath $environmentPython)) {
    if ($Python -eq "py") {
        & $Python -3.12 -m venv $environmentPath
    } else {
        & $Python -m venv $environmentPath
    }
}

& $environmentPython -m pip install --upgrade pip
& $environmentPython -m pip install `
    torch==2.13.0+cu130 torchvision==0.28.0+cu130 `
    --index-url https://download.pytorch.org/whl/cu130
& $environmentPython -m pip install `
    accelerate==1.1.0 diffusers==0.35.0 einops==0.8.0 fairscale==0.4.13 `
    loguru==0.7.3 numpy==1.26.4 opencv-python-headless==4.10.0.84 `
    pillow==12.1.1 PyYAML==6.0.2 safetensors==0.4.4 timm==1.0.11 `
    transformers==4.52.0 huggingface_hub==0.36.2 hf_xet==1.6.0 `
    "triton-windows>=3.7,<3.8"

$checkpointPath = Join-Path $vosrPath "preset\ckpts"
$download = @"
from huggingface_hub import snapshot_download
snapshot_download(
    repo_id="CSWRY/VOSR",
    local_dir=r"$checkpointPath",
    allow_patterns=[
        "VOSR2/**",
        "Qwen-Image-vae-2d/**",
        "torch_cache/checkpoints/dinov2_vitl14_pretrain.pth",
    ],
)
"@
& $environmentPython -c $download

& $environmentPython -c "import torch; assert torch.cuda.is_available(); print(torch.__version__, torch.cuda.get_device_name(0))"
Write-Host "Installed official VOSR 2.0 at $vosrPath"
Write-Host "The source, model weights, and Python environment remain in the ignored build directory."
