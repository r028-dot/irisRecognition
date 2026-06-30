@echo off
setlocal

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set SRC=%~dp0
set BUILD=%~dp0build

echo === Step 1: CMake Configure ===
%CMAKE% -S "%SRC%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_MANIFEST_MODE=OFF
if ERRORLEVEL 1 (
    echo [FAILED] CMake configure failed!
    pause
    exit /b 1
)
echo [OK] Configure done.

echo.
echo === Step 2: Build Release targets ===
%CMAKE% --build "%BUILD%" --config Release --target IrisRecognitionServer EnrollTool IrisSimulation -- /m
if ERRORLEVEL 1 (
    echo [FAILED] Build failed!
    pause
    exit /b 1
)

echo.
echo === Build complete! ===
echo Executables in: %BUILD%\Release\
dir "%BUILD%\Release\*.exe"
pause
