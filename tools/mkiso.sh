#!/bin/sh
set -eu

grub_mkrescue=$1
output=$2
kernel=$3
application=$4
if [ "$#" -eq 8 ]; then
  pingpong=
  server=$5
  client=$6
  font=$7
  grub_config=$8
elif [ "$#" -eq 9 ]; then
  pingpong=$5
  server=$6
  client=$7
  font=$8
  grub_config=$9
else
  printf 'usage: %s grub-mkrescue output kernel calc [pingpong] fbserver fbclient font grub.cfg\n' "$0" >&2
  exit 2
fi
staging=$(mktemp -d)

trap 'rm -rf "$staging"' EXIT HUP INT TERM
mkdir -p "$staging/boot/grub"
cp "$kernel" "$staging/boot/JanOS.kernel"
cp "$application" "$staging/boot/calc"
if [ -n "$pingpong" ]; then
  cp "$pingpong" "$staging/boot/pingpong"
fi
cp "$server" "$staging/boot/fbserver"
cp "$client" "$staging/boot/fbclient"
cp "$font" "$staging/boot/janos.psf2"
cp "$grub_config" "$staging/boot/grub/grub.cfg"
"$grub_mkrescue" -o "$output" "$staging"
