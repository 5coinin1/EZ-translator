param(
    [string]$BaseAppDir = "",
    [string]$ProfileDir = "",
    [string]$OrtWheel = "",
    [string]$OrtVersion = "1.20.2",
    [string]$ModelsDir = ""
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$text) {
    Write-Host ""
    Write-Host "== $text ==" -ForegroundColor Cyan
}

if (-not $BaseAppDir) {
    $BaseAppDir = (Get-Location).Path
}
$BaseAppDir = (Resolve-Path -LiteralPath $BaseAppDir).Path
if (-not $ProfileDir) {
    $ProfileDir = Join-Path $env:LOCALAPPDATA "EZTeam\EZ-Translator\cuda"
}
New-Item -ItemType Directory -Force -Path $ProfileDir | Out-Null

Write-Step "Kiểm tra GPU NVIDIA"
& nvidia-smi --query-gpu=name,memory.total --format=csv,noheader
if ($LASTEXITCODE -ne 0) {
    throw "Không tìm thấy NVIDIA GPU/driver. Không cài CUDA OCR."
}

Write-Step "Tìm Python + CUDA runtime"
$python = $null
foreach ($candidate in @("C:\Python312\python.exe", "python.exe", "py.exe")) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) {
        $python = $cmd.Source
        break
    }
}
if (-not $python) {
    throw "Không tìm thấy Python. Cần Python có ctranslate2 để cài gói CUDA."
}

$torchLib = ""
try {
    $torchLib = (& $python -c "import os,torch; print(os.path.join(os.path.dirname(torch.__file__),'lib'))" 2>$null).Trim()
} catch {
    $torchLib = ""
}
if (-not (Test-Path -LiteralPath $torchLib)) {
    if ($env:CUDA_PATH) {
        $candidate = Join-Path $env:CUDA_PATH "bin"
        if (Test-Path -LiteralPath $candidate) {
            $torchLib = $candidate
        }
    }
}
if (-not (Test-Path -LiteralPath $torchLib)) {
    throw "Không thấy cuBLAS/cuDNN (torch/lib hoặc CUDA_PATH). Hãy cài PyTorch CUDA hoặc CUDA Toolkit trước."
}
Write-Host "CUDA DLLs: $torchLib"

Write-Step "Lấy ONNX Runtime GPU wheel $OrtVersion"
$temp = Join-Path $env:TEMP "ez_cuda_ocr_pack"
if (Test-Path -LiteralPath $temp) {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $temp | Out-Null

if ($OrtWheel -and (Test-Path -LiteralPath $OrtWheel)) {
    $wheel = (Resolve-Path -LiteralPath $OrtWheel).Path
} else {
    & $python -m pip download "onnxruntime-gpu==$OrtVersion" --no-deps -d $temp
    if ($LASTEXITCODE -ne 0) {
        throw "Tải onnxruntime-gpu thất bại."
    }
    $wheel = Get-ChildItem -LiteralPath $temp -Filter "onnxruntime_gpu-*.whl" |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $wheel) {
        throw "Không thấy wheel onnxruntime-gpu sau khi tải."
    }
    $wheel = $wheel.FullName
}

$extract = Join-Path $temp "extract"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($wheel, $extract)
$capi = Join-Path $extract "onnxruntime\capi"
foreach ($name in @("onnxruntime.dll", "onnxruntime_providers_shared.dll",
                    "onnxruntime_providers_cuda.dll")) {
    $source = Join-Path $capi $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Thiếu $name trong wheel."
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $ProfileDir $name) -Force
}

Write-Step "Tạo profile app CUDA"
foreach ($name in @("EZTranslator.exe", "wgc_capture_helper.exe", "ct2_server.py")) {
    $source = Join-Path $BaseAppDir $name
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $ProfileDir $name) -Force
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $ProfileDir "EZTranslator.exe"))) {
    throw "Thiếu EZTranslator.exe trong thư mục app."
}

$envFile = Join-Path $ProfileDir "cuda_env.txt"
$lines = @($torchLib)
if ($ModelsDir -and (Test-Path -LiteralPath $ModelsDir)) {
    $lines += "MODELS=$((Resolve-Path -LiteralPath $ModelsDir).Path)"
}
Set-Content -LiteralPath $envFile -Value $lines -Encoding UTF8

@(
    "version=$OrtVersion"
    "installed=$(Get-Date -Format o)"
    "base=$BaseAppDir"
) | Set-Content -LiteralPath (Join-Path $ProfileDir "cuda_ocr_ready.txt") -Encoding UTF8

Write-Step "Hoàn tất"
Write-Host "Đã cài CUDA OCR profile tại: $ProfileDir" -ForegroundColor Green
Write-Host "Khởi động lại EZTranslator để dùng CUDA OCR." -ForegroundColor Yellow
Write-Host "Nếu app không lên, xóa thư mục trên để quay lại DirectML/CPU."
