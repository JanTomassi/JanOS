#!/bin/sh
set -eu

qemu=$1
iso=$2
disk=${3:-}
smp=${JANOS_QEMU_SMP:-2}
log=$(mktemp)
keep_log=0
if [ -n "${JANOS_QEMU_LOG:-}" ]; then
	log=$JANOS_QEMU_LOG
	: > "$log"
	keep_log=1
fi
monitor=$(mktemp -u)
qemu_pid=

cleanup() {
	if [ -n "$qemu_pid" ]; then
		kill "$qemu_pid" 2>/dev/null || true
		wait "$qemu_pid" 2>/dev/null || true
	fi
	if [ "$keep_log" -eq 0 ]; then
		rm -f "$log"
	fi
	rm -f "$monitor"
}
trap cleanup EXIT HUP INT TERM

if [ -z "$disk" ]; then
	printf 'shell guest: application disk missing\n' >&2
	exit 2
fi
if ! command -v nc >/dev/null 2>&1; then
	printf 'shell guest: nc is required\n' >&2
	exit 77
fi

"$qemu" -smp "$smp" -m 1G -machine pc -cpu qemu64 \
	-drive "id=os_file,file=$iso,format=raw,if=none" \
	-device ahci,id=ahci -device ide-hd,drive=os_file,bus=ahci.0 \
	-drive "id=test_disk1,file=$disk,format=raw,if=none" \
	-device ide-hd,drive=test_disk1,bus=ahci.1 \
	-serial "file:$log" -monitor "unix:$monitor,server=on,wait=off" \
	-display none -no-reboot -snapshot &
qemu_pid=$!

for _ in $(seq 1 30); do
	if grep -q 'SHELL_READY' "$log"; then
		break
	fi
	sleep 1
done
if ! grep -q 'SHELL_READY' "$log"; then
	cat "$log"
	exit 1
fi

send_keys() {
	for key in "$@"; do
		printf 'sendkey %s\n' "$key"
		sleep 0.12
	done
}

send_command() {
	before=$(grep -o 'jan> ' "$log" | wc -l)
	send_keys "$@" | nc -q 0 -U "$monitor" >/dev/null
	for _ in $(seq 1 30); do
		after=$(grep -o 'jan> ' "$log" | wc -l)
		if [ "$after" -gt "$before" ]; then
			return 0
		fi
		sleep 1
	done
	cat "$log"
	exit 1
}

send_command h e l p ret
send_command p s ret
send_command i n f o ret
send_command f o n t ret
send_command c a l c ret
send_command c a l c spc 2 ret
send_command c l e a r ret
send_command h e l p ret
send_command up ret

for _ in $(seq 1 45); do
	if grep -q 'help ps info font calc clear exit' "$log" && \
		grep -Eq 'pid=.*name=calc state=blocked' "$log" && \
		grep -q 'framebuffer=' "$log" && \
		grep -q 'font size=' "$log" && \
		grep -q 'calc started via IPC' "$log" && \
		grep -Eq '^2[[:space:]]*$' "$log" && \
		! grep -q 'clear: framebuffer service unavailable' "$log" && \
		[ "$(grep -c 'help ps info font calc clear exit' "$log")" -ge 2 ]; then
		exit 0
	fi
	if grep -Eq 'KERNEL PANIC|Unhandled exception|User fault:|Kernel page fault:|Slab Allocator.*(unaligned|double free|cross-cache)' "$log"; then
		break
	fi
	sleep 1
done
cat "$log"
exit 1
