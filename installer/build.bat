@echo off
rem Builds dist\MoreSaveSlotsInstaller.exe (portable, .NET Framework 4.8) with the plugin and its
rem graphics embedded. Build the plugin first (..\build.bat).
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "CSC="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -find MSBuild\**\Bin\Roslyn\csc.exe`) do set "CSC=%%i"
if not defined CSC for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*" "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do (
  for /d %%e in ("%%~v\*") do if exist "%%~e\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%%~e\MSBuild\Current\Bin\Roslyn\csc.exe"
)
if not defined CSC (
  echo Roslyn csc.exe not found. Install "Build Tools for Visual Studio" with ".NET desktop build tools".
  exit /b 1
)

if not exist "..\bin\zhlMoreSaveSlots.dll" (
  echo ..\bin\zhlMoreSaveSlots.dll is missing: run ..\build.bat first.
  exit /b 1
)

set "PAYLOAD=/resource:..\bin\zhlMoreSaveSlots.dll,payload.zhlMoreSaveSlots.dll"
for %%f in (..\assets\*) do set "PAYLOAD=!PAYLOAD! /resource:..\assets\%%~nxf,payload.assets.%%~nxf"

set "ICON="
if exist src\app.ico set "ICON=/win32icon:src\app.ico"

if not exist dist mkdir dist
"%CSC%" /nologo /target:winexe /optimize+ /platform:anycpu /out:dist\MoreSaveSlotsInstaller.exe ^
  /win32manifest:src\app.manifest %ICON% !PAYLOAD! /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
  src\*.cs || exit /b 1

echo Built: dist\MoreSaveSlotsInstaller.exe
