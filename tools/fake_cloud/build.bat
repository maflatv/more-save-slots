@echo off
rem Compila zhlFakeCloud.dll (SOLO PRUEBAS). No necesita libzhl.
rem   build.bat           compila
rem   build.bat instalar  la copia a isaac_repentogon\Repentogon (solo la copia de pruebas)
rem   build.bat quitar    la borra de la copia de pruebas
setlocal
set "HERE=%~dp0"
set "DEST=%HERE%..\..\..\..\isaac_repentogon\Repentogon\zhlFakeCloud.dll"
if /i "%~1"=="quitar" (
  if exist "%DEST%" del "%DEST%"
  echo Quitada de la copia de pruebas.
  exit /b 0
)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist "%HERE%bin" mkdir "%HERE%bin"
cl /nologo /O2 /MD /EHsc /std:c++17 /utf-8 /LD "%HERE%FakeCloud.cpp" /Fo"%HERE%bin\\" /Fe"%HERE%bin\zhlFakeCloud.dll" /link user32.lib || exit /b 1
if /i "%~1"=="instalar" (
  copy /y "%HERE%bin\zhlFakeCloud.dll" "%DEST%" >nul || exit /b 1
  echo Instalada en la copia de pruebas.
)
endlocal
