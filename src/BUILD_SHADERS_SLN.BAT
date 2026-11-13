@echo off
setlocal DisableDelayedExpansion
title Srcbox Pre-Build Scripts

:: Get the directory this script is in (should be the src folder)
set "SRC=%~dp0"
cd /d "%SRC%"

echo.
echo ========================================
echo  Running !Compile_All.bat
echo ========================================
echo.
pushd "%SRC%materialsystem\stdshaders"
call "!Compile_All.bat"
popd
echo.
echo !Compile_All.bat finished.

echo.
echo ========================================
echo  Running !Compile_GMod.bat
echo ========================================
echo.
pushd "%SRC%materialsystem\stdshaders"
call "!Compile_GMod.bat"
popd
echo.
echo !Compile_GMod.bat finished.

echo.
echo ========================================
echo  Running create_physbox.bat
echo ========================================
echo.
call "%SRC%create_physbox.bat"
echo.
echo create_physbox.bat finished.
echo.
echo All scripts completed.