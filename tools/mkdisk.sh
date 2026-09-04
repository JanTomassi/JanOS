#!/bin/sh
set -eu

output=$1
calc=$2
pingpong=$3
server=$4
client=$5
procserv=$6
font=$7

rm -f "$output"
mkfs.fat -C -F 16 -S 512 -s 4 -R 4 -n JANOS -i 4A4E4F53 "$output" 16384 >/dev/null
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$calc" ::CALC
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$pingpong" ::PINGPONG
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$server" ::FBSERVER
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$client" ::FBCLIENT
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$procserv" ::PROCSERV
MTOOLS_SKIP_CHECK=1 mcopy -i "$output" "$font" ::JANOS.PSF
fsck.fat -n "$output" >/dev/null
