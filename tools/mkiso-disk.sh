#!/bin/sh
set -eu

grub_mkrescue=$1
output=$2
kernel=$3
grub_config=$4
staging=$(mktemp -d)

trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir -p "$staging/boot/grub"
cp "$kernel" "$staging/boot/JanOS.kernel"
cp "$grub_config" "$staging/boot/grub/grub.cfg"
"$grub_mkrescue" -o "$output" "$staging"
