# syntax=docker/dockerfile:1.7-labs
FROM archlinux:base-devel AS build

RUN pacman -Syu --noconfirm curl gmp libmpc mpfr nasm meson ninja grub libisoburn mtools

WORKDIR /build/tools
COPY tools/get_tools.sh ./
RUN ./get_tools.sh

WORKDIR /build
COPY meson.build meson.options ./
COPY config ./config
COPY data ./data
COPY sdk ./sdk
COPY libc ./libc
COPY kernel ./kernel
COPY apps ./apps
COPY tests ./tests
COPY tools/mkiso.sh tools/run-qemu.sh ./tools/

RUN meson setup out --cross-file config/i686-elf.ini --prefix=/usr --libdir=lib32 \
    -Diso=enabled -Dqemu=disabled
RUN meson compile -C out iso

FROM scratch AS iso
COPY --from=build /build/out/JanOS.iso /
