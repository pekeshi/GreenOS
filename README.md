# GreenyOS

GreenyOS is an experimental x86-64 operating system. Its current boot path uses
UEFI as a firmware loader, then transfers control to a separate freestanding ELF
kernel.

## Build and run

On Windows, install:

- MinGW-w64 GCC targeting `x86_64-w64-mingw32` for the UEFI application.
- GCC targeting `x86_64-elf` for the kernel.
- QEMU for x86-64 and an x86-64 OVMF firmware image.

Add the compiler and QEMU executables to `PATH`, then run:

```bat
build.bat
run.bat
```

The scripts accept `EFI_CC`, `KERNEL_CC`, `QEMU_EXE`, `OVMF_CODE`, and
`OVMF_VARS` environment-variable overrides. The build places the EFI loader at
`build\esp\EFI\BOOT\BOOTX64.EFI` and the kernel at `build\esp\KERNEL.ELF`.

The QEMU window displays the kernel shell on the GOP framebuffer. Type commands
into the focused QEMU window; the kernel reads the emulated PS/2 keyboard. COM1
output is captured in a per-run `build\serial-*.log` file for boot diagnostics,
and COM1 input remains available while QEMU is running from a terminal. Each
run also gets its own writable OVMF variable store, so starting another QEMU
instance does not contend for the serial log or variable-store file.
Terminal text begins flush with the inner window edges. Use Page Up and Page
Down to browse up to 256 lines of scrollback; the `uptime` command reports
elapsed seconds and milliseconds.
The shell's `shutdown` and `reboot` commands use QEMU/Q35 ACPI and PC chipset
power-control ports; `halt` stops the CPU without powering it off. Generic
firmware ACPI-table-based power management is not implemented yet.
Run `themes` in the shell to list the bright green, blue, red, orange, and
yellow console themes; apply one with `settheme green` (or another listed
name). Each theme keeps a dark background for readable text and uses bright
colors for accents.

## Current boot and kernel boundary

The UEFI loader reads `KERNEL.ELF` from the EFI system partition, loads its
64-bit ELF load segments, captures the GOP framebuffer and UEFI memory map,
calls `ExitBootServices`, and jumps to the kernel entry point with a versioned
`GreenyOSBootInfo` structure. The initial kernel ELF is linked at a fixed
64 MiB physical address.

The kernel initializes a physical-page allocator from conventional UEFI memory,
installs its own identity-mapped page tables for the first 16 GiB, loads its own
GDT and IDT, remaps the legacy PIC, and programs a 100 Hz PIT timer. The shell
renders text on the framebuffer and reads the emulated PS/2 keyboard. The COM1
driver remains available for diagnostics and optional keyboard input. The
framebuffer shell currently uses a compact uppercase bitmap font.

## Next milestones

- Add exception/interrupt diagnostics and improve keyboard layout support.
- Replace the fixed kernel load address with relocatable ELF loading.
- Build windowing, mouse input, and a GUI on top of the framebuffer.
- Add user mode and process isolation after the kernel foundations are stable.
