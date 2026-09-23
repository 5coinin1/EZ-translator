# Tải các dependency nhị phân (ONNX Runtime + model OCR) cho EZTranslator.
#
#   pwsh -File scripts/fetch_deps.ps1            # tải phần còn thiếu
#   pwsh -File scripts/fetch_deps.ps1 -Force     # tải lại tất cả
#
# Mọi file đều được kiểm tra SHA256. Script idempotent: file đã có và đúng hash
# thì bỏ qua. Nguồn:
#   - ONNX Runtime DirectML : NuGet Microsoft.ML.OnnxRuntime.DirectML
#   - DirectML runtime      : NuGet Microsoft.AI.DirectML
#   - Model PP-OCRv4        : RapidOCR (SWHL/RapidOCR trên HuggingFace,
#                             RapidAI/RapidOCR trên ModelScope)
#   - Từ điển               : PaddlePaddle/PaddleOCR
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root      = Split-Path -Parent $PSScriptRoot
$OrtDir    = Join-Path $Root "third_party\onnxruntime"
$ModelsDir = Join-Path $Root "models"
$TmpDir    = Join-Path ([IO.Path]::GetTempPath()) "ez_translator_deps"

# Phiên bản phải khớp nhau: ORT DirectML 1.20.1 phụ thuộc Microsoft.AI.DirectML 1.15.2.
$OrtVersion = "1.20.1"
$DmlVersion = "1.15.2"

# {name, url, sha256} — nupkg được kiểm tra hash rồi giải nén.
$Nupkgs = @(
    @{ name = "ort"; url = "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/$OrtVersion"; sha256 = "6763468507B7CFC777B1334B3E174C11A540DDACB7BD4354BC2E0EC89E56EEC2" },
    @{ name = "dml"; url = "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/$DmlVersion";           sha256 = "9F07482559087088A4DBA4AE76EEEEE1FAD3F7077A92CCFBDB439C6BC2964C09" }
)

# {name, url, sha256} — tải thẳng vào models/.
$Models = @(
    @{ name = "ch_PP-OCRv4_det_infer.onnx";  url = "https://huggingface.co/SWHL/RapidOCR/resolve/main/PP-OCRv4/ch_PP-OCRv4_det_infer.onnx";                      sha256 = "D2A7720D45A54257208B1E13E36A8479894CB74155A5EFE29462512D42F49DA9" },
    @{ name = "ch_PP-OCRv4_rec_infer.onnx";  url = "https://huggingface.co/SWHL/RapidOCR/resolve/main/PP-OCRv4/ch_PP-OCRv4_rec_infer.onnx";                      sha256 = "48FC40F24F6D2A207A2B1091D3437EB3CC3EB6B676DC3EF9C37384005483683B" },
    @{ name = "en_PP-OCRv4_rec_mobile.onnx"; url = "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/master/onnx/PP-OCRv4/rec/en_PP-OCRv4_rec_mobile.onnx"; sha256 = "E8770C967605983D1570CDF5352041DFB68FA0C21664F49F47B155ABD3E0E318" },
    @{ name = "ppocr_keys_v1.txt";           url = "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/ppocr_keys_v1.txt";               sha256 = "A1C84D9BDB9AB29043C58896224D32941783EB821629618416DCB08F12886492" },
    @{ name = "en_dict.txt";                 url = "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/en_dict.txt";                     sha256 = "5662DF9D2D03F0E8CA0D3B0649D6ACBAB904B6A14B3D3521463C71C37C668CE3" }
)

function Get-FileHash256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Test-File([string]$Path, [string]$Sha256) {
    return (Test-Path -LiteralPath $Path) -and ((Get-FileHash256 $Path) -eq $Sha256)
}

function Save-Verified([string]$Url, [string]$Dest, [string]$Sha256, [switch]$Force) {
    if ((Test-File $Dest $Sha256) -and -not $Force) {
        Write-Host "[deps] có sẵn : $Dest"
        return
    }
    $parent = Split-Path -Parent $Dest
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }

    Write-Host "[deps] tải    : $Url"
    $temp = "$Dest.download"
    Invoke-WebRequest -Uri $Url -OutFile $temp -MaximumRedirection 5 -UseBasicParsing

    $got = Get-FileHash256 $temp
    if ($got -ne $Sha256) {
        Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
        throw "SHA256 lệch cho $Dest`n  mong đợi: $Sha256`n  thực tế : $got"
    }
    Move-Item -LiteralPath $temp -Destination $Dest -Force
    Write-Host "[deps] OK     : $Dest"
}

function Get-Nupkg([hashtable]$Package) {
    $cache = Join-Path $TmpDir "$($Package.name).nupkg"
    Save-Verified -Url $Package.url -Dest $cache -Sha256 $Package.sha256 -Force:$Force

    $extract = Join-Path $TmpDir "$($Package.name)_x"
    Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
    Expand-Archive -LiteralPath $cache -DestinationPath $extract -Force
    return $extract
}

New-Item -ItemType Directory -Force -Path $TmpDir, $OrtDir, $ModelsDir | Out-Null

Write-Host "[deps] ONNX Runtime DirectML $OrtVersion + Microsoft.AI.DirectML $DmlVersion"
$ortPkg = Get-Nupkg ($Nupkgs | Where-Object { $_.name -eq "ort" })
$dmlPkg = Get-Nupkg ($Nupkgs | Where-Object { $_.name -eq "dml" })

# include/ (header) và lib/onnxruntime.lib, bin/onnxruntime.dll cho win-x64.
Copy-Item -Recurse -Force (Join-Path $ortPkg "build\native\include\*")        (Join-Path $OrtDir "include")
Copy-Item -Force          (Join-Path $ortPkg "runtimes\win-x64\native\onnxruntime.lib") (Join-Path $OrtDir "lib\onnxruntime.lib")
Copy-Item -Force          (Join-Path $ortPkg "runtimes\win-x64\native\onnxruntime.dll") (Join-Path $OrtDir "bin\onnxruntime.dll")
# DirectML runtime phải nằm cạnh onnxruntime.dll lúc chạy.
Copy-Item -Force          (Join-Path $dmlPkg "bin\x64-win\DirectML.dll")       (Join-Path $OrtDir "bin\DirectML.dll")
Write-Host "[deps] OK     : $OrtDir"

Write-Host "[deps] Model OCR"
foreach ($model in $Models) {
    Save-Verified -Url $model.url -Dest (Join-Path $ModelsDir $model.name) -Sha256 $model.sha256 -Force:$Force
}

Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
Write-Host "[deps] Hoàn tất."
