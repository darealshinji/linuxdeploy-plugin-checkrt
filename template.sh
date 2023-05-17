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
if [ -x "$APPDIR/apprun-hooks/checkrt/checkrt" ]; then
    export LD_LIBRARY_PATH="$($APPDIR/apprun-hooks/checkrt/checkrt)${LD_LIBRARY_PATH}"
fi
if [ -f "$APPDIR/apprun-hooks/checkrt/exec.so" ]; then
    export LD_PRELOAD="$APPDIR/apprun-hooks/checkrt/exec.so:${LD_PRELOAD}"
fi
EOF

cd "$APPDIR/apprun-hooks/checkrt"

save_files

echo "Compiling checkrt"
gcc -O2 checkrt.c -o checkrt -s -ldl
echo "Compiling exec.so"
gcc -shared -O2 -fPIC exec.c -o exec.so -s -ldl
rm checkrt.c exec.c

./checkrt --copy-libraries
