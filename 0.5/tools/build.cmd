@echo off
setlocal

set "ROOT=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "VSROOT=D:\BuildTools"
set "VSDEVCMD=%VSROOT%\Common7\Tools\VsDevCmd.bat"
set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CTEST=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "BUILD_DIR=%ROOT%\out\build\ninja-%CONFIG%"

if not exist "%VSDEVCMD%" (
    echo Visual Studio Build Tools was not found at:
    echo   %VSROOT%
    exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

"%CMAKE%" --fresh -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=%CONFIG% ^
    -DBUILD_TESTING=ON
if errorlevel 1 exit /b %errorlevel%

"%CMAKE%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 exit /b %errorlevel%

"%CTEST%" --test-dir "%BUILD_DIR%" --output-on-failure
exit /b %errorlevel%

