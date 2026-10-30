@echo off
REM ========================================================================
REM ucache PHP Extension Build Script for every PHP version (7.2 to 8.5 NTS x64)
REM ========================================================================
REM Usage:
REM   build-all.bat [install]
REM
REM   install:         Install each version into its PHP ext folder once its tests pass
REM
REM Runs build.bat for each PHP version, one after the other: 7.3, 7.4, 8.0, 8.1, 8.2, 8.3, 8.4, 8.5, then 7.2
REM last, so that php_ucache.props (which build.bat rewrites for the version it builds) ends as it is in git, set
REM for 7.2. Each build goes to bin\Release\<version>\phpXX\php_ucache.dll and is tested as build.bat does (without
REM and with opcache). A failed version does not stop the others: the summary at the end lists the result of each,
REM and the exit code is 1 if any failed. build.bat's input comes from nul, so that its "pause" after an error does
REM not wait for a key in the middle of the run.
REM
REM The versions are built and tested one at a time, not at once: the timing checks of test-build.php only hold on
REM a machine that is not busy with the other builds.
REM
REM install overwrites php_ucache.dll in the ext folder of every PHP installation in build.bat's table
REM (c:\php72 to c:\php85), each only if its tests passed.
REM ========================================================================

REM The variables set here and the current directory are restored when the script ends
SETLOCAL

IF "%~1"=="/?" goto show_usage
SET INSTALL=
IF /i "%~1"=="install" SET INSTALL=install
IF DEFINED INSTALL goto args_ok
IF "%~1"=="" goto args_ok
echo ERROR: Invalid parameter '%~1'. Use 'install' or nothing
goto show_usage_failed
:args_ok
IF NOT "%~2"=="" (
    echo ERROR: Too many parameters
    goto show_usage_failed
)

cd /d "%~dp0"

SET VERSIONS=7.3 7.4 8.0 8.1 8.2 8.3 8.4 8.5 7.2
SET FAILED=
SET PASSED=

FOR %%V IN (%VERSIONS%) DO call :build_version %%V

echo.
echo ====================================================
echo SUMMARY
echo ====================================================
IF DEFINED PASSED echo Passed:%PASSED%
IF DEFINED FAILED echo Failed:%FAILED%
echo ====================================================
IF DEFINED FAILED exit /b 1
echo All versions built and tested.
exit /b 0

REM ========================================================================
REM Helper Functions
REM ========================================================================

:build_version
REM Builds and tests one PHP version with build.bat, and records the result in PASSED or FAILED
echo.
echo ####################################################
echo # PHP %~1
echo ####################################################
call "%~dp0build.bat" %~1 %INSTALL% < nul
IF "%ERRORLEVEL%"=="0" (
    SET "PASSED=%PASSED% %~1"
) ELSE (
    SET "FAILED=%FAILED% %~1"
)
exit /b 0

:show_usage_failed
call :usage_text
exit /b 1

:show_usage
call :usage_text
exit /b 0

:usage_text
echo.
echo Usage: build-all.bat [install]
echo.
echo   install:         Install each version into its PHP ext folder once its tests pass (optional)
echo.
echo Builds and tests php_ucache.dll for every PHP version with build.bat, one after the other
echo (7.3 to 8.5, then 7.2), into bin\Release\^<version^>\php^<major^>^<minor^>, and lists the versions
echo that passed and failed.
echo.
exit /b 0
