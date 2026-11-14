@echo off

devtools\bin\vpc.exe /win32 /hl2mp /define:SOURCESDK +game /mksln PHYSBOX_x86.sln msbuild /m /p:Configuration=Release

if errorlevel 1 (
    echo.
    echo VPC failed with exit code %ERRORLEVEL%.
    pause
)

pause