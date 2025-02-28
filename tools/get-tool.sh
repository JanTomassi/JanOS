#!/bin/bash

NUM_PROC=$(nproc)

mkdir -p build

export PREFIX="$(pwd)/build"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

mkdir -p src

pushd src

if [ ! -f "binutils-2.42.tar.xz" ]; then
    DOWNLOAD_LIST+="https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz -o binutils-2.42.tar.xz "
fi
if [ ! -f "gdb-14.2.tar.xz" ]; then
    DOWNLOAD_LIST+="https://ftp.gnu.org/gnu/gdb/gdb-14.2.tar.xz -o gdb-14.2.tar.xz "
fi
if [ ! -f "gcc-14.1.0.tar.xz" ]; then
    DOWNLOAD_LIST+="https://ftp.gnu.org/gnu/gcc/gcc-14.1.0/gcc-14.1.0.tar.xz -o gcc-14.1.0.tar.xz "
fi

curl --parallel --parallel-immediate --parallel-max 3 $DOWNLOAD_LIST

echo decompress binutils-2.42.tar.xz
tar xf binutils-2.42.tar.xz
echo decompress gdb-14.2.tar.xz
tar xf gdb-14.2.tar.xz
echo decompress gcc-14.1.0.tar.xz
tar xf gcc-14.1.0.tar.xz

if [ ! -f "$PREFIX/bin/$TARGET-ld" ]; then
    mkdir -p binutils-2.42-build
    pushd binutils-2.42-build

    ../binutils-2.42/configure --target="$TARGET" --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror

    make -j $NUM_PROC
    make install -j $NUM_PROC

    popd
fi

if [ ! -f "$PREFIX/bin/$TARGET-gdb" ]; then
    mkdir -p gdb-14.2-build
    pushd gdb-14.2-build

    ../gdb-14.2/configure --target="$TARGET" --prefix="$PREFIX" --disable-werror

    make -j $NUM_PROC all-gdb
    make -j $NUM_PROC install-gdb

    popd
fi

if [ ! -f "$PREFIX/bin/$TARGET-gcc" ]; then
    mkdir -p gcc-14.1.0-build
    pushd gcc-14.1.0-build


   ../gcc-14.1.0/configure --target="$TARGET" --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers

   make -j $NUM_PROC all-gcc
   make -j $NUM_PROC all-target-libgcc
   make -j $NUM_PROC install-gcc
   make -j $NUM_PROC install-target-libgcc

   popd
fi



popd
