@echo off
setlocal

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set SRC=%~dp0
set BUILD=%~dp0build

echo === CMake Configure ===
%CMAKE% -S "%SRC%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_MANIFEST_MODE=OFF
if ERRORLEVEL 1 (
    echo [FAILED] Configure failed
    pause
    exit /b 1
)

echo === Build IrisGateGUI ===
%CMAKE% --build "%BUILD%" --config Release --target IrisGateGUI
if ERRORLEVEL 1 (
    echo [FAILED] Build failed
    pause
    exit /b 1
)

echo === Build Complete ===
echo Executable: %BUILD%\Release\IrisGateGUI.exe
dir "%BUILD%\Release\*.exe"
pause
