show_usage() {
    echo "Usage: $0 --appdir <path to AppDir>"
    echo
}

while [ "$1" != "" ]; do
    case "$1" in
        --plugin-api-version)
            echo "0"
            exit 0
            ;;
        --appdir)
            APPDIR="$2"
            shift
            shift
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            echo "Invalid argument: $1"
            echo
            show_usage
            exit 1
            ;;
    esac
done

if [ "$APPDIR" == "" ]; then
    show_usage
    exit 1
fi

APPDIR="$(realpath "$APPDIR")"
echo "Installing AppRun hook 'checkrt'"

mkdir -p "$APPDIR/apprun-hooks/checkrt"

cat > "$APPDIR/apprun-hooks/linuxdeploy-plugin-checkrt.sh" << \EOF
#! /usr/bin/env bash

if [ -z "$APPDIR" ]; then
    APPDIR="$(dirname "$(realpath "$0")")"
fi

CHECKRTDIR="$APPDIR/apprun-hooks/checkrt"

if [ -x "$CHECKRTDIR/checkrt" ]; then
    CHECKRT_LIBS=

    # check for libstdc++
    if [ -e "$CHECKRTDIR/cxx/libstdc++.so.6" ]; then
        ver_bundle="$("$CHECKRTDIR/checkrt" "$CHECKRTDIR/cxx/libstdc++.so.6" | tail -n1)"
        ver_sys="$("$CHECKRTDIR/checkrt" "libstdc++.so.6" | tail -n1)"

        if [ -n "$CHECKRT_DEBUG" ]; then
            echo "CHECKRT>> libstdc++.so.6 system = $ver_sys"
            echo "CHECKRT>> libstdc++.so.6 bundle = $ver_bundle"
        fi

        if [ $ver_bundle -gt $ver_sys ]; then
            CHECKRT_LIBS="$CHECKRTDIR/cxx:"
        fi
    fi

    # check for libgcc
    if [ -e "$CHECKRTDIR/gcc/libgcc_s.so.1" ]; then
        ver_bundle="$("$CHECKRTDIR/checkrt" "$CHECKRTDIR/gcc/libgcc_s.so.1" | tail -n1)"
        ver_sys="$("$CHECKRTDIR/checkrt" "libgcc_s.so.1" | tail -n1)"

        if [ -n "$CHECKRT_DEBUG" ]; then
            echo "CHECKRT>> libgcc_s.so.1 system = $ver_sys"
            echo "CHECKRT>> libgcc_s.so.1 bundle = $ver_bundle"
        fi

        if [ $ver_bundle -gt $ver_sys ]; then
            CHECKRT_LIBS="$CHECKRTDIR/gcc:$CHECKRT_LIBS"
        fi
    fi

    # prepend to LD_LIBRARY_PATH
    if [ -n "$CHECKRT_LIBS" ]; then
        export LD_LIBRARY_PATH="${CHECKRT_LIBS}${LD_LIBRARY_PATH}"
    fi
fi

# check for exec.so
if [ -f "$CHECKRTDIR/exec.so" ]; then
    export LD_PRELOAD="$CHECKRTDIR/exec.so:${LD_PRELOAD}"
fi

# debugging
if [ -n "$CHECKRT_DEBUG" ]; then
    echo "CHECKRT>> LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    echo "CHECKRT>> LD_PRELOAD=$LD_PRELOAD"
fi
EOF

cd "$APPDIR/apprun-hooks/checkrt"

save_files

# check for a compiler
set +e
CC=$(which $(uname -m)-linux-gnu-gcc gcc clang | head -n1)
set -e

test -n $CC || CC=cc
LDFLAGS="-Wl,--as-needed -static-libgcc -ldl -s"
echo "Compiling checkrt"
$CC -O2 checkrt.c -o checkrt $LDFLAGS
echo "Compiling exec.so"
$CC -shared -O2 -fPIC exec.c -o exec.so $LDFLAGS
rm checkrt.c exec.c

mkdir cxx gcc
cp -v "$(./checkrt "libstdc++.so.6" | head -n1)" "$PWD/cxx"
cp -v "$(./checkrt "libgcc_s.so.1" | head -n1)" "$PWD/gcc"
