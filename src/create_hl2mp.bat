@echo off

devtools\bin\vpc.exe /hl2mp /define:SOURCESDK +game /mksln HL2MP.sln

if errorlevel 1 (
    echo.
    echo VPC failed with exit code %ERRORLEVEL%.
    pause
)
