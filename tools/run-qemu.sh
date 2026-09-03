#!/bin/sh
set -eu

qemu=$1
iso=$2
mode=$3
disk1=${4:-}
disk2=${5:-}

debug_args=
case "$mode" in
debug|debug-sata) debug_args='-s -S' ;;
esac

case "$mode" in
sata|debug-sata)
  set -- $debug_args -smp 2 -accel tcg,thread=multi -m 1G -machine pc -cpu qemu64 -vga std -display default -serial stdio \
    -drive "id=os_file,file=$iso,format=raw,if=none" \
    -device ahci,id=ahci -device ide-hd,drive=os_file,bus=ahci.0
  if [ -n "$disk1" ]; then
    set -- "$@" -drive "id=test_disk1,file=$disk1,format=raw,if=none" \
      -device ide-hd,drive=test_disk1,bus=ahci.1
  fi
  if [ -n "$disk2" ]; then
    set -- "$@" -drive "id=test_disk2,file=$disk2,format=raw,if=none" \
      -device ide-hd,drive=test_disk2,bus=ahci.2
  fi
  ;;
*)
  set -- $debug_args -smp 2 -accel tcg,thread=multi -m 1G -machine pc -cpu qemu64 -vga std -display default -serial stdio \
    -drive "file=$iso,format=raw"
  if [ -n "$disk1" ]; then
    set -- "$@" -drive "file=$disk1,format=raw"
  fi
  ;;
esac

exec "$qemu" "$@"
