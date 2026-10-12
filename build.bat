@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"
set "ESP_DIR=%BUILD_DIR%\esp"
if not defined EFI_CC set "EFI_CC=gcc"
if not defined KERNEL_CC set "KERNEL_CC=x86_64-elf-gcc"

where "%EFI_CC%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: UEFI compiler was not found. Set EFI_CC to x86_64-w64-mingw32-gcc.
    exit /b 1
)

for /f "delims=" %%T in ('"%EFI_CC%" -dumpmachine 2^>nul') do set "EFI_TARGET=%%T"
if not "%EFI_TARGET%"=="x86_64-w64-mingw32" (
    echo ERROR: Expected the x86_64-w64-mingw32 target; found "%EFI_TARGET%".
    exit /b 1
)

where "%KERNEL_CC%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Freestanding kernel compiler was not found. Install x86_64-elf-gcc or set KERNEL_CC.
    exit /b 1
)

for /f "delims=" %%T in ('"%KERNEL_CC%" -dumpmachine 2^>nul') do set "KERNEL_TARGET=%%T"
if not "%KERNEL_TARGET%"=="x86_64-elf" (
    echo ERROR: Expected the x86_64-elf target; found "%KERNEL_TARGET%".
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if errorlevel 1 exit /b 1
if not exist "%ESP_DIR%\EFI\BOOT" mkdir "%ESP_DIR%\EFI\BOOT"
if errorlevel 1 exit /b 1

"%KERNEL_CC%" ^
    -std=c11 -O2 -Wall -Wextra ^
    -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector ^
    -fno-asynchronous-unwind-tables -fno-unwind-tables ^
    -mno-red-zone -mgeneral-regs-only -mcmodel=small ^
    -I"%ROOT%kernel\include" -I"%ROOT%boot" ^
    -nostdlib -static -no-pie ^
    -Wl,-T,"%ROOT%kernel\linker.ld" ^
    -Wl,-Map,"%BUILD_DIR%\KERNEL.map" ^
    -Wl,-z,noexecstack ^
    "%ROOT%kernel\entry.S" ^
    "%ROOT%kernel\gdt.c" ^
    "%ROOT%kernel\kernel.c" ^
    "%ROOT%kernel\memory.c" ^
    "%ROOT%kernel\paging.c" ^
    "%ROOT%kernel\interrupts.c" ^
    "%ROOT%kernel\power.c" ^
    "%ROOT%kernel\serial.c" ^
    "%ROOT%kernel\usb_xhci.c" ^
    "%ROOT%kernel\framebuffer.c" ^
    "%ROOT%kernel\keyboard.c" ^
    "%ROOT%kernel\shell.c" ^
    "%ROOT%kernel\vga.c" ^
    -o "%BUILD_DIR%\KERNEL.ELF"
if errorlevel 1 (
    echo ERROR: Freestanding kernel build failed.
    exit /b 1
)

"%EFI_CC%" ^
    -std=c11 -O2 -Wall -Wextra ^
    -ffreestanding -fno-builtin -fno-stack-protector ^
    -fno-asynchronous-unwind-tables -fno-unwind-tables ^
    -fshort-wchar ^
    -I"%ROOT%kernel\include" -I"%ROOT%boot" ^
    "%ROOT%boot\bootx64.c" ^
    -nostdlib -Wl,--subsystem,10 -Wl,--entry,efi_main ^
    -Wl,--enable-reloc-section -o "%BUILD_DIR%\BOOTX64.EFI"
if errorlevel 1 (
    echo ERROR: UEFI bootloader build failed.
    exit /b 1
)

copy /y "%BUILD_DIR%\BOOTX64.EFI" "%ESP_DIR%\EFI\BOOT\BOOTX64.EFI" >nul
if errorlevel 1 (
    echo ERROR: Could not copy the EFI application into the ESP folder.
    exit /b 1
)
copy /y "%BUILD_DIR%\KERNEL.ELF" "%ESP_DIR%\KERNEL.ELF" >nul
if errorlevel 1 (
    echo ERROR: Could not copy KERNEL.ELF into the ESP folder.
    exit /b 1
)

echo Built UEFI loader and freestanding kernel.
echo Bootloader: "%BUILD_DIR%\BOOTX64.EFI"
echo Kernel:     "%BUILD_DIR%\KERNEL.ELF"
exit /b 0