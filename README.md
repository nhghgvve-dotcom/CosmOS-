# CosmOS — first bootable kernel prototype

This is a small, freestanding x86-64 kernel project intended to boot in a UEFI VirtualBox VM. It does **not** use a Linux kernel. Limine is used as the bootloader; the kernel is our own C program.

The first milestone is deliberately small: Limine loads `cosmos.elf`, the kernel obtains a 32-bit framebuffer, paints a dark-space startup screen, and halts. It is not yet a full desktop, installer, filesystem, or app store.

## Build on Windows 11 with WSL2 (Ubuntu)

1. Install WSL2 from an elevated PowerShell window:

   ```powershell
   wsl --install -d Ubuntu
   ```

   Restart if Windows asks, then open Ubuntu from the Start menu and create its Linux username/password.

2. In the Ubuntu terminal, install build tools:

   ```bash
   sudo apt update
   sudo apt install -y build-essential git make gcc binutils xorriso nasm mtools autoconf automake
   ```

3. Copy/extract this `CosmOS-Starter` folder into your WSL home directory. If it is in your Windows Downloads folder, it is usually available under `/mnt/c/Users/YOUR_WINDOWS_USERNAME/Downloads/`.

4. Enter the project directory and build:

   ```bash
   cd ~/CosmOS-Starter
   chmod +x build.sh
   ./build.sh
   ```

   The first build downloads the Limine bootloader source and builds its UEFI files. Internet access is needed for that first build.

5. The UEFI-bootable ISO will be created at:

   ```text
   build/CosmOS.iso
   ```

## Test in VirtualBox

1. Create a new VM named `CosmOS`.
2. Type: Linux; Version: Other Linux (64-bit). This only tells VirtualBox which generic virtual hardware profile to use; CosmOS itself is not Linux.
3. Give it at least 2 GB RAM and a 20 GB dynamically allocated virtual disk.
4. In Settings → System → Motherboard, enable **EFI (special OSes only)**.
5. In Settings → Display, set Video Memory to 128 MB.
6. Under Storage, attach `build/CosmOS.iso` to the virtual optical drive.
7. Start the VM. Choose `/CosmOS (First Boot)` if the Limine menu appears.

Do not install this prototype onto your laptop's real drive. Test it in a new VirtualBox VM only.

## What this prototype does not do yet

- No disk installer or persistent filesystem.
- No USB, audio, networking, GPU acceleration, or general device drivers.
- No window manager or app store yet.
- No Windows `.exe` support yet.

## Project layout

- `kernel/kernel.c` — our freestanding kernel and framebuffer startup screen.
- `linker.ld` — kernel memory layout.
- `boot/limine.conf` — boot menu configuration.
- `build.sh` — compiles the kernel and assembles a bootable ISO.

## Notes

The ISO includes Limine as a third-party bootloader. Review its license and the current upstream documentation before redistributing a built image. The build uses the upstream `v12.x` branch; upstream changes may occasionally require build-script adjustments.

## Latest ISO build

The current `main` branch includes the PS/2 mouse-failure fallback and keyboard dock navigation. GitHub Actions builds the UEFI ISO on every push to `main`; download the `CosmOS-UEFI-ISO` artifact from the latest successful run.
