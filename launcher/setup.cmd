@echo off
chcp 65001 >nul
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0OBS Quick RecordとOBS起動アシストをまとめてインストール.ps1" -NoPause %*
set "installResult=%errorlevel%"
echo.
if not "%installResult%"=="0" echo Installation failed. Please check the message above.
pause
exit /b %installResult%
