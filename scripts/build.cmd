@echo off
setlocal
where cl.exe >nul 2>nul
if not errorlevel 1 goto ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio C++ Build Tools and Windows SDK are required.
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL exit /b 1
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:ready
pushd "%~dp0.."
if not exist build mkdir build
pushd src
rc /nologo /fo "..\build\app.res" app.rc
if errorlevel 1 goto failure
cl /nologo /std:c++17 /utf-8 /W4 /O2 /MT /EHsc /DWIN32_LEAN_AND_MEAN /Fo"..\build\LightCapture.obj" LightCapture.cpp "..\build\app.res" /Fe"..\build\LightCapture.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT
if errorlevel 1 goto failure
popd
cl /nologo /std:c++17 /utf-8 /W4 /O2 /MT /EHsc /Fo"build\store_tests.obj" tests\store_tests.cpp /Fe"build\store_tests.exe"
if errorlevel 1 goto testfailure
cl /nologo /std:c++17 /utf-8 /W4 /O2 /MT /EHsc /DWIN32_LEAN_AND_MEAN /Fo"build\export_tests.obj" tests\export_tests.cpp /Fe"build\export_tests.exe"
if errorlevel 1 goto testfailure
popd
exit /b 0
:failure
popd
:testfailure
popd
exit /b 1
