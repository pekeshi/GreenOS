@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"
set "ESP_DIR=%BUILD_DIR%\esp"
set "RUN_ID=%RANDOM%-%RANDOM%"
set "SERIAL_LOG=%BUILD_DIR%\serial-%RUN_ID%.log"
set "OVMF_VARS_RUN=%BUILD_DIR%\OVMF_VARS-%RUN_ID%.fd"
if not defined QEMU_EXE set "QEMU_EXE=qemu-system-x86_64"

call "%ROOT%build.bat"
if errorlevel 1 exit /b 1

where "%QEMU_EXE%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: QEMU was not found. Install QEMU and add qemu-system-x86_64 to PATH.
    exit /b 1
)

if not defined OVMF_CODE (
    for %%F in (
        "C:\msys64\mingw64\share\qemu\edk2-x86_64-code.fd"
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
        "C:\Program Files\qemu\share\OVMF_CODE.fd"
    ) do if not defined OVMF_CODE if exist "%%~F" set "OVMF_CODE=%%~F"
)
if not defined OVMF_CODE (
    echo ERROR: OVMF firmware was not found. Set OVMF_CODE to the x64 firmware image.
    exit /b 1
)
if not exist "%OVMF_CODE%" (
    echo ERROR: OVMF_CODE does not exist: "%OVMF_CODE%"
    exit /b 1
)

if not defined OVMF_VARS (
    for %%F in (
        "C:\msys64\mingw64\share\qemu\edk2-x86_64-vars.fd"
        "C:\msys64\mingw64\share\qemu\edk2-i386-vars.fd"
        "C:\Program Files\qemu\share\OVMF_VARS.fd"
    ) do if not defined OVMF_VARS if exist "%%~F" set "OVMF_VARS=%%~F"
)

if defined OVMF_VARS (
    if not exist "%OVMF_VARS%" (
        echo ERROR: OVMF_VARS does not exist: "%OVMF_VARS%"
        exit /b 1
    )
    copy /y "%OVMF_VARS%" "%OVMF_VARS_RUN%" >nul
    if errorlevel 1 (
        echo ERROR: Could not prepare the writable OVMF variable store.
        exit /b 1
    )
    echo Serial log: "%SERIAL_LOG%"
    "%QEMU_EXE%" -name "GreenyOS - 0.1 - pekeshi" -machine q35 -m 256M ^
        -display sdl ^
        -drive "if=pflash,format=raw,unit=0,file=%OVMF_CODE%,readonly=on" ^
        -drive "if=pflash,format=raw,unit=1,file=%OVMF_VARS_RUN%" ^
        -drive "format=raw,file=fat:rw:%ESP_DIR%,if=ide,index=0" ^
        -serial "file:%SERIAL_LOG%" -monitor none
) else (
    echo WARNING: No OVMF variable-store image found; using firmware without persistent variables.
    echo Serial log: "%SERIAL_LOG%"
    "%QEMU_EXE%" -name "GreenyOS - 0.1 - pekeshi" -machine q35 -m 256M -display sdl -bios "%OVMF_CODE%" ^
        -drive "format=raw,file=fat:rw:%ESP_DIR%,if=ide,index=0" ^
        -serial "file:%SERIAL_LOG%" -monitor none
)

exit /b %errorlevel%
