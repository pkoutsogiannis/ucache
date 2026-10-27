@echo off
REM ========================================================================
REM ucache PHP Extension Build Script (PHP 7.2 to 8.5 NTS x64)
REM ========================================================================
REM Usage:
REM   build.bat <version> [install]
REM
REM   version:         PHP version: 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4, 8.5 (required)
REM   install:         Install to PHP ext folder once the tests pass
REM
REM Examples:
REM   build.bat                    - Shows usage
REM   build.bat 8.4                - Build and test for PHP 8.4
REM   build.bat 7.2                - Build and test for PHP 7.2
REM   build.bat 8.4 install        - Build and test for PHP 8.4, then install
REM
REM Builds bin\Release\phpXX\php_ucache.dll (php72, php73, php74, php80, php81, php82, php83, php84 or php85),
REM each version in a folder of its own, the same way as mutx: MSBuild runs php_ucache.vcxproj
REM against the official devel pack of the PHP version (its headers and php7.lib or php8.lib),
REM so no PHP SDK, phpize or configure. Unlike mutx there is no ts/nts parameter: ucache only
REM supports non-thread-safe (NTS) PHP (see config.w32). The DLL is then tested with the PHP in
REM PHP_DIR, without and with opcache, and installed into PHP_DIR\ext only if both runs pass.
REM
REM Each version is built with the Visual Studio toolset of its PHP binaries (v141 = VS 2017
REM for 7.2 to 7.4, v142 = VS 2019 for 8.0 to 8.3, v143 = VS 2022 for 8.4 and 8.5), which Visual
REM Studio 2022 can install as individual components. Since PHP 7.4, PHP refuses to load a module
REM linked with a newer linker than its own ("Can't load module ... as it's linked with 14.44, but
REM the core is linked with 14.29"), so a VS 2022 build does not load in PHP 7.4 to 8.3, nor a
REM VS 2019 build in PHP 7.4.
REM
REM Paths (PHP_DIR, PHP_DEVEL_ROOT, the folder of this script) may contain spaces and parentheses,
REM as in C:\Program Files (x86)\PHP: they are only expanded in quotes or outside parenthesized
REM blocks, where a ")" would end the block. Failures exit outside nested blocks, because "exit /b 1"
REM in a block nested in another one that has more commands makes "cmd /c build.bat ..." return 0.
REM ========================================================================

REM The variables set here and the current directory are restored when the script ends
SETLOCAL

REM ========================================================================
REM Parameter Validation
REM ========================================================================

REM Show usage if no parameters
IF "%~1%~2"=="" (
    call :show_usage
    exit /b 0
)
REM ... or for /? (passed on to CALL, it would print the help of CALL)
IF "%~1"=="/?" (
    call :show_usage
    exit /b 0
)

REM Initialize values
SET CONFIG=Release
SET INSTALL=0
SET MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

REM Parse first parameter (version) - required
IF "%~1"=="" (
    echo ERROR: PHP version parameter is required
    call :show_usage
    pause
    exit /b 1
)
IF /i "%~1"=="install" (
    echo ERROR: PHP version parameter is required before 'install'
    call :show_usage
    pause
    exit /b 1
)
SET "PHP_VER=%~1"

REM Map PHP versions to their details using subroutine
call :set_php_version "%PHP_VER%"
IF DEFINED PHP_VERSION goto version_ok
echo ERROR: Unsupported PHP version '%PHP_VER%'
echo Supported versions: 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4, 8.5
pause
exit /b 1
:version_ok

REM The output folder of the version in bin\Release: php72, php73, php74, php80, php81, php82, php83, php84 or php85
SET "PHP_FOLDER=php%PHP_VER:.=%"

REM Parse second parameter (install) - optional; there is no third one
IF /i "%~2"=="install" SET INSTALL=1
IF "%INSTALL%"=="1" goto install_parsed
IF "%~2"=="" goto install_parsed
echo ERROR: Invalid parameter '%~2'. Use 'install' or nothing
call :show_usage
pause
exit /b 1
:install_parsed
IF NOT "%~3"=="" (
    echo ERROR: Too many parameters
    call :show_usage
    pause
    exit /b 1
)

REM Check if MSBuild exists
IF NOT EXIST %MSBUILD% (
    echo ERROR: MSBuild not found at %MSBUILD%
    echo Please install Visual Studio 2022 or update the MSBUILD path in this script.
    pause
    exit /b 1
)

cd /d "%~dp0"

REM The official NTS devel pack of this PHP version from windows.php.net: the headers and import library of the
REM released binaries, so the DLL carries their build ID (e.g. API20240924,NTS,VS17) and loads in them. Downloaded
REM once into PHP_DEVEL_ROOT (default D:\Dev\php-devel), one folder per pack; the zip is deleted once unpacked.
IF NOT DEFINED PHP_DEVEL_ROOT SET "PHP_DEVEL_ROOT=D:\Dev\php-devel"
SET "PHP_DEVEL_ROOT=%PHP_DEVEL_ROOT:"=%"
SET "DEVEL_NAME=php-devel-pack-%PHP_VERSION%-nts-Win32-%VC_VERSION%-x64"
SET "DEVEL_DIR=%PHP_DEVEL_ROOT%\%DEVEL_NAME%"

REM Determine PHP library name based on major version (the NTS import library of the PHP DLL)
for /f "tokens=1 delims=." %%a in ("%PHP_VERSION%") do SET PHP_LIB_MAJOR=%%a
IF "%PHP_LIB_MAJOR%"=="7" ( SET "PHP_LIB_NAME=php7.lib" ) ELSE ( SET "PHP_LIB_NAME=php8.lib" )

REM Get the pack if its folder lacks the version header or the import library (e.g. after an interrupted unpack)
SET DEVEL_FOUND=1
IF NOT EXIST "%DEVEL_DIR%\include\main\php_version.h" SET DEVEL_FOUND=0
IF NOT EXIST "%DEVEL_DIR%\lib\%PHP_LIB_NAME%" SET DEVEL_FOUND=0
IF "%DEVEL_FOUND%"=="0" call :get_devel_pack
IF NOT EXIST "%DEVEL_DIR%\include\main\php_version.h" goto no_devel_pack
IF EXIST "%DEVEL_DIR%\lib\%PHP_LIB_NAME%" goto devel_pack_ok
:no_devel_pack
echo ERROR: no PHP devel pack at "%DEVEL_DIR%" (with include\main\php_version.h and lib\%PHP_LIB_NAME%)
pause
exit /b 1
:devel_pack_ok

REM The v141 toolset does not understand WindowsTargetPlatformVersion=10.0 (the newest Windows SDK, as the
REM vcxproj says), so pass the newest installed Windows 10/11 SDK by its version number. It is looked for in the
REM default folder, then in the folder that the registry gives (KitsRoot10), for an SDK installed elsewhere
SET WINSDK_VERSION=10.0
SET "WINSDK_INCLUDE=%ProgramFiles(x86)%\Windows Kits\10\Include"
call :find_winsdk
IF "%WINSDK_VERSION%"=="10.0" call :find_winsdk_in_registry

REM The Visual Studio 2022 component that installs the toolset, and whether vswhere finds it
IF "%TOOLSET%"=="v141" SET "TOOLSET_COMPONENT=MSVC v141 - VS 2017 C++ x64/x86 build tools"
IF "%TOOLSET%"=="v142" SET "TOOLSET_COMPONENT=MSVC v142 - VS 2019 C++ x64/x86 build tools"
IF "%TOOLSET%"=="v143" SET "TOOLSET_COMPONENT=MSVC v143 - VS 2022 C++ x64/x86 build tools"
call :find_toolset

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
echo Thread Safety: nts
echo PHP Version: %PHP_VER% (%PHP_VERSION%)
echo VC Version: %VC_VERSION%
echo Toolset: %TOOLSET% (%TOOLSET_COMPONENT%)
echo Windows SDK: %WINSDK_VERSION%
echo Platform: x64
echo PHP devel pack: %DEVEL_DIR%
echo PHP library: %PHP_LIB_NAME%
echo ====================================================
echo.

REM Build the project with the toolset of the PHP version (the global property overrides the vcxproj's v143),
REM into the version's folder (OutDir is relative to the project, and must end with a backslash), without PDB files
%MSBUILD% php_ucache.vcxproj /p:Configuration=%CONFIG% /p:Platform=x64 /p:PlatformToolset=%TOOLSET% /p:WindowsTargetPlatformVersion=%WINSDK_VERSION% /p:OutDir=bin\%CONFIG%\%PHP_FOLDER%\ /p:DebugSymbols=false /p:DebugType=None /p:GenerateDebugInformation=false /v:minimal /m

REM Not "IF ERRORLEVEL 1": that is false for the negative exit codes of a crashed process
IF NOT "%ERRORLEVEL%"=="0" (
    echo.
    echo ====================================================
    echo BUILD FAILED!
    echo ====================================================
    call :build_failed_hints
    pause
    exit /b 1
)

REM Remove PDB files and intermediates, as mutx does, and the incremental link-time code generation files
REM (.iobj, .ipdb) that the v141 toolset writes next to the DLL: the next build of the version then links in
REM full instead of reusing the code of this one
del /s /q bin\%CONFIG%\*.pdb >nul 2>&1
del /s /q bin\%CONFIG%\*.iobj >nul 2>&1
del /s /q bin\%CONFIG%\*.ipdb >nul 2>&1
rd /s /q obj >nul 2>&1

echo.
echo ====================================================
echo BUILD SUCCESSFUL!
echo ====================================================
echo Configuration: %CONFIG%
echo PHP Version: %PHP_VER% (%PHP_VERSION%)
echo Output: bin\%CONFIG%\%PHP_FOLDER%\php_ucache.dll
echo.

REM Test the built DLL before anything is installed. -n ignores php.ini, and the CLI only creates the user cache
REM with ucache.enablecli=1. The full path of the DLL as argument lets test-build.php start other PHP processes
REM that load it and share the cache. Without php.exe there is no test, and so no install either.
SET "DLL=%CD%\bin\%CONFIG%\%PHP_FOLDER%\php_ucache.dll"
IF EXIST "%PHP_DIR%\php.exe" goto run_tests
IF "%INSTALL%"=="1" goto cannot_test
echo PHP not found at "%PHP_DIR%" - skipping test
echo.
exit /b 0
:cannot_test
echo ERROR: PHP not found at "%PHP_DIR%": the DLL cannot be tested, so it is not installed
pause
exit /b 1

:run_tests
REM PHP reads a -d value as INI, where a path with spaces or parentheses must be in quotes: single quotes, which
REM keep backslashes as they are, or double quotes if the path has a single quote (as in C:\Users\O'Brien)
SET "DLL_INI='%DLL%'"
IF NOT "%DLL:'=%"=="%DLL%" SET DLL_INI=\"%DLL%\"
echo Running test-build.php with "%PHP_DIR%\php.exe" and "%DLL%"...
"%PHP_DIR%\php.exe" -n -d "extension=%DLL_INI%" -d ucache.enablecli=1 test-build.php "%DLL%"
SET TEST_EXIT=%ERRORLEVEL%
REM Any non-zero exit code fails, including a crash (negative NTSTATUS exit code)
IF NOT "%TEST_EXIT%"=="0" goto test_failed

REM The same tests with opcache, whose immutable arrays take other paths through the copy code. opcache is built
REM into PHP 8.5, and a zend_extension in the ext folder before
SET OPCACHE_ARGS=-d opcache.enable_cli=1
IF "%PHP_VER%"=="8.5" goto run_opcache_tests
SET "OPCACHE_DLL=%PHP_DIR%\ext\php_opcache.dll"
IF NOT EXIST "%OPCACHE_DLL%" goto no_opcache
SET "OPCACHE_INI='%OPCACHE_DLL%'"
IF NOT "%OPCACHE_DLL:'=%"=="%OPCACHE_DLL%" SET OPCACHE_INI=\"%OPCACHE_DLL%\"
SET OPCACHE_ARGS=-d "zend_extension=%OPCACHE_INI%" -d opcache.enable_cli=1
goto run_opcache_tests
:no_opcache
echo.
echo opcache not found at "%OPCACHE_DLL%" - skipping the test with opcache
goto tests_passed
:run_opcache_tests
echo.
echo Running test-build.php again with opcache...
"%PHP_DIR%\php.exe" -n -d "extension=%DLL_INI%" -d ucache.enablecli=1 %OPCACHE_ARGS% test-build.php "%DLL%"
SET TEST_EXIT=%ERRORLEVEL%
IF NOT "%TEST_EXIT%"=="0" goto test_failed

:tests_passed
echo.
IF NOT "%INSTALL%"=="1" exit /b 0

REM Install to PHP ext folder now that the tests have passed. The version's folder is deleted after the copy, and
REM bin\Release and bin once they are empty; the builds of the other versions stay
IF EXIST "%PHP_DIR%\ext\" goto install_dll
echo ERROR: PHP ext directory not found: "%PHP_DIR%\ext"
pause
exit /b 1
:install_dll
echo Installing to "%PHP_DIR%\ext\php_ucache.dll"...
copy /Y "%DLL%" "%PHP_DIR%\ext\php_ucache.dll" >nul
IF NOT "%ERRORLEVEL%"=="0" goto install_failed
rd /S /Q "bin\%CONFIG%\%PHP_FOLDER%" >nul 2>&1
rd "bin\%CONFIG%" >nul 2>&1
rd "bin" >nul 2>&1
echo Successfully installed to "%PHP_DIR%\ext\php_ucache.dll"
echo.
exit /b 0
:install_failed
echo ERROR: Failed to copy the DLL to "%PHP_DIR%\ext" (a running PHP process may hold the old one open)
pause
exit /b 1

:test_failed
echo.
echo TEST FAILED! Exit code: %TEST_EXIT%
IF "%INSTALL%"=="1" echo Nothing was installed.
pause
exit /b 1

REM ========================================================================
REM Helper Functions
REM ========================================================================

:show_usage
echo.
echo Usage: build.bat ^<version^> [install]
echo.
echo   version:         PHP version: 7.2, 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4, 8.5 (required)
echo   install:         Install to PHP ext folder once the tests pass (optional)
echo.
echo Builds php_ucache.dll for the non-thread-safe (NTS) x64 PHP of that version into
echo bin\Release\php^<major^>^<minor^> (php72, php73, php74, php80, php81, php82, php83, php84 or php85),
echo then tests it, without and with opcache, with the php.exe in that version's PHP_DIR
echo (the table at the end of build.bat).
echo.
echo Examples:
echo   build.bat 8.4                - Build and test for PHP 8.4
echo   build.bat 7.2                - Build and test for PHP 7.2
echo   build.bat 8.4 install        - Build and test for PHP 8.4, then install
echo.
exit /b 0

:set_php_version
REM Sets PHP_VERSION, VC_VERSION, TOOLSET and PHP_DIR based on PHP_VER parameter
REM Usage: call :set_php_version "8.4"
REM PHP_VERSION and VC_VERSION name the official devel pack of the version on windows.php.net and its compiler
REM tag as the file name spells it (VC15 in capitals for 7.2 and 7.3, vc15 for 7.4). The DLL loads in any NTS x64
REM PHP of that version built with that compiler, whatever its patch release: PHP checks the build ID
REM (e.g. API20240924,NTS,VS17). TOOLSET is the MSVC toolset of that compiler: since PHP 7.4, the linker must
REM not be newer than PHP's in the tens of its minor version, which is all that PHP compares (PHP_LINKER_MAJOR and
REM PHP_LINKER_MINOR in the devel pack's include\main\config.w32.h: 14.16 for 7.4's vc15, 14.29 for vs16, 14.44 for
REM vs17; 7.2 and 7.3 define none and check nothing), so 8.0.0, linked with 14.28, loads a 14.29 DLL.
REM PHP_DIR is the PHP installation whose php.exe runs the tests and that install copies the DLL into (PHP_DIR\ext).
REM Keep every value in quotes: a PHP_DIR such as C:\Program Files (x86)\PHP would otherwise end the block.
SET PHP_VERSION=
SET VC_VERSION=
SET TOOLSET=
SET PHP_DIR=
IF "%~1"=="7.2" ( SET "PHP_VERSION=7.2.34" & SET "VC_VERSION=VC15" & SET "TOOLSET=v141" & SET "PHP_DIR=c:\php72" & exit /b 0 )
IF "%~1"=="7.3" ( SET "PHP_VERSION=7.3.33" & SET "VC_VERSION=VC15" & SET "TOOLSET=v141" & SET "PHP_DIR=c:\php73" & exit /b 0 )
IF "%~1"=="7.4" ( SET "PHP_VERSION=7.4.33" & SET "VC_VERSION=vc15" & SET "TOOLSET=v141" & SET "PHP_DIR=c:\php74" & exit /b 0 )
IF "%~1"=="8.0" ( SET "PHP_VERSION=8.0.30" & SET "VC_VERSION=vs16" & SET "TOOLSET=v142" & SET "PHP_DIR=c:\php80" & exit /b 0 )
IF "%~1"=="8.1" ( SET "PHP_VERSION=8.1.34" & SET "VC_VERSION=vs16" & SET "TOOLSET=v142" & SET "PHP_DIR=c:\php81" & exit /b 0 )
IF "%~1"=="8.2" ( SET "PHP_VERSION=8.2.35" & SET "VC_VERSION=vs16" & SET "TOOLSET=v142" & SET "PHP_DIR=c:\php82" & exit /b 0 )
IF "%~1"=="8.3" ( SET "PHP_VERSION=8.3.36" & SET "VC_VERSION=vs16" & SET "TOOLSET=v142" & SET "PHP_DIR=c:\php83" & exit /b 0 )
IF "%~1"=="8.4" ( SET "PHP_VERSION=8.4.27" & SET "VC_VERSION=vs17" & SET "TOOLSET=v143" & SET "PHP_DIR=c:\php84" & exit /b 0 )
IF "%~1"=="8.5" ( SET "PHP_VERSION=8.5.12" & SET "VC_VERSION=vs17" & SET "TOOLSET=v143" & SET "PHP_DIR=c:\php85" & exit /b 0 )
exit /b 1

:find_winsdk
REM Sets WINSDK_VERSION to the newest SDK version folder in WINSDK_INCLUDE that has um\windows.h
for /f "delims=" %%d in ('dir /b /ad /o:n "%WINSDK_INCLUDE%\10.*" 2^>nul') do IF EXIST "%WINSDK_INCLUDE%\%%d\um\windows.h" SET "WINSDK_VERSION=%%d"
exit /b 0

:find_winsdk_in_registry
REM Looks for the SDK under KitsRoot10, the folder that the SDK installer records in the registry (64-bit view,
REM then 32-bit view), e.g. D:\Windows Kits\10\
SET KITS_ROOT=
for /f "tokens=1,2,*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 2^>nul') do IF /i "%%a"=="KitsRoot10" SET "KITS_ROOT=%%c"
IF NOT DEFINED KITS_ROOT for /f "tokens=1,2,*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 /reg:32 2^>nul') do IF /i "%%a"=="KitsRoot10" SET "KITS_ROOT=%%c"
IF NOT DEFINED KITS_ROOT exit /b 0
IF "%KITS_ROOT:~-1%"=="\" SET "KITS_ROOT=%KITS_ROOT:~0,-1%"
SET "WINSDK_INCLUDE=%KITS_ROOT%\Include"
call :find_winsdk
exit /b 0

:find_toolset
REM Sets TOOLSET_FOUND to 1 if vswhere finds a Visual Studio with the toolset that MSBuild 2022 can use: VS 2022
REM with the toolset's component, or the VS 2017 (v141) or VS 2019 (v142) whose own toolset it is (a test of the
REM folders of VS 2022 would miss those). Sets it to 0 if not, and leaves it empty without vswhere.
SET TOOLSET_FOUND=
SET "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
IF NOT EXIST "%VSWHERE%" exit /b 0
SET TOOLSET_FOUND=0
SET "VS_COMPONENT_ID=Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
SET VS_OWN_VERSIONS=
IF "%TOOLSET%"=="v141" ( SET "VS_COMPONENT_ID=Microsoft.VisualStudio.Component.VC.v141.x86.x64" & SET "VS_OWN_VERSIONS=[15.0,16.0)" )
IF "%TOOLSET%"=="v142" ( SET "VS_COMPONENT_ID=Microsoft.VisualStudio.ComponentGroup.VC.Tools.142.x86.x64" & SET "VS_OWN_VERSIONS=[16.0,17.0)" )
"%VSWHERE%" -products * -version "[17.0,18.0)" -requires %VS_COMPONENT_ID% -property instanceId 2>nul | findstr /r "." >nul && SET TOOLSET_FOUND=1
IF "%TOOLSET_FOUND%"=="1" exit /b 0
IF NOT DEFINED VS_OWN_VERSIONS exit /b 0
"%VSWHERE%" -products * -version "%VS_OWN_VERSIONS%" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property instanceId 2>nul | findstr /r "." >nul && SET TOOLSET_FOUND=1
exit /b 0

:build_failed_hints
REM After a failed build, names the Visual Studio 2022 component of the toolset unless vswhere found the toolset,
REM and the SDK if none was found. MSBuild's own message for a missing toolset (MSB8020) suggests retargeting the
REM project, which builds a DLL that PHP 7.4 to 8.3 refuse to load.
IF "%TOOLSET_FOUND%"=="1" goto sdk_hint
echo.
IF "%TOOLSET_FOUND%"=="0" echo vswhere found no Visual Studio with the %TOOLSET% toolset that PHP %PHP_VER% needs.
IF NOT "%TOOLSET_FOUND%"=="0" echo If MSBuild reported MSB8020, the %TOOLSET% toolset that PHP %PHP_VER% needs is missing.
echo Install the Visual Studio 2022 component "%TOOLSET_COMPONENT%"
echo with the Visual Studio Installer (Modify, Individual components), then run build.bat again.
echo Do not retarget the project to a newer toolset: since PHP 7.4, PHP refuses a DLL linked with
echo a newer linker than its own.
:sdk_hint
IF NOT "%WINSDK_VERSION%"=="10.0" exit /b 0
echo.
echo No Windows 10/11 SDK was found: install one with the Visual Studio Installer (Individual components).
exit /b 0

:get_devel_pack
REM Downloads the zip of the pack from windows.php.net (the current releases, then the archive) into
REM PHP_DEVEL_ROOT, unpacks it into DEVEL_DIR without its top folder, checks the result and deletes the zip.
REM curl writes to a .part file that is renamed once complete, so an interrupted download is never taken for a
REM pack. A 404 from the current releases only means that the pack is in the archive, so that message is kept
REM back. On failure the zip and the partly unpacked folder are deleted, and the next run starts afresh.
REM System32's tar is bsdtar, which unpacks zip files; a GNU tar earlier in PATH cannot.
SET "CURL=curl"
IF EXIST "%SystemRoot%\System32\curl.exe" SET "CURL=%SystemRoot%\System32\curl.exe"
SET "TAR=tar"
IF EXIST "%SystemRoot%\System32\tar.exe" SET "TAR=%SystemRoot%\System32\tar.exe"
IF NOT EXIST "%PHP_DEVEL_ROOT%\" mkdir "%PHP_DEVEL_ROOT%" >nul 2>&1
IF EXIST "%PHP_DEVEL_ROOT%\" goto devel_root_ok
echo ERROR: cannot create the folder "%PHP_DEVEL_ROOT%" for the devel packs: set PHP_DEVEL_ROOT to another folder
exit /b 1
:devel_root_ok
SET "DEVEL_ZIP=%PHP_DEVEL_ROOT%\%DEVEL_NAME%.zip"
IF EXIST "%DEVEL_ZIP%" goto devel_unpack
echo Downloading %DEVEL_NAME%.zip from windows.php.net...
SET "CURL_ERRORS=%TEMP%\%DEVEL_NAME%.curl.txt"
"%CURL%" -fsSL -o "%DEVEL_ZIP%.part" "https://windows.php.net/downloads/releases/%DEVEL_NAME%.zip" 2>"%CURL_ERRORS%"
SET CURL_EXIT=%ERRORLEVEL%
IF NOT "%CURL_EXIT%"=="22" goto devel_downloaded
"%CURL%" -fsSL -o "%DEVEL_ZIP%.part" "https://windows.php.net/downloads/releases/archives/%DEVEL_NAME%.zip" 2>"%CURL_ERRORS%"
SET CURL_EXIT=%ERRORLEVEL%
:devel_downloaded
IF "%CURL_EXIT%"=="0" goto devel_rename
del /q "%DEVEL_ZIP%.part" >nul 2>&1
type "%CURL_ERRORS%" 2>nul
del /q "%CURL_ERRORS%" >nul 2>&1
IF "%CURL_EXIT%"=="22" (
    echo ERROR: %DEVEL_NAME%.zip was not found on windows.php.net, neither in the current releases nor in the archive
    exit /b 1
)
echo ERROR: could not download %DEVEL_NAME%.zip from windows.php.net: curl exit code %CURL_EXIT%
exit /b 1
:devel_rename
del /q "%CURL_ERRORS%" >nul 2>&1
move /y "%DEVEL_ZIP%.part" "%DEVEL_ZIP%" >nul
IF NOT "%ERRORLEVEL%"=="0" (
    echo ERROR: could not rename the download to "%DEVEL_ZIP%"
    exit /b 1
)
:devel_unpack
echo Unpacking %DEVEL_NAME%.zip...
IF NOT EXIST "%DEVEL_DIR%\" mkdir "%DEVEL_DIR%" >nul 2>&1
"%TAR%" -xf "%DEVEL_ZIP%" -C "%DEVEL_DIR%" --strip-components=1
SET TAR_EXIT=%ERRORLEVEL%
SET DEVEL_PROBLEM=
IF NOT "%TAR_EXIT%"=="0" SET "DEVEL_PROBLEM=tar exit code %TAR_EXIT%"
IF NOT DEFINED DEVEL_PROBLEM IF NOT EXIST "%DEVEL_DIR%\include\main\php_version.h" SET "DEVEL_PROBLEM=no include\main\php_version.h in it"
IF NOT DEFINED DEVEL_PROBLEM IF NOT EXIST "%DEVEL_DIR%\lib\%PHP_LIB_NAME%" SET "DEVEL_PROBLEM=no lib\%PHP_LIB_NAME% in it"
IF DEFINED DEVEL_PROBLEM goto devel_unpack_failed
del /q "%DEVEL_ZIP%" >nul 2>&1
echo Unpacked into "%DEVEL_DIR%"; the zip has been deleted
exit /b 0
:devel_unpack_failed
echo ERROR: %DEVEL_NAME%.zip could not be unpacked (%DEVEL_PROBLEM%): deleting it and "%DEVEL_DIR%"
del /q "%DEVEL_ZIP%" >nul 2>&1
rd /s /q "%DEVEL_DIR%" >nul 2>&1
exit /b 1
