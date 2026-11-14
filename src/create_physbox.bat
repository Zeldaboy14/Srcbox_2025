@echo off

devtools\bin\vpc.exe /physbox /define:SOURCESDK +game /mksln PHYSBOX.sln

if errorlevel 1 (
    echo.
    echo VPC failed with exit code %ERRORLEVEL%.
    pause
)

pause