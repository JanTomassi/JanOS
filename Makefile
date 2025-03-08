export SYSTEM_HEADER_PROJECTS ::= libc kernel
export PROJECTS ::= libc kernel

include make.config

export AR ::= $(shell pwd)/tools/build/bin/i686-elf-ar
export AS ::= nasm -f elf32
export CC ::= $(shell pwd)/tools/build/bin/i686-elf-gcc

export PREFIX=/usr
export EXEC_PREFIX=${PREFIX}
export BOOTDIR=/boot
export LIBDIR=${EXEC_PREFIX}/lib32
export INCLUDEDIR=${PREFIX}/include

# Configure the cross-compiler to use the desired system root.
export SYSROOT ::= $(shell pwd)/sysroot

export CFLAGS ::= -O0 -g --sysroot=${SYSROOT}
export CPPFLAGS ::= --sysroot=${SYSROOT}

# Work around that the -elf gcc targets doesn't have a system include directory
# because it was configured with --without-headers rather than --with-sysroot.
ifeq ($(findstring elf, ${HOST}), elf)
export CFLAGS += -isystem=${INCLUDEDIR}
export CPPFLAGS += -isystem=${INCLUDEDIR}
endif


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


build: headers
	if $$(command -v etags &> /dev/null) && $$(command -v find &> /dev/null) ; then \
	    find ${PROJECTS} -name "*.[chCH]" -print | etags - ; \
	fi

	for PROJECT in ${PROJECTS}; do \
	    DESTDIR="${SYSROOT}" ${MAKE} -C $$PROJECT install ; \
	done


JanOS.iso: build
	mkdir -p isodir isodir/boot isodir/boot/grub
	cp sysroot/boot/JanOS.kernel isodir/boot/JanOS.kernel

	echo "set timeout=1"                     > isodir/boot/grub/grub.cfg
	echo "insmod all_video"                 >> isodir/boot/grub/grub.cfg
	echo 'menuentry "JanOS" {'              >> isodir/boot/grub/grub.cfg
	echo "    multiboot /boot/JanOS.kernel" >> isodir/boot/grub/grub.cfg
	echo "}"                                >> isodir/boot/grub/grub.cfg

	grub-mkrescue -o JanOS.iso isodir


qemu_debug: JanOS.iso
	qemu-system-${ARCH} -s -S -cdrom $< -audiodev pa,id=speaker \
	-machine pcspk-audiodev=speaker


qemu: JanOS.iso
	qemu-system-${ARCH} -cdrom JanOS.iso -audiodev pa,id=speaker \
	-machine pcspk-audiodev=speaker


.PHONY: build clean headers qemu qemu_debug
