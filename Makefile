export SYSTEM_HEADER_PROJECTS ::= libc kernel
export PROJECTS ::= libc kernel

include make.config

build: headers
	if $$(command -v etags &> /dev/null) && $$(command -v find &> /dev/null) ; then \
	    find ${PROJECTS} -name "*.[chCH]" -print | etags - ; \
	fi

	for PROJECT in ${PROJECTS}; do \
	    DESTDIR="${SYSROOT}" ${MAKE} -C $$PROJECT install ; \
	done

headers:
	for PROJECT in ${SYSTEM_HEADER_PROJECTS} ; do \
	    DESTDIR="${SYSROOT}" ${MAKE} -C $${PROJECT} install-headers ; \
	done


clean:
	for PROJECT in ${PROJECTS}; do \
	    ${MAKE} -C $${PROJECT} clean ; \
	done

	rm -f TAGS JanOS.iso
	rm -rf sysroot isodir

JanOS.iso: build
	mkdir -p isodir isodir/boot isodir/boot/grub
	cp sysroot/boot/JanOS.kernel isodir/boot/JanOS.kernel

	echo "set timeout=1"                     > isodir/boot/grub/grub.cfg
	echo "insmod all_video"                  >> isodir/boot/grub/grub.cfg
	echo "insmod acpi"                  	 >> isodir/boot/grub/grub.cfg
	echo 'menuentry "JanOS" {'               >> isodir/boot/grub/grub.cfg
	echo "    multiboot2 /boot/JanOS.kernel" >> isodir/boot/grub/grub.cfg
	echo "}"                                 >> isodir/boot/grub/grub.cfg

	grub-mkrescue -o JanOS.iso isodir


qemu_sata: JanOS.iso
	qemu-system-${ARCH} \
	-m 1G \
	-machine pc -cpu qemu64 \
	-drive id=os_file,file=JanOS.iso,format=raw,if=none \
	-drive id=test_disk,file=harry_potter.raw,format=raw,if=none \
	-device ahci,id=ahci \
	-device ide-hd,drive=os_file,bus=ahci.0 \
	-device ide-hd,drive=test_disk,bus=ahci.1

qemu_sata_debug: JanOS.iso
	qemu-system-${ARCH} -s -S \
	-m 1G \
	-machine pc -cpu qemu64 \
	-drive id=os_file,file=JanOS.iso,format=raw,if=none \
	-drive id=test_disk,file=harry_potter.raw,format=raw,if=none \
	-device ahci,id=ahci \
	-device ide-hd,drive=os_file,bus=ahci.0 \
	-device ide-hd,drive=test_disk,bus=ahci.1

qemu: JanOS.iso
	qemu-system-${ARCH} \
	-smp 2 \
	-m 1G \
	-machine pc -cpu qemu64 \
	-drive file=JanOS.iso,format=raw \
	-drive file=harry_potter.raw,format=raw

qemu_debug: JanOS.iso
	qemu-system-${ARCH} -s -S \
	-smp 2 \
	-m 1G \
	-machine pc -cpu qemu64 \
	-drive file=JanOS.iso,format=raw \
	-drive file=harry_potter.raw,format=raw

.PHONY: build clean headers qemu qemu_debug
