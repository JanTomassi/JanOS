# syntax=docker/dockerfile:1.7-labs
FROM archlinux:base-devel AS build-env
WORKDIR /build
RUN mkdir -p ./tools

COPY ./tools ./tools
COPY ./libc ./libc/
COPY ./kernel ./kernel

RUN cd tools && ./get-tool.sh
RUN pacman -Syu --noconfirm nasm grub libisoburn mtools


FROM build-env as build
WORKDIR /build
COPY Makefile make.config ./
RUN make JanOS.iso


FROM scratch AS iso
COPY --from=build /build/JanOS.iso /
