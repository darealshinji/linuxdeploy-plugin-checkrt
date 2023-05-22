linuxdeploy-plugin-checkrt
--------------------------

This linuxdeploy plugin allows you to deploy `libstdc++.so.6` and `libgcc_s.so.1`
without breaking compatibility. The installed AppRun hook script will compare the
symbol version numbers between the deployed libraries and those on your system
and only add the deployed ones to `LD_LIBRARY_PATH` if they're newer.

Additionally the library `exec.so` is deployed and will be preloaded by AppRun if
it's found. This library is intended to restore the environment of the AppImage's
parent process in order to avoid library clashing of bundled libraries with external
processes called from within the AppImage (i.e. when a webbrowser is opened or a
terminal emulator is started).
If you don't want `exec.so` then simply delete it before creating your final AppImage.

Requirements
------------
C compiler (GCC or Clang)

Usage
-----
``` sh
# get linuxdeploy and linuxdeploy-plugin-checkrt and make them executable
wget -c "https://github.com/darealshinji/linuxdeploy-plugin-checkrt/releases/download/continuous/linuxdeploy-plugin-checkrt.sh"
wget -c "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
chmod a+x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-checkrt.sh

# add GCC's libraries to the search path
# and call through linuxdeploy using the `--plugin checkrt` option
LD_LIBRARY_PATH="/path/to/my/gcc-snapshot/lib64" \
  ./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin checkrt --output appimage --icon-file mypackage.png --desktop-file mypackage.desktop
```

Why?
----
`libstdc++.so.6` and `libgcc_s.so.1` are part of GCC and if you compile code it
will be linked against these libraries if required. If you use a newer version
of GCC than normally available on your system these libraries will also be newer
and chances are high that your program - linked against these newer versions - is
no longer compatible to those installed by default on your system.

Starting your program would get you an error message like this:
```
./myApp: /lib/x86_64-linux-gnu/libstdc++.so.6: version `GLIBCXX_3.4.29' not found (required by ./myApp)
```

However you cannot simply bundle these libraries as usual. If the AppImage is
started on a different system and an unbundled system library depends on a newer
version than available from the AppImage it won't work either.

This linuxdeploy plugin will install a hook script that quickly checks and compares
the internal version number of the libraries and only adds them to the search
path if they're newer.

Hacking
-------
The file `linuxdeploy-plugin-checkrt.sh` is created from `generate.sh`.
To add changes to the plugin you must edit the other files and then run `./generate.sh`.
