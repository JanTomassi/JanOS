#!/bin/sh
set -e
. ./build.sh
 
mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub
 
cp sysroot/boot/JanOS.kernel isodir/boot/JanOS.kernel
cat > isodir/boot/grub/grub.cfg << EOF
set timeout=1

menuentry "JanOS" {
	multiboot /boot/JanOS.kernel
}
EOF
grub-mkrescue -o JanOS.iso isodir
