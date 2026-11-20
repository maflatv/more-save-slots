@echo off
rem Builds zhlMoreSaveSlots.dll (x86) against REPENTOGON 1.1.3's libzhl.
rem   build.bat           build
rem   build.bat instalar  build and copy the DLL + graphics to the test copy (isaac_repentogon\Repentogon)
setlocal
set "ROOT=%~dp0"
set "RGON=%ROOT%build\rgon"
set "OUT=%ROOT%build\out"
set "BIN=%ROOT%bin"
set "GAME=%ROOT%..\..\isaac_repentogon\Repentogon"
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist "%RGON%\CMakeLists.txt" (
  echo REPENTOGON source missing in build\rgon. Clone tag 1.1.3:
  echo   git clone --branch 1.1.3 --depth 1 --recurse-submodules -c core.longpaths=true https://github.com/TeamREPENTOGON/REPENTOGON.git build\rgon
  exit /b 1
)

call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1

rem 1) libzhl (generates IsaacRepentance.h with zhlparser and the import library)
if not exist "%OUT%\Release\libzhl.lib" (
  "%CMAKE%" -S "%RGON%" -B "%OUT%" -G "Visual Studio 18 2026" -A Win32 || exit /b 1
  "%CMAKE%" --build "%OUT%" --config Release --target libzhl -- /m /v:minimal || exit /b 1
)

rem 2) the mod
if not exist "%BIN%" mkdir "%BIN%"
cl /nologo /c /O2 /MD /EHs /std:c++17 /utf-8 /W3 /wd4251 /wd4996 /DNOMINMAX ^
  /I"%RGON%\include" /I"%RGON%\libzhl" /I"%RGON%\libs\lua" /I"%RGON%\libs\rapidxml" ^
  /I"%RGON%\libs\LuaBridge\Source\LuaBridge" /I"%RGON%\libs\glad\include" ^
  /Fo"%OUT%\MoreSaveSlots.obj" "%ROOT%src\MoreSaveSlots.cpp" || exit /b 1
link /nologo /DLL /MAP:"%OUT%\zhlMoreSaveSlots.map" /OUT:"%BIN%\zhlMoreSaveSlots.dll" "%OUT%\MoreSaveSlots.obj" "%OUT%\Release\libzhl.lib" user32.lib shell32.lib userenv.lib || exit /b 1
echo Built: %BIN%\zhlMoreSaveSlots.dll

if /i "%~1"=="instalar" (
  rem old prototype names: two copies loaded at once would hook everything twice
  if exist "%GAME%\zhlMasRanuras.dll" del "%GAME%\zhlMasRanuras.dll"
  if exist "%GAME%\mas-ranuras" rmdir /s /q "%GAME%\mas-ranuras"
  copy /y "%BIN%\zhlMoreSaveSlots.dll" "%GAME%\zhlMoreSaveSlots.dll" >nul || exit /b 1
  if not exist "%GAME%\more-save-slots" mkdir "%GAME%\more-save-slots"
  copy /y "%ROOT%assets\*" "%GAME%\more-save-slots\" >nul || exit /b 1
  echo Installed in isaac_repentogon\Repentogon\zhlMoreSaveSlots.dll
)
endlocal
