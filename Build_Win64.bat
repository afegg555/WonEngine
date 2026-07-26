@echo off
setlocal

cd /d "%~dp0"

set "build_target=%~1"
set "config_arg=%~2"
set "build_config=Release"

if /I "%build_target%"=="help" goto :usage
if /I "%build_target%"=="-h" goto :usage
if /I "%build_target%"=="--help" goto :usage
if /I "%build_target%"=="/?" goto :usage

if not "%~3"=="" (
    echo Too many arguments.
    goto :usage_error
)

if not "%config_arg%"=="" (
    if /I "%config_arg%"=="Debug" (
        set "build_config=Debug"
    ) else if /I "%config_arg%"=="Release" (
        set "build_config=Release"
    ) else if /I "%config_arg%"=="Shipping" (
        set "build_config=Shipping"
    ) else (
        echo Invalid build config: %config_arg%
        goto :usage_error
    )
)

set "cmake_exe=cmake"
set "vswhere_exe=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "vs_install_dir="
set "vs_version=[17.0,18.0^)"

where cmake >nul 2>nul
if errorlevel 1 (
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "cmake_exe=C:\Program Files\CMake\bin\cmake.exe"
    ) else (
        if exist "%vswhere_exe%" (
            for /f "usebackq delims=" %%i in (`"%vswhere_exe%" -latest -version %vs_version% -products * -property installationPath`) do (
                set "vs_install_dir=%%i"
            )
        )

        if defined vs_install_dir if exist "%vs_install_dir%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "cmake_exe=%vs_install_dir%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        ) else (
            echo cmake.exe not found.
            exit /b 1
        )
    )
)

echo Config: %build_config%
if defined build_target (
    echo Target: %build_target%
) else (
    echo Target: all
)
echo Output: Binary\Win64\%build_config%

"%cmake_exe%" -S . -B build
if errorlevel 1 (
    echo CMake configure failed.
    exit /b 1
)

if defined build_target (
    "%cmake_exe%" --build build --config %build_config% --target %build_target%
) else (
    "%cmake_exe%" --build build --config %build_config%
)
if errorlevel 1 (
    echo CMake build failed.
    exit /b 1
)

echo Build completed.
exit /b 0

:usage
echo Usage:
echo   Build_Win64.bat [target] [Release^|Debug^|Shipping]
echo.
echo Examples:
echo   Build_Win64.bat
echo   Build_Win64.bat Editor
echo   Build_Win64.bat Editor Debug
exit /b 0

:usage_error
echo.
echo Usage:
echo   Build_Win64.bat [target] [Release^|Debug^|Shipping]
exit /b 1
