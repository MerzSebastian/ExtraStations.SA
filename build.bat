@echo off
rem Builds out\ExtraStations.SA.asi (32-bit) and copies the ini next to it. Wrapper for tools\build.ps1,
rem which finds the newest Visual Studio itself. The unit tests are run by tools\test.ps1.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" || exit /b 1
if not exist "%~dp0out" mkdir "%~dp0out"
copy /y "%~dp0data\ExtraStations.SA.ini" "%~dp0out\ExtraStations.SA.ini" >nul || exit /b 1
echo.
echo Built out\ExtraStations.SA.asi
