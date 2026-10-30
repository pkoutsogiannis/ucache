@echo off
REM ========================================================================
REM ucache PHP Extension Build Script (PHP 7.2.34 NTS x64)
REM ========================================================================
REM Usage:
REM   build.bat [install]
REM
REM   install:         Install to PHP ext folder after build
REM
REM Builds bin\Release\<version>\php72\php_ucache.dll, where <version> is the name of the current git branch
REM (the branches are named after the versions: 1.0.0.0, 1.0.0.1, ...), or else the version in precomp.h
REM (a detached HEAD, a branch of another name, or no git). The same way as mutx: MSBuild runs php_ucache.vcxproj
REM against the official devel pack (its headers and php7.lib), so no PHP SDK, phpize or configure.
REM ========================================================================

SET CONFIG=Release
SET INSTALL=0
IF /i "%1"=="install" SET INSTALL=1
SET MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
SET DEVEL_DIR=D:\Dev\php-devel\php-devel-pack-7.2.34-nts-Win32-VC15-x64
SET PHP_LIB_NAME=php7.lib
SET PHP_DIR=c:\php72

cd /d "%~dp0"

REM The version that names the output folder: the current git branch if its name is a version, else the version
REM that precomp.h defines
SET "GIT_BRANCH="
SET "UCACHE_VERSION="
FOR /F "delims=" %%B IN ('git rev-parse --abbrev-ref HEAD 2^>nul') DO SET "GIT_BRANCH=%%B"
IF DEFINED GIT_BRANCH echo %GIT_BRANCH%| findstr /R /X "[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*" >nul && SET "UCACHE_VERSION=%GIT_BRANCH%"
SET "SOURCE_VERSION="
FOR /F "tokens=3" %%V IN ('findstr /B /C:"#define PHP_UCACHE_VERSION " precomp.h') DO SET "SOURCE_VERSION=%%~V"
IF NOT DEFINED UCACHE_VERSION SET "UCACHE_VERSION=%SOURCE_VERSION%"
IF NOT DEFINED UCACHE_VERSION (
    echo ERROR: no version: the current git branch is not named after one, and precomp.h defines none
    pause
    exit /b 1
)
IF NOT "%UCACHE_VERSION%"=="%SOURCE_VERSION%" echo WARNING: the branch is %UCACHE_VERSION%, but precomp.h says %SOURCE_VERSION%
SET "OUT_DIR=bin\%CONFIG%\%UCACHE_VERSION%\php72"

REM Check if MSBuild exists
IF NOT EXIST %MSBUILD% (
    echo ERROR: MSBuild not found at %MSBUILD%
    echo Please install Visual Studio 2022 or update the MSBUILD path in this script.
    pause
    exit /b 1
)

IF NOT EXIST "%DEVEL_DIR%\include\main\php_version.h" (
    echo ERROR: no PHP devel pack at %DEVEL_DIR%
    pause
    exit /b 1
)

REM Update php_ucache.props with the devel pack's headers (PhpSdkRoot) and libraries
echo Updating php_ucache.props with PHP devel pack path...
>php_ucache.props echo ^<?xml version="1.0" encoding="utf-8"?^>
>>php_ucache.props echo ^<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"^>
>>php_ucache.props echo   ^<PropertyGroup Label="UserMacros"^>
>>php_ucache.props echo     ^<PhpSdkRoot^>%DEVEL_DIR%\include^</PhpSdkRoot^>
>>php_ucache.props echo     ^<PhpLibName^>%PHP_LIB_NAME%^</PhpLibName^>
>>php_ucache.props echo   ^</PropertyGroup^>
>>php_ucache.props echo   ^<ItemDefinitionGroup^>
>>php_ucache.props echo     ^<Link^>
>>php_ucache.props echo       ^<AdditionalLibraryDirectories^>%DEVEL_DIR%\lib;%%(AdditionalLibraryDirectories)^</AdditionalLibraryDirectories^>
>>php_ucache.props echo     ^</Link^>
>>php_ucache.props echo   ^</ItemDefinitionGroup^>
>>php_ucache.props echo ^</Project^>

echo.
echo ====================================================
echo Building ucache PHP Extension
echo Configuration: %CONFIG%
echo ucache version: %UCACHE_VERSION%
echo PHP devel pack: %DEVEL_DIR%
echo ====================================================
echo.

%MSBUILD% php_ucache.vcxproj /p:Configuration=%CONFIG% /p:Platform=x64 /p:OutDir=%OUT_DIR%\ /p:DebugSymbols=false /p:DebugType=None /p:GenerateDebugInformation=false /v:minimal /m

REM Not "IF ERRORLEVEL 1": that is false for the negative exit codes of a crashed process
IF NOT "%ERRORLEVEL%"=="0" (
    echo.
    echo ====================================================
    echo BUILD FAILED!
    echo ====================================================
    pause
    exit /b 1
)

REM Remove PDB files and intermediates, as mutx does
del /s /q bin\%CONFIG%\*.pdb >nul 2>&1
rd /s /q obj >nul 2>&1

echo.
echo ====================================================
echo BUILD SUCCESSFUL!
echo ====================================================
echo Output: %OUT_DIR%\php_ucache.dll
echo.

SET DLL=./%OUT_DIR:\=/%/php_ucache.dll

REM Install to PHP ext folder if requested
IF "%INSTALL%"=="1" (
    IF EXIST "%PHP_DIR%\ext" (
        echo Installing to %PHP_DIR%\ext\php_ucache.dll...
        copy /Y "%OUT_DIR%\php_ucache.dll" "%PHP_DIR%\ext\php_ucache.dll" >nul
        IF ERRORLEVEL 1 (
            echo ERROR: Failed to copy DLL to %PHP_DIR%\ext
            pause
            exit /b 1
        )
        REM Delete this build's folder, then bin\Release\<version>, bin\Release and bin once they are empty
        rd /S /Q "%OUT_DIR%" >nul 2>&1
        rd "bin\%CONFIG%\%UCACHE_VERSION%" >nul 2>&1
        rd "bin\%CONFIG%" >nul 2>&1
        rd "bin" >nul 2>&1
        SET DLL=%PHP_DIR%\ext\php_ucache.dll
        echo Successfully installed to %PHP_DIR%\ext\php_ucache.dll
    ) ELSE (
        echo ERROR: PHP ext directory not found: %PHP_DIR%\ext
    )
    echo.
)

REM Test the DLL; -n ignores php.ini, and the CLI needs ucache.enablecli=1 for the user cache
IF NOT EXIST "%PHP_DIR%\php.exe" (
    echo PHP not found at %PHP_DIR% - skipping test
    exit /b 0
)
"%PHP_DIR%\php.exe" -n -d "extension=%DLL%" -d ucache.enablecli=1 test-build.php
REM Any non-zero exit code fails, including a crash (negative NTSTATUS exit code)
IF NOT "%ERRORLEVEL%"=="0" (
    echo TEST FAILED!
    pause
    exit /b 1
)

echo.
exit /b 0
