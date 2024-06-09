#!/bin/sh
set -e
. ./iso.sh


is_debug=false

while getopts "hd" args; do
	case $args in
		h)
			echo "Run the os in qemu use -d for debugging"
			echo "to connect to the debugging instance use gdb with localhost 1234"
			;;
		d)
			is_debug=true
			;;
	esac
done

if test "$is_debug" = true ; then
	qemu-system-$(./target-triplet-to-arch.sh $HOST) -s -S -cdrom JanOS.iso -audiodev pa,id=speaker -machine pcspk-audiodev=speaker
else
	qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom JanOS.iso -audiodev pa,id=speaker -machine pcspk-audiodev=speaker
fi
