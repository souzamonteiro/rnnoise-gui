#!/usr/bin/env bash

echo ">> Installing dependencies for Audio Recorder (MSYS2 + GTK 3)..."
echo ">> Make sure you are running this in the UCRT64 environment!"

# Update package database.
pacman -Sy --noconfirm

# Install GTK 3 and development tools.
pacman -S --needed --noconfirm \
    mingw-w64-ucrt-x86_64-gtk3 \
    mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-pkgconf \
    make

echo "✅ Done! Now you can run 'make' to build the project."

