#!/bin/sh

git clone https://github.com/xiph/rnnoise.git
cd rnnoise
./autogen.sh
./configure
make clean
make
