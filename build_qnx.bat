@echo off
set BUILD_TYPE=%1
set QNX_DIR=%2
set PLATFORM=%3
set QNX_ENV=%QNX_DIR%/qnxsdp-env.bat

if not exist %QNX_DIR% (
  ECHO Directory %QNX_DIR% does not exist
  GOTO show_usage
)

if not exist %QNX_ENV% (
  echo File %QNX_ENV% does not exist
  GOTO show_usage
)

if not exist %QNX_ENV% (
  echo File %QNX_ENV% does not exist
  GOTO show_usage
)

where /q cmake
IF ERRORLEVEL 1 (
  ECHO CMake not found
  GOTO error
)

2>NUL CALL :CASE_%PLATFORM%
IF ERRORLEVEL 1 CALL :DEFAULT_PLATFORM

:CASE_x86_64
  GOTO check_build_type
:CASE_aarch64le
  GOTO check_build_type
:DEFAULT_PLATFORM
  ECHO Unknown target platform "%PLATFORM%"
  GOTO show_usage
  
:check_build_type
2>NUL CALL :CASE_%BUILD_TYPE%
IF ERRORLEVEL 1 CALL :DEFAULT_BUILD_TYPE

:CASE_Release
  GOTO build
:CASE_Debug
  GOTO build
:DEFAULT_BUILD_TYPE
  ECHO Unknown build type "%BUILD_TYPE%"
  GOTO show_usage

:build
set PATH=%PATH%;D:\Programme\CMake\bin
call %QNX_ENV%
@echo off
rm -r build
@rem call cmake -S . -B build -DUSE_QCC=ON -G "Unix Makefiles" --debug-trycompile -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON .
call cmake -S . -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DUSE_QCC=ON -G "Unix Makefiles" || goto error
call cmake --build build || goto error

echo Build for QNX completed successfully
exit(0)

:show_usage
  echo.
  echo Usage: build_qnx.bat ^[build_type] ^[qnx_install_dir^] ^[target_platform^]
  echo.
  echo Parameters:
  echo   build_type:        Debug^|Release
  echo   qnx_install_dir:   Path to QNX SDP installation, e.g. "C:\QNX\qnx710"
  echo   target_platform:   aarch64le^|x86_64
  echo.

:error
echo Build for QNX failed
exit(-1)