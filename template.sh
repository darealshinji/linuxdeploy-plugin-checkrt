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

    if [ -e "$CHECKRTDIR/cxx/libstdc++.so.6" ]; then
        ver_local="$("$CHECKRTDIR/checkrt" "$CHECKRTDIR/cxx/libstdc++.so.6" | tail -n1)"
        ver_sys="$("$CHECKRTDIR/checkrt" "libstdc++.so.6" | tail -n1)"

        if [ $ver_sys -gt $ver_local ]; then
            CHECKRT_LIBS="$CHECKRTDIR/cxx:"
        fi
    fi

    if [ -e "$CHECKRTDIR/gcc/libgcc_s.so.1" ]; then
        ver_local="$("$CHECKRTDIR/checkrt" "$CHECKRTDIR/gcc/libgcc_s.so.1" | tail -n1)"
        ver_sys="$("$CHECKRTDIR/checkrt" "libgcc_s.so.1" | tail -n1)"

        if [ $ver_sys -gt $ver_local ]; then
            CHECKRT_LIBS="$CHECKRTDIR/gcc:$CHECKRT_LIBS"
        fi
    fi

    if [ -n "$CHECKRT_LIBS" ]; then
        export LD_LIBRARY_PATH="${CHECKRT_LIBS}${LD_LIBRARY_PATH}"
    fi
fi

if [ -f "$CHECKRTDIR/exec.so" ]; then
    export LD_PRELOAD="$CHECKRTDIR/exec.so:${LD_PRELOAD}"
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

mkdir cxx gcc
cp -v "$(./checkrt "libstdc++.so.6" | head -n1)" "$PWD/cxx"
cp -v "$(./checkrt "libgcc_s.so.1" | head -n1)" "$PWD/gcc"
