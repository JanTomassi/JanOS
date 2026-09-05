#!/bin/sh
set -eu

qemu=$1
gdb=$2
kernel=$3
qemu_script=$4
iso=$5
mode=$6
disk1=${7:-}
disk2=${8:-}
qemu_pid=

cleanup() {
	if [ -n "$qemu_pid" ]; then
		kill "$qemu_pid" 2>/dev/null || true
		wait "$qemu_pid" 2>/dev/null || true
	fi
}
trap cleanup EXIT HUP INT TERM

"$qemu_script" "$qemu" "$iso" "$mode" "$disk1" "$disk2" &
qemu_pid=$!

# Give QEMU time to open its GDB stub before the debugger connects.
sleep 1
if ! kill -0 "$qemu_pid" 2>/dev/null; then
	wait "$qemu_pid" 2>/dev/null || true
	exit 1
fi
if ! (exec </dev/tty >/dev/tty) 2>/dev/null; then
	printf '%s\n' 'gdb target requires an interactive terminal' >&2
	exit 2
fi

gdb_status=0
"$gdb" "$kernel" \
	-ex 'set pagination off' \
	-ex 'set breakpoint pending on' \
	-ex 'set disassemble-next-line on' \
	-ex 'set confirm off' \
	-ex 'target remote localhost:1234' </dev/tty >/dev/tty 2>/dev/tty || gdb_status=$?
exit "$gdb_status"
