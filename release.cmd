@echo off
rem Build and package N.O.V.A. 3 PC into dist\. Options: --test --apk PATH --no-logos --no-zip
setlocal
where py >NUL 2>NUL
if %errorlevel%==0 (
    py -3 "%~dp0tools\release.py" %*
) else (
    python "%~dp0tools\release.py" %*
)
if errorlevel 1 (
    echo.
    echo Release failed. See the messages above.
    pause
    exit /b 1
)
