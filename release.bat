@echo off
set "GAME_EXE=DoubleSteal"
cd /d "%~dp0"
set "OUT=out\build\release"
if not exist "%OUT%\%GAME_EXE%.exe" call build.bat || exit /b 1
rem the configure step writes where the SDK is
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
if not exist "%OUT%\sdk_dir.txt" echo %OUT%\sdk_dir.txt was not written, update the SDK. && exit /b 1
set /p SDK=<out\build\release\sdk_dir.txt
del /q "%GAME_EXE%.zip" 2>nul
rem an empty assets folder beside the exe, where the game (files or .iso) goes
rd /s /q "%OUT%\release" 2>nul
mkdir "%OUT%\release\assets"
rem 7-Zip on the PATH, or where its installer puts it
set "SEVENZIP=7z"
where 7z >nul 2>nul || set "SEVENZIP=%ProgramFiles%\7-Zip\7z.exe"
"%SEVENZIP%" a "%GAME_EXE%.zip" ".\%OUT%\%GAME_EXE%.exe" ".\%OUT%\release\assets" .\README.md "%SDK%\THIRD_PARTY.txt"
