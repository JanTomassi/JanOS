#!/bin/sh
set -eu

grub_mkrescue=$1
output=$2
kernel=$3
application=$4
grub_config=$5
staging=$(mktemp -d)

trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir -p "$staging/boot/grub"
cp "$kernel" "$staging/boot/JanOS.kernel"
cp "$application" "$staging/boot/calc"
cp "$grub_config" "$staging/boot/grub/grub.cfg"
"$grub_mkrescue" -o "$output" "$staging"
