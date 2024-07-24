# syntax=docker/dockerfile:1.7-labs
FROM archlinux:base-devel AS stage-0

WORKDIR /build

COPY --exclude=Dockerfile ./ ./

RUN ./tools/get-tool.sh

RUN pacman -Syu --noconfirm nasm grub libisoburn mtools

RUN ./iso.sh


FROM scratch AS iso

COPY --from=stage-0 /build/JanOS.iso /
