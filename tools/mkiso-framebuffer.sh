#!/bin/sh
set -eu

grub_mkrescue=$1
output=$2
kernel=$3
server=$4
client=$5
font=$6
grub_config=$7
staging=$(mktemp -d)

trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir -p "$staging/boot/grub"
cp "$kernel" "$staging/boot/JanOS.kernel"
cp "$server" "$staging/boot/fbserver"
cp "$client" "$staging/boot/fbclient"
cp "$font" "$staging/boot/janos.psf2"
cp "$grub_config" "$staging/boot/grub/grub.cfg"
"$grub_mkrescue" -o "$output" "$staging"
