@echo off
set PATH=C:\msys64\mingw64\bin;%PATH%
set QT_PLUGIN_PATH=C:\msys64\mingw64\share\qt6\plugins
start "" "%~dp0build\EZTranslator.exe"
