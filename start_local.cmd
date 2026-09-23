@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo Edge Health Monitor - 启动本地设备监测
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_local.ps1" %*
if errorlevel 1 pause
