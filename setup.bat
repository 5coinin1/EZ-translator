@echo off
rem ============================================================================
rem  Setup MOT LAN: tai dependency (ONNX Runtime + model OCR + model dich CT2)
rem  + cai goi Python cho sidecar dich + build Release.
rem
rem  Sau khi xong: chay run.bat
rem ============================================================================
setlocal
set "ROOT=%~dp0"

echo.
echo === [1/3] Tai dependency (ONNX Runtime + model) ===
pwsh -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\fetch_deps.ps1"
if errorlevel 1 goto :fail

echo.
echo === [2/3] Python cho sidecar dich offline ===
pwsh -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\fetch_python.ps1"
if errorlevel 1 (
    echo [setup] Khong cai duoc Python/pip - app van chay nhung dich offline se dung Google.
)

echo.
echo === [3/3] Build Release ===
call "%ROOT%build.bat"
if errorlevel 1 goto :fail

echo.
echo [setup] HOAN TAT. Chay: run.bat
exit /b 0

:fail
echo.
echo [setup] That bai. Xem loi o tren.
exit /b 1
