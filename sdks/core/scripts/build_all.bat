@echo off
REM Cross-platform build script for Chirp Core SDK on Windows
REM Builds the SDK for Windows, Android, and generates headers for other platforms

setlocal enabledelayedexpansion

REM Configuration
set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%\..\..
set BUILD_DIR=%PROJECT_ROOT%\build
set INSTALL_DIR=%PROJECT_ROOT%\install
set BUILD_TYPE=%BUILD_TYPE:-Release%

REM Default values
set BUILD_ANDROID=true
set BUILD_DESKTOP=true

REM Parse arguments
:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="--android" set BUILD_ANDROID=true
if /i "%~1"=="--no-android" set BUILD_ANDROID=false
if /i "%~1"=="--desktop" set BUILD_DESKTOP=true
if /i "%~1"=="--no-desktop" set BUILD_DESKTOP=false
if /i "%~1"=="--debug" set BUILD_TYPE=Debug
if /i "%~1"=="--release" set BUILD_TYPE=Release
shift
goto :parse_args
:end_parse

echo ========================================
echo Chirp Core SDK Build Script (Windows)
echo ========================================
echo Build Type: %BUILD_TYPE%
echo Build Android: %BUILD_ANDROID%
echo Build Desktop: %BUILD_DESKTOP%
echo.

REM Function to build for Windows
if "%BUILD_DESKTOP%"=="true" (
    echo Building for Windows...

    set BUILD_SUBDIR=%BUILD_DIR%\windows
    set INSTALL_SUBDIR=%INSTALL_DIR%\windows

    if not exist "%BUILD_SUBDIR%" mkdir "%BUILD_SUBDIR%"
    if not exist "%INSTALL_SUBDIR%" mkdir "%INSTALL_SUBDIR%"

    cmake -G "Visual Studio 17 2022" -A x64 ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DCMAKE_INSTALL_PREFIX="%INSTALL_SUBDIR%" ^
        -DBUILD_SHARED_LIBS=ON ^
        -DCHIRP_BUILD_SDK=ON ^
        -DCHIRP_BUILD_TESTS=OFF ^
        -B "%BUILD_SUBDIR%" ^
        -S "%PROJECT_ROOT%"

    if errorlevel 1 (
        echo [ERROR] CMake configuration failed
        exit /b 1
    )

    cmake --build "%BUILD_SUBDIR%" --config %BUILD_TYPE% --parallel
    if errorlevel 1 (
        echo [ERROR] Build failed
        exit /b 1
    )

    cmake --install "%BUILD_SUBDIR%" --config %BUILD_TYPE%
    if errorlevel 1 (
        echo [ERROR] Install failed
        exit /b 1
    )

    echo [OK] Windows build complete
)

REM Function to build for Android
if "%BUILD_ANDROID%"=="true" (
    echo Building for Android...

    REM Check for Android NDK
    if "%ANDROID_NDK%"=="" (
        if "%ANDROID_HOME%"=="" (
            echo [ERROR] ANDROID_HOME or ANDROID_NDK must be set
            exit /b 1
        )
        set ANDROID_NDK=%ANDROID_HOME%\ndk\25.2.9519653
    )

    if not exist "%ANDROID_NDK%" (
        echo [ERROR] Android NDK not found at %ANDROID_NDK%
        exit /b 1
    )

    set BUILD_SUBDIR=%BUILD_DIR%\android
    set INSTALL_SUBDIR=%INSTALL_DIR%\android

    if not exist "%BUILD_SUBDIR%" mkdir "%BUILD_SUBDIR%"

    REM Build for each architecture
    for %%A in (armeabi-v7a arm64-v8a x86_64) do (
        echo Building Android %%A...

        set ARCH_BUILD_DIR=%BUILD_SUBDIR%\%%A
        if not exist "%ARCH_BUILD_DIR%" mkdir "%ARCH_BUILD_DIR%"

        set ABI=%%A

        cmake -G "Ninja" ^
            -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
            -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%\build\cmake\android.toolchain.cmake" ^
            -DANDROID_ABI=!ABI! ^
            -DANDROID_PLATFORM=android-24 ^
            -DCMAKE_INSTALL_PREFIX="%INSTALL_SUBDIR%\%%A" ^
            -DBUILD_SHARED_LIBS=ON ^
            -DCHIRP_BUILD_SDK=ON ^
            -DCHIRP_BUILD_TESTS=OFF ^
            -B "!ARCH_BUILD_DIR!" ^
            -S "%PROJECT_ROOT%"

        if errorlevel 1 (
            echo [ERROR] CMake configuration for %%A failed
            exit /b 1
        )

        cmake --build "!ARCH_BUILD_DIR!" --config %BUILD_TYPE% --parallel
        if errorlevel 1 (
            echo [ERROR] Build for %%A failed
            exit /b 1
        )

        cmake --install "!ARCH_BUILD_DIR!" --config %BUILD_TYPE%
    )

    echo [OK] Android build complete
)

echo ========================================
echo Build complete!
echo ========================================
echo Install directory: %INSTALL_DIR%

endlocal
