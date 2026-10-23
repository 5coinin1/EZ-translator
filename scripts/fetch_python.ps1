# Cài Python + gói cho sidecar dịch offline (CTranslate2): ctranslate2 + sentencepiece.
#
#   pwsh -File scripts/fetch_python.ps1
#
# Idempotent: gói đã có thì bỏ qua. Không tìm thấy Python thì in lệnh cài (winget).
# App tự dò Python qua `py` / `python` / `python3` (hoặc biến EZ_PYTHON).
param(
    [string]$Python = $env:EZ_PYTHON
)

$ErrorActionPreference = "Continue"

function Find-Python {
    $candidates = @()
    if ($Python) { $candidates += $Python }
    $candidates += @("py", "python", "python3")
    foreach ($cand in $candidates) {
        if (-not $cand) { continue }
        try {
            $cmd = Get-Command $cand -ErrorAction Stop
        } catch {
            continue
        }
        $exe = $cmd.Source
        # `py` launcher: lấy python.exe THẬT (launcher sinh thêm tiến trình con).
        if ([IO.Path]::GetFileNameWithoutExtension($exe) -eq "py") {
            $real = & $exe -3 -c "import sys; print(sys.executable)" 2>$null
            if ($real -and (Test-Path $real.Trim())) { return $real.Trim() }
            continue
        }
        return $exe
    }
    return $null
}

$exe = Find-Python
if (-not $exe) {
    Write-Host "[python] Không tìm thấy Python 3. Cài bằng: winget install Python.Python.3.12"
    Write-Host "         (hoặc đặt biến EZ_PYTHON trỏ tới python.exe)"
    exit 1
}
Write-Host "[python] dùng   : $exe"

& $exe -c "import ctranslate2, sentencepiece" 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "[python] có sẵn : ctranslate2 + sentencepiece"
    exit 0
}

Write-Host "[python] cài    : ctranslate2 + sentencepiece (pip --user)"
& $exe -m pip install --user --upgrade ctranslate2 sentencepiece
if ($LASTEXITCODE -ne 0) {
    Write-Host "[python] pip install thất bại. Chạy tay:"
    Write-Host "         $exe -m pip install --user ctranslate2 sentencepiece"
    exit 1
}

& $exe -c "import ctranslate2, sentencepiece" 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "[python] OK — sidecar dịch offline sẵn sàng"
    exit 0
}
Write-Host "[python] Cài xong nhưng import vẫn lỗi — kiểm tra lại Python/pip."
exit 1
