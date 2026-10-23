param(
    [string]$PythonExecutable = "python"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$venvPython = Join-Path $repoRoot ".venv\Scripts\python.exe"
$modelDir = Join-Path $repoRoot "models\ct2-en-vi-int8"

if (-not (Test-Path -LiteralPath $venvPython)) {
    & $PythonExecutable -m venv (Join-Path $repoRoot ".venv")
    if ($LASTEXITCODE -ne 0) {
        & $PythonExecutable -m venv --without-pip (Join-Path $repoRoot ".venv")
    }
    if ($LASTEXITCODE -ne 0) { throw "Khong tao duoc Python virtual environment." }
}
& $venvPython -c "import ctranslate2, sentencepiece" *> $null
if ($LASTEXITCODE -ne 0) {
    & $venvPython -m pip --version *> $null
    if ($LASTEXITCODE -eq 0) {
        & $venvPython -m pip install ctranslate2 sentencepiece
    } else {
        & $PythonExecutable -m pip install --target (Join-Path $repoRoot ".venv\Lib\site-packages") ctranslate2 sentencepiece
    }
    if ($LASTEXITCODE -ne 0) { throw "Khong cai duoc CTranslate2/SentencePiece." }
}

New-Item -ItemType Directory -Path $modelDir -Force | Out-Null
$baseUrl = "https://huggingface.co/Sams200/opus-mt-en-vi/resolve/main/"
$sizes = @{
    "model.bin" = 287773463
    "shared_vocabulary.json" = 935068
    "source.spm" = 808612
    "target.spm" = 756198
}
foreach ($name in $sizes.Keys) {
    $path = Join-Path $modelDir $name
    if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -ne $sizes[$name]) {
        Invoke-WebRequest -Uri ($baseUrl + $name + "?download=true") -OutFile $path -TimeoutSec 600
    }
    if ((Get-Item -LiteralPath $path).Length -ne $sizes[$name]) {
        throw "File model khong du dung luong: $name"
    }
}

$configPath = Join-Path $modelDir "config.json"
Invoke-WebRequest -Uri ($baseUrl + "config.json?download=true") -OutFile $configPath -TimeoutSec 60
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($null -eq $config.layer_norm_epsilon) {
    $config.layer_norm_epsilon = 0.00001
    $json = $config | ConvertTo-Json -Depth 20
    [System.IO.File]::WriteAllText($configPath, $json, [System.Text.UTF8Encoding]::new($false))
}

& $venvPython -c "import ctranslate2, sentencepiece; ctranslate2.Translator(r'$modelDir', device='cpu', compute_type='int8')"
if ($LASTEXITCODE -ne 0) { throw "Model offline khong nap duoc." }
Write-Host "Model en->vi offline da san sang. Hay khoi dong lai EZTranslator."
