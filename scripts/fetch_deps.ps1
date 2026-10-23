# Tải các dependency nhị phân (ONNX Runtime + model OCR + model dịch CT2) cho EZTranslator.
#
#   pwsh -File scripts/fetch_deps.ps1             # tải phần còn thiếu
#   pwsh -File scripts/fetch_deps.ps1 -Force      # tải lại tất cả
#   pwsh -File scripts/fetch_deps.ps1 -IncludeMt  # kèm model Marian ONNX en→vi (~600MB, không cần)
#
# Mọi file đều được kiểm tra SHA256 (trừ gói model CT2 tải từ GitHub Release của repo).
# Script idempotent: file đã có và đúng hash thì bỏ qua. Nguồn:
#   - ONNX Runtime DirectML : NuGet Microsoft.ML.OnnxRuntime.DirectML
#   - DirectML runtime      : NuGet Microsoft.AI.DirectML
#   - Model PP-OCRv4        : RapidOCR (SWHL/RapidOCR trên HuggingFace,
#                             RapidAI/RapidOCR trên ModelScope)
#   - Từ điển               : PaddlePaddle/PaddleOCR
#   - Model chữ Nhật        : cycloneboy/japan_PP-OCRv4_rec_infer (HuggingFace)
#   - Model dịch CT2        : models-ct2.zip trong GitHub Release của repo
param(
    [switch]$Force,
    [switch]$IncludeMt
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
    @{ name = "en_dict.txt";                 url = "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/en_dict.txt";                     sha256 = "5662DF9D2D03F0E8CA0D3B0649D6ACBAB904B6A14B3D3521463C71C37C668CE3" },
    @{ name = "japan_PP-OCRv4_rec_infer.onnx"; url = "https://huggingface.co/cycloneboy/japan_PP-OCRv4_rec_infer/resolve/main/model.onnx";                         sha256 = "BB0F23444C059D27A615C1A426414240BBD02D3658900D0455E007DC47D3E4A4" },
    @{ name = "japan_dict.txt";              url = "https://huggingface.co/cycloneboy/japan_PP-OCRv4_rec_infer/resolve/main/japan_dict.txt";                      sha256 = "2A0842E81EF31D6A99D811A36FC12C33E37AA9406667EF1BD9B468F9108A66BF" }
)

# {name, url, sha256} — model dịch OFFLINE Marian/OPUS-MT (en→vi), tải vào models/.
# Cần cho engine dịch không dùng mạng. Dùng -SkipMt nếu chỉ muốn engine online.
$MtRepo = "https://huggingface.co/Xenova/opus-mt-en-vi/resolve/main"
$MtModels = @(
    @{ name = "opus-mt-en-vi/encoder_model.onnx";             url = "$MtRepo/onnx/encoder_model.onnx";             sha256 = "BE90B557001B7EB13B5056872A079D19F1D459397A0FC1A23809751A9DCA91BF" },
    @{ name = "opus-mt-en-vi/decoder_model.onnx";             url = "$MtRepo/onnx/decoder_model.onnx";             sha256 = "6749FBAA057790F6AA24044D6C4C8E9E9866C4C09643C7A626A212423EF62EF6" },
    @{ name = "opus-mt-en-vi/decoder_with_past_model.onnx";   url = "$MtRepo/onnx/decoder_with_past_model.onnx";   sha256 = "3EFADF66D7039A6A5C42F565F9E99AC1F270F46535AB7363E0004EC635891B4D" },
    @{ name = "opus-mt-en-vi/source.spm";                     url = "$MtRepo/source.spm";                         sha256 = "FDD9AB486CA8EA060967105E30DB7D8928729C78A67448711D2DD7C3153B37CD" },
    @{ name = "opus-mt-en-vi/target.spm";                     url = "$MtRepo/target.spm";                         sha256 = "FB8AD63C818F073C246952C91B2CB1F16AA30728BF51B5C179B8481BC0E20BB5" },
    @{ name = "opus-mt-en-vi/vocab.json";                     url = "$MtRepo/vocab.json";                         sha256 = "BCC8BBAFEB0B6B58FECF244F8DA996DA9AA45DBEA7871164F9C8CE480507F9FE" },
    @{ name = "opus-mt-en-vi/config.json";                    url = "$MtRepo/config.json";                        sha256 = "0C5AB981C2F5A4878C78522C8A61E3D733F5E3BE6B1F83B68BBFB7F00E604637" }
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

# Đã có đủ ORT + DirectML thì bỏ qua (tránh tải lại ~100MB mỗi lần chạy).
$ortReady = (Test-Path (Join-Path $OrtDir "bin\onnxruntime.dll")) `
            -and (Test-Path (Join-Path $OrtDir "bin\DirectML.dll")) `
            -and (Test-Path (Join-Path $OrtDir "lib\onnxruntime.lib")) `
            -and (Test-Path (Join-Path $OrtDir "include\onnxruntime_cxx_api.h"))
if ($ortReady -and -not $Force) {
    Write-Host "[deps] có sẵn : ONNX Runtime DirectML $OrtVersion + DirectML $DmlVersion"
} else {
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
}

Write-Host "[deps] Model OCR"
foreach ($model in $Models) {
    Save-Verified -Url $model.url -Dest (Join-Path $ModelsDir $model.name) -Sha256 $model.sha256 -Force:$Force
}

if ($IncludeMt) {
    Write-Host "[deps] Model dịch offline Marian/OPUS-MT en→vi (~600MB) — tuỳ chọn"
    foreach ($model in $MtModels) {
        Save-Verified -Url $model.url -Dest (Join-Path $ModelsDir ($model.name -replace '/', '\')) `
                      -Sha256 $model.sha256 -Force:$Force
    }
} else {
    Write-Host "[deps] Bỏ qua Marian ONNX en→vi (~600MB, không cần khi có CT2) — thêm -IncludeMt nếu muốn"
}

# Model dịch CT2 (en/zh/ja → vi): đóng gói sẵn trong GitHub Release của repo để khỏi phải convert
# transformers → ct2 (cần torch ~2GB). Thiếu release thì app vẫn chạy, chỉ mất đường dịch offline.
Write-Host "[deps] Model dịch CT2 (en/zh/ja → vi)"
$ct2Names = @("ct2-en-vi-int8", "ct2-zh-vi", "ct2-ja-vi")
$ct2Missing = @()
foreach ($name in $ct2Names) {
    if (-not (Test-Path (Join-Path $ModelsDir "$name\model.bin"))) { $ct2Missing += $name }
}
if ($ct2Missing.Count -eq 0) {
    Write-Host "[deps] có sẵn : model CT2 (en/zh/ja → vi)"
} else {
    $ct2Url = "https://github.com/5coinin1/EZ-translator/releases/latest/download/models-ct2.zip"
    $ct2Zip = Join-Path $TmpDir "models-ct2.zip"
    try {
        Write-Host "[deps] tải    : $ct2Url"
        Invoke-WebRequest -Uri $ct2Url -OutFile $ct2Zip -MaximumRedirection 5 -UseBasicParsing
        Expand-Archive -LiteralPath $ct2Zip -DestinationPath $ModelsDir -Force
        Write-Host "[deps] OK     : model CT2"
    } catch {
        Write-Warning "[deps] Không tải được models-ct2.zip (release chưa có?) — app vẫn chạy nhưng"
        Write-Warning "       dịch offline cần model: tải models-ct2.zip từ Release rồi giải nén vào models\."
    }
}

Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
Write-Host "[deps] Hoàn tất."
