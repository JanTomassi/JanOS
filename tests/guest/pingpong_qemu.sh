#!/bin/sh
set -eu

qemu=$1
iso=$2
disk=${3:-}
smp=${JANOS_QEMU_SMP:-2}
client_cpu=1
if [ "$smp" -gt 3 ]; then
	client_cpu=3
fi
log=$(mktemp)
pid=
cleanup() {
	if [ -n "$pid" ]; then
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
	fi
	rm -f "$log"
}
trap cleanup EXIT HUP INT TERM

if [ -z "$disk" ]; then
    printf 'pingpong guest: application disk missing\n' >&2
    exit 2
fi
"$qemu" -smp "$smp" -m 1G -machine pc -cpu qemu64 \
    -drive "id=os_file,file=$iso,format=raw,if=none" \
    -device ahci,id=ahci -device ide-hd,drive=os_file,bus=ahci.0 \
    -drive "id=test_disk1,file=$disk,format=raw,if=none" \
    -device ide-hd,drive=test_disk1,bus=ahci.1 \
    -serial "file:$log" -display none -no-reboot -snapshot &
pid=$!
for _ in $(seq 1 20); do
	if grep -q 'PINGPONG_CLIENT_OK' "$log" &&
	   grep -q 'pong 0 19' "$log" && grep -q 'pong 1 19' "$log" &&
	   grep -q "PINGPONG_AFFINITY server=0 client0=0 client1=$client_cpu" "$log"; then
		if grep -Eq 'KERNEL PANIC|Unhandled exception|User fault:|Kernel page fault:' "$log"; then
			break
		fi
		exit 0
	fi
	sleep 1
done
cat "$log"
exit 1
