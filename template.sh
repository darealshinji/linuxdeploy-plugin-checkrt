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
    CHECKRT_LIBS="$($CHECKRTDIR/checkrt)"

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

LDFLAGS="-Wl,--as-needed -static-libgcc -ldl -s"

echo "Compiling checkrt"
cc -O2 checkrt.c -o checkrt $LDFLAGS

echo "Compiling exec.so"
cc -shared -O2 -fPIC exec.c -o exec.so $LDFLAGS
rm checkrt.c exec.c

echo "Copy libgcc_s.so.1 and libstdc++.so.6"
./checkrt --copy
