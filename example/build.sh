#!/usr/bin/env bash

if [ -z "$GCC_PREFIX" ]; then
    echo "Environment variable GCC_PREFIX is empty."
    echo "GCC_PREFIX has to be the path to a custom installation of GCC."
    echo ""
    exit 1
fi

set -e
set -x

wget -c "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"

cd ..
./generate.sh
cp -f linuxdeploy-plugin-checkrt.sh example
cd example

chmod a+x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-checkrt.sh

"$GCC_PREFIX/bin/g++" -O2 example.cpp -o example -s

export LD_LIBRARY_PATH="$GCC_PREFIX/lib64:$GCC_PREFIX/lib:$LD_LIBRARY_PATH"
export LINUXDEPLOY_OUTPUT_VERSION=1
./linuxdeploy-x86_64.AppImage --appdir=appdir -pcheckrt -oappimage -eexample -iexample.png -dexample.desktop
