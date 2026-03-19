#!/bin/sh

mkdir -p autotools
autoreconf -fi
mkdir -p build-sdl
cd build-sdl && ../configure "$@"

