@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "CONFIG=Release"

rem ---------- 0. Tai dependency nhi phan (ONNX Runtime + model) neu thieu ----------
set "NEED_DEPS="
if not exist "%ROOT%third_party\onnxruntime\bin\onnxruntime.dll" set "NEED_DEPS=1"
if not exist "%ROOT%models\ch_PP-OCRv4_det_infer.onnx" set "NEED_DEPS=1"
if defined NEED_DEPS (
    echo [build] Thieu dependency - dang tai ^(ONNX Runtime + model^)...
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\fetch_deps.ps1"
    if errorlevel 1 (
        echo [build] Tai dependency that bai.
        exit /b 1
    )
)

rem ---------- 1. Tim Qt6 ----------
set "QT="
if defined QTDIR if exist "%QTDIR%\bin\Qt6Core.dll" set "QT=%QTDIR%"
if not defined QT if exist "C:\msys64\mingw64\bin\Qt6Core.dll" set "QT=C:\msys64\mingw64"
if not defined QT call :scanqt "C:\Qt" "msvc"
if not defined QT call :scanqt "C:\Qt" "mingw"

if not defined QT (
    echo [build] Khong tim thay Qt6. Cai Qt6 hoac set bien QTDIR.
    exit /b 1
)

set "QTKIND=msvc"
echo(%QT%|findstr /i "msys64 mingw" >nul && set "QTKIND=mingw"
echo [build] Qt6 : %QT%  ^(%QTKIND%^)

rem ---------- 2. Configure ----------
if /i "%QTKIND%"=="mingw" (
    set "MINGWBIN="
    if exist "C:\msys64\mingw64\bin\g++.exe" set "MINGWBIN=C:\msys64\mingw64\bin"
    if not defined MINGWBIN if exist "%QT%\..\..\Tools\mingw1310_64\bin\g++.exe" set "MINGWBIN=%QT%\..\..\Tools\mingw1310_64\bin"
    if not defined MINGWBIN (
        echo [build] Khong tim thay trinh bien dich MinGW.
        exit /b 1
    )
    set "PATH=%QT%\bin;%MINGWBIN%;%PATH%"
    cmake -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH="%QT%" -DCMAKE_CXX_COMPILER=g++.exe
) else (
    cmake -B "%BUILD%" -DCMAKE_PREFIX_PATH="%QT%"
)
if errorlevel 1 exit /b 1

rem ---------- 3. Build ----------
cmake --build "%BUILD%" --config %CONFIG%
if errorlevel 1 exit /b 1

rem ---------- 4. Deploy Qt runtime canh exe ----------
set "APPDIR=%BUILD%\%CONFIG%"
if not exist "%APPDIR%\EZTranslator.exe" set "APPDIR=%BUILD%"
set "WDQ=%QT%\bin\windeployqt.exe"
if not exist "%WDQ%" set "WDQ=%QT%\bin\windeployqt6.exe"
if exist "%WDQ%" (
    "%WDQ%" --release --no-translations "%APPDIR%\EZTranslator.exe"
) else (
    echo [build] Khong co windeployqt - chay se dua vao PATH/QT_PLUGIN_PATH.
)

rem Qt6Test.dll cho cac target unit test (neu co)
if exist "%QT%\bin\Qt6Test.dll" if exist "%APPDIR%\test_detection.exe" (
    copy /y "%QT%\bin\Qt6Test.dll" "%APPDIR%\" >nul
)

echo.
echo [build] Hoan tat. Chay: run.bat
endlocal
exit /b 0

:scanqt
for /d %%V in ("%~1\6.*") do (
    for /d %%K in ("%%V\%~2*") do (
        if not defined QT if exist "%%K\bin\Qt6Core.dll" set "QT=%%K"
    )
)
exit /b 0
