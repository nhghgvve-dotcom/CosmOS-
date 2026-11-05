#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

for tool in git make gcc ld xorriso autoconf automake; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Missing tool: $tool"; exit 1; }
done

if [ ! -d limine-src/.git ]; then
    git clone --depth=1 --branch=v12.x https://github.com/limine-bootloader/limine.git limine-src
fi

if [ ! -x limine-src/configure ]; then
    (cd limine-src && ./bootstrap)
fi
(cd limine-src && ./configure)
make -C limine-src
HEADER="$(find limine-src -name limine.h -type f -print -quit)"
[ -n "$HEADER" ] || { echo "Could not find limine.h in the Limine checkout."; exit 1; }

mkdir -p build iso_root/boot/limine iso_root/EFI/BOOT
gcc -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector \
    -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel -m64 \
    -I"$(dirname "$HEADER")" -c kernel/kernel.c -o build/kernel.o
ld -nostdlib -static -z max-page-size=0x1000 -T linker.ld \
    -o build/cosmos.elf build/kernel.o

cp build/cosmos.elf iso_root/boot/cosmos.elf
cp boot/limine.conf iso_root/limine.conf
cp limine-src/bin/limine-uefi-cd.bin iso_root/boot/limine/
cp limine-src/bin/BOOTX64.EFI iso_root/EFI/BOOT/BOOTX64.EFI

xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
    --protective-msdos-label iso_root -o build/CosmOS.iso

echo
echo "Build complete: $ROOT/build/CosmOS.iso"
