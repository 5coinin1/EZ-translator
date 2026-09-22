@echo off
setlocal

set "ROOT=%~dp0"
set "APP=%ROOT%build\EZTranslator.exe"
if exist "%ROOT%build\Release\EZTranslator.exe" set "APP=%ROOT%build\Release\EZTranslator.exe"
if exist "%ROOT%build\Debug\EZTranslator.exe"   set "APP=%ROOT%build\Debug\EZTranslator.exe"

if not exist "%APP%" (
    echo [run] Khong tim thay EZTranslator.exe. Hay build truoc.
    exit /b 1
)

for %%F in ("%APP%") do set "APPDIR=%%~dpF"

rem 1) Qt da duoc deploy canh exe (windeployqt)
if exist "%APPDIR%Qt6Core.dll" goto :run

rem 2) Nguoi dung chi dinh QTDIR
if defined QTDIR if exist "%QTDIR%\bin\Qt6Core.dll" (
    set "PATH=%QTDIR%\bin;%PATH%"
    set "QT_PLUGIN_PATH=%QTDIR%\plugins"
    goto :run
)

rem 3) Do toolchain cua exe: MinGW (libstdc++) hay MSVC
set "WANT=msvc"
findstr /m /c:"libstdc++-6.dll" "%APP%" >nul 2>&1 && set "WANT=mingw"

if "%WANT%"=="mingw" (
    if exist "C:\msys64\mingw64\bin\Qt6Core.dll" (
        set "PATH=C:\msys64\mingw64\bin;%PATH%"
        set "QT_PLUGIN_PATH=C:\msys64\mingw64\share\qt6\plugins"
        goto :run
    )
    call :scanqt "C:\Qt" "mingw"
) else (
    call :scanqt "C:\Qt" "msvc"
)

rem 4) Cuoi cung: bat ky kit Qt6 nao
call :scanqt "C:\Qt" ""

echo [run] Khong tim thay Qt6 runtime. Hay chay windeployqt hoac cai Qt6.
exit /b 1

:scanqt
for /d %%V in ("%~1\6.*") do (
    for /d %%K in ("%%V\%~2*") do (
        if exist "%%K\bin\Qt6Core.dll" (
            set "PATH=%%K\bin;%PATH%"
            set "QT_PLUGIN_PATH=%%K\plugins"
            goto :run
        )
    )
)
exit /b 0

:run
start "" "%APP%"
endlocal
