#!/bin/sh
set -eu

output=$1
application=$2
font=$3

rm -f "$output"
mkfs.fat -C -F 16 -S 512 -s 4 -R 4 -n JANOS -i 4A4E4F53 "$output" 16384 >/dev/null
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$application" ::CALC
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$font" ::JANOS.PSF
fsck.fat -n "$output" >/dev/null
