SYSTEM_HEADER_PROJECTS="libc kernel"
PROJECTS="libc kernel"
 
export MAKE=${MAKE:-make}
export HOST=${HOST:-$(./default-host.sh)}
 
export AR="$(pwd)/tools/build/bin/i686-elf-ar"
export AS='nasm -f elf32'
export CC="$(pwd)/tools/build/bin/i686-elf-gcc"
 
export PREFIX=/usr
export EXEC_PREFIX=$PREFIX
export BOOTDIR=/boot
export LIBDIR=$EXEC_PREFIX/lib32
export INCLUDEDIR=$PREFIX/include
 
export CFLAGS='-O0 -g'
export CPPFLAGS=''
 
# Configure the cross-compiler to use the desired system root.
export SYSROOT="$(pwd)/sysroot"
export CC="$CC --sysroot=$SYSROOT"
 
# Work around that the -elf gcc targets doesn't have a system include directory
# because it was configured with --without-headers rather than --with-sysroot.
if echo "$HOST" | grep -Eq -- '-elf($|-)'; then
  export CC="$CC -isystem=$INCLUDEDIR"
fi
