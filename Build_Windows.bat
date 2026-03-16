@echo off
setlocal

cd /d "%~dp0"

if not exist build (
    mkdir build
)

cd /d build
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

"%cmake_exe%" ..
